#!/bin/sh
# Fuzz src/wallet/sol_tx.c under ASan/UBSan (random + mutated messages).
set -e
cd "$(dirname "$0")"
gcc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -I ../../src/wallet -I ../../external/monocypher \
    fuzz.c ../../src/wallet/sol_tx.c ../../src/wallet/sol_key.c ../../external/monocypher/monocypher.c \
    ../../external/monocypher/monocypher-ed25519.c -o /tmp/fuzz_tx
/tmp/fuzz_tx "${1:-100000}"
