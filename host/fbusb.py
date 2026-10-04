#!/usr/bin/env python3
"""ForgeBox USB link test: EchoTest + GetDeviceInfo over Keystone's EAPDU protocol.

Framing follows agave remote-wallet/src/keystone.rs (write()/read()):
  request  packet = CLA(1)=0 | INS(2) | P1 total(2) | P2 index(2) | LC request id(2) | data(<=55)
  response packet = same 9-byte header | data | status(2), all big-endian.

Usage:  .venv/bin/python fbusb.py [--emulator] [echo TEXT | info | addr | all]     (default: all)
  addr needs the step-2 firmware; "all" skips it gracefully on step-1 firmware.
Needs read/write access to 1209:3001 (udev rule or sudo).
"""
import json
import sys

from fblink import CMD_ECHO, CMD_FB_SOL_ADDRESS, CMD_INFO, open_link, transact

EXPECTED_ADDRESS = "Kti8hwMYH8iBLJNJhRqiWf8ttCcuHEw54o4FvD3RfLz"   # test mnemonic, m/44'/501'/0'/0'


def main():
    args = [a for a in sys.argv[1:] if a != "--emulator"]
    link = open_link(emulator="--emulator" in sys.argv)
    print(f"device: {link.name}")
    what = args[0] if args else "all"
    ok = True
    if what in ("echo", "all"):
        text = (args[1] if what == "echo" and len(args) > 1 else "hello forgebox 🦞").encode()
        cmd, status, data = transact(link, CMD_ECHO, text)
        good = status == 0 and data == text
        ok &= good
        print(f"EchoTest: status=0x{status:04x} reply={data!r} -> {'OK' if good else 'MISMATCH'}")
    if what in ("info", "all"):
        cmd, status, data = transact(link, CMD_INFO)
        print(f"GetDeviceInfo: status=0x{status:04x} reply={data.decode(errors='replace')}")
        try:
            info = json.loads(data)
            ok &= status == 0 and "firmwareVersion" in info
        except ValueError:
            ok = False
    if what in ("addr", "all"):
        cmd, status, data = transact(link, CMD_FB_SOL_ADDRESS)
        if status == 0x0006 and what == "all":
            print("GetSolAddress: not in this firmware (step 1 build), skipped")
        else:
            try:
                addr = json.loads(data).get("address", "")
            except ValueError:
                addr = ""
            good = status == 0 and addr == EXPECTED_ADDRESS
            ok &= good
            print(f"GetSolAddress: status=0x{status:04x} address={addr or data!r} -> "
                  f"{'matches solana-keygen' if good else 'MISMATCH, expected ' + EXPECTED_ADDRESS}")
    print("OK" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
