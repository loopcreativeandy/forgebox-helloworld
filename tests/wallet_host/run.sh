#!/bin/sh
# Host test of src/wallet/sol_key.c: BIP39 + SLIP-10 vectors, address vs solana-keygen, sign/verify.
set -e
cd "$(dirname "$0")"
gcc -O2 -Wall -I ../../src/wallet -I ../../external/monocypher t.c ../../src/wallet/sol_key.c \
    ../../external/monocypher/monocypher.c ../../external/monocypher/monocypher-ed25519.c -o /tmp/wallet_hosttest
/tmp/wallet_hosttest
