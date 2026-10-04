#!/bin/sh
# Host-side test of src/usb/eapdu.c + eapdu_framing.c with stubbed USB/RTOS.
set -e
cd "$(dirname "$0")"
gcc -Wall -I inc -I ../../src/usb -I ../../src/wallet -I ../../external/monocypher t.c ../../src/usb/eapdu.c ../../src/usb/eapdu_framing.c \
    ../../src/wallet/sol_key.c ../../external/monocypher/monocypher.c ../../external/monocypher/monocypher-ed25519.c -o /tmp/eapdu_hosttest
/tmp/eapdu_hosttest
