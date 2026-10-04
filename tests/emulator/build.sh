#!/bin/sh
# Build the host emulator → /tmp/fb_emu (used by forgebox/host/fbsend.py --emulator)
set -e
cd "$(dirname "$0")"
S=../../src
gcc -O2 -Wall -I inc -I $S/usb -I $S/wallet -I ../../external/monocypher emu.c approval_stub.c \
    $S/usb/eapdu.c $S/usb/eapdu_framing.c $S/wallet/sol_key.c $S/wallet/sol_tx.c \
    ../../external/monocypher/monocypher.c ../../external/monocypher/monocypher-ed25519.c -o "${1:-/tmp/fb_emu}"
echo "built ${1:-/tmp/fb_emu}"
