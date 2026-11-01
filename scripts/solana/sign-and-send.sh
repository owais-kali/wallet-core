#!/bin/bash
#
# Signs a SOL transfer with walletconsole and broadcasts it, in one run.
# A blockhash is only valid for ~32 seconds, so it is fetched after all
# confirmation prompts and signed/sent immediately.
#
# usage: sign-and-send.sh <recipient> <lamports>
#
# Needs HELIUS_API_KEY (or RPC_URL) set, and a built walletconsole.
# The mnemonic is read from a hidden prompt and is never stored or passed as an argument.

set -euo pipefail

DIR=$(cd "$(dirname "$0")" && pwd)
CONSOLE="$DIR/../../build/walletconsole/walletconsole"

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <recipient> <lamports>" >&2
    exit 1
fi
RECIPIENT=$1
LAMPORTS=$2

if ! [[ "$LAMPORTS" =~ ^[0-9]+$ ]]; then
    echo "lamports must be a non-negative integer" >&2
    exit 1
fi
if [[ ! -x "$CONSOLE" ]]; then
    echo "walletconsole not found at $CONSOLE (build it first)" >&2
    exit 1
fi

if [[ -z "${RPC_URL:-}" ]]; then
    : "${HELIUS_API_KEY:?Set HELIUS_API_KEY, e.g. export HELIUS_API_KEY=your-key}"
    RPC_URL="https://mainnet.helius-rpc.com/?api-key=$HELIUS_API_KEY"
fi
export RPC_URL

read -rs -p "Mnemonic (hidden): " MNEMONIC
echo

# Show who will pay, before anything is signed
SENDER=$(printf 'setMnemonic %s\ncoin solana\naddrDefault\nexit\n' "$MNEMONIC" | "$CONSOLE" 2>&1 | sed -n 's/.*Result:  \(.*\)$/\1/p' | tail -1)
if [[ -z "$SENDER" ]]; then
    echo "FAIL: could not derive the sender address" >&2
    exit 1
fi

echo "From:      $SENDER"
echo "To:        $RECIPIENT"
echo "Lamports:  $LAMPORTS ($(awk -v l="$LAMPORTS" 'BEGIN { printf "%.9f", l / 1000000000 }') SOL)"
read -r -p "Sign and broadcast? [y/N] " CONFIRM
if [[ "$CONFIRM" != "y" ]]; then
    echo "Aborted."
    exit 1
fi

# From here on, speed matters: the blockhash expires in ~32 seconds.
BH_JSON=$(curl -s "$RPC_URL" -X POST -H "Content-Type: application/json" -d '{"jsonrpc":"2.0","id":1,"method":"getLatestBlockhash","params":[{"commitment":"finalized"}]}')
BLOCKHASH=$(echo "$BH_JSON" | sed -n 's/.*"blockhash":"\([^"]*\)".*/\1/p')
if [[ -z "$BLOCKHASH" ]]; then
    echo "FAIL: could not fetch blockhash: $BH_JSON" >&2
    exit 1
fi

SIGNED=$(printf 'setMnemonic %s\nsolanaSign %s %s %s\nexit\n' "$MNEMONIC" "$RECIPIENT" "$LAMPORTS" "$BLOCKHASH" \
    | "$CONSOLE" 2>&1 | sed -n 's/.*Result:  \(.*\)$/\1/p' | tail -1)
unset MNEMONIC
if [[ -z "$SIGNED" ]]; then
    echo "FAIL: signing produced no transaction (check recipient address)" >&2
    exit 1
fi

"$DIR/send.sh" "$SIGNED"
