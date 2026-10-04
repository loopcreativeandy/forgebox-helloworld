#!/bin/sh
# Stock-solana-CLI protocol check against the emulator (needs tests/emulator/build.sh first).
#   ./run.sh pubkey "m/44'/501'"            ./run.sh sign "m/44'/501'" <message hex>
set -e
cd "$(dirname "$0")"
PATH="$HOME/.cargo/bin:$PATH" cargo build --offline -q 2>/dev/null
exec ./target/debug/clicheck "$@"
