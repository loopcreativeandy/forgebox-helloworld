#!/usr/bin/env python3
"""Send a devnet SOL transfer signed on the ForgeBox (step 3a, own protocol).

  .venv/bin/python fbsend.py [--emulator] address
  .venv/bin/python fbsend.py [--emulator] balance
  .venv/bin/python fbsend.py [--emulator] airdrop [SOL]
  .venv/bin/python fbsend.py [--emulator] send TO_ADDRESS SOL [--memo TEXT] [--dry-run]

The host builds the message (SystemProgram transfer, optional memo), the device shows
"SEND x SOL to y", waits for Approve, and returns an Ed25519 signature. The host verifies the
signature against the device's address, then submits to devnet. DEVNET ONLY: the device key is a test key.
"""
import argparse
import base64
import hashlib
import json
import sys
import time
import urllib.request

from solders.hash import Hash
from solders.instruction import AccountMeta, Instruction
from solders.message import Message
from solders.pubkey import Pubkey
from solders.signature import Signature
from solders.system_program import TransferParams, transfer
from solders.transaction import Transaction

from fblink import CMD_FB_SIGN_SOL_MESSAGE, CMD_FB_SOL_ADDRESS, STATUS_NAMES, open_link, transact

RPC = "https://api.devnet.solana.com"
MEMO_PROGRAM = Pubkey.from_string("MemoSq4gqABAXKb96qnH8TysNcWxMyWCqXgDLGmfcHr")
LAMPORTS = 1_000_000_000
APPROVAL_WAIT_MS = 75_000   # device times out at 60 s


def rpc(method, params):
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}).encode()
    req = urllib.request.Request(RPC, body, {"content-type": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as r:
        out = json.load(r)
    if "error" in out:
        raise SystemExit(f"RPC {method} error: {out['error']}")
    return out["result"]


def device_address(link):
    _, status, data = transact(link, CMD_FB_SOL_ADDRESS)
    if status != 0:
        raise SystemExit(f"device: GetSolAddress failed (0x{status:04x}) — step-2+ firmware needed")
    return Pubkey.from_string(json.loads(data)["address"])


def balance(pk):
    return rpc("getBalance", [str(pk), {"commitment": "confirmed"}])["value"]


def wait_confirmed(sig, seconds=60):
    for _ in range(seconds):
        st = rpc("getSignatureStatuses", [[sig]])["value"][0]
        if st and st.get("confirmationStatus") in ("confirmed", "finalized"):
            if st.get("err"):
                raise SystemExit(f"transaction failed on chain: {st['err']}")
            return st["confirmationStatus"]
        time.sleep(1)
    raise SystemExit("not confirmed within 60 s (check the explorer)")


def cmd_send(link, me, args):
    to = Pubkey.from_string(args.to)
    lamports = round(float(args.sol) * LAMPORTS)
    ixs = [transfer(TransferParams(from_pubkey=me, to_pubkey=to, lamports=lamports))]
    if args.memo:
        ixs.append(Instruction(MEMO_PROGRAM, args.memo.encode(), [AccountMeta(me, True, False)]))
    blockhash = Hash.from_string(rpc("getLatestBlockhash", [{"commitment": "confirmed"}])["value"]["blockhash"])
    msg = Message.new_with_blockhash(ixs, me, blockhash)
    raw = bytes(msg)
    print(f"message: {len(raw)} bytes, fingerprint {hashlib.sha512(raw).hexdigest()[:4]}-"
          f"{hashlib.sha512(raw).hexdigest()[4:8]} (compare with the device screen)")
    print("→ approve on the device …")
    _, status, data = transact(link, CMD_FB_SIGN_SOL_MESSAGE, raw, timeout_ms=APPROVAL_WAIT_MS)
    if status != 0:
        reason = json.loads(data).get("payload", data) if data else ""
        raise SystemExit(f"device said {STATUS_NAMES.get(status, hex(status))}: {reason}")
    sig = Signature.from_string(json.loads(data)["signature"])
    if not sig.verify(me, raw):
        raise SystemExit("ERROR: device signature does NOT verify against its address")
    print(f"signature verified: {sig}")
    tx = Transaction.populate(msg, [sig])
    if args.dry_run:
        print("dry run, not sending. tx base64:", base64.b64encode(bytes(tx)).decode())
        return
    sent = rpc("sendTransaction", [base64.b64encode(bytes(tx)).decode(), {"encoding": "base64"}])
    print(f"sent: {sent}")
    print(f"status: {wait_confirmed(sent)}")
    print(f"explorer: https://explorer.solana.com/tx/{sent}?cluster=devnet")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emulator", action="store_true")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("address")
    sub.add_parser("balance")
    a = sub.add_parser("airdrop")
    a.add_argument("sol", nargs="?", default="1")
    s = sub.add_parser("send")
    s.add_argument("to")
    s.add_argument("sol")
    s.add_argument("--memo")
    s.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    link = open_link(emulator=args.emulator)
    me = device_address(link)
    print(f"device: {link.name}\naddress: {me}  (devnet test key)")
    if args.cmd == "balance":
        print(f"balance: {balance(me) / LAMPORTS} SOL")
    elif args.cmd == "airdrop":
        sig = rpc("requestAirdrop", [str(me), round(float(args.sol) * LAMPORTS)])
        print(f"airdrop {sig}: {wait_confirmed(sig)}; balance {balance(me) / LAMPORTS} SOL")
    elif args.cmd == "send":
        cmd_send(link, me, args)


if __name__ == "__main__":
    main()
