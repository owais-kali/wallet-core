#!/bin/bash
#
# Signs a native-segwit BTC transfer with walletconsole and broadcasts it.
# Spends all confirmed UTXOs of the mnemonic's m/84'/0'/0'/0/0 address; change returns to it.
#
# usage: sign-and-send-btc.sh <recipient> <sats> [satPerVbyte]
#
# satPerVbyte may be fractional (up to 3 decimals, e.g. 0.1 or 0.5).
# It defaults to the "half hour" estimate from mempool.space.
# BTC_API overrides the API base (default https://mempool.space/api; Esplora-compatible).
# DRY_RUN=1 signs and prints the raw tx without broadcasting.
# The mnemonic is read from a hidden prompt and is never stored or passed as an argument.

set -euo pipefail

DIR=$(cd "$(dirname "$0")" && pwd)
CONSOLE="$DIR/../../build/walletconsole/walletconsole"
API=${BTC_API:-https://mempool.space/api}

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "usage: $0 <recipient> <sats> [satPerVbyte]" >&2
    exit 1
fi
RECIPIENT=$1
SATS=$2
FEE_RATE=${3:-}

if ! [[ "$SATS" =~ ^[1-9][0-9]*$ ]]; then
    echo "sats must be a positive integer" >&2
    exit 1
fi
if [[ -n "$FEE_RATE" ]] && ! awk -v r="$FEE_RATE" 'BEGIN { exit !(r ~ /^[0-9]+(\.[0-9]{1,3})?$/ && r >= 0.001) }'; then
    echo "satPerVbyte must be a number >= 0.001 with at most 3 decimals" >&2
    exit 1
fi
if [[ ! -x "$CONSOLE" ]]; then
    echo "walletconsole not found at $CONSOLE (build it first)" >&2
    exit 1
fi

if [[ -z "$FEE_RATE" ]]; then
    FEE_JSON=$(curl -sf "$API/v1/fees/recommended") || { echo "FAIL: could not fetch fee estimate" >&2; exit 1; }
    FEE_RATE=$(echo "$FEE_JSON" | sed -n 's/.*"halfHourFee":\([0-9]*\(\.[0-9]*\)\{0,1\}\).*/\1/p')
    if [[ -z "$FEE_RATE" ]] || ! awk -v r="$FEE_RATE" 'BEGIN { exit !(r >= 0.001) }'; then
        echo "FAIL: bad fee estimate: $FEE_JSON" >&2
        exit 1
    fi
fi

read -rs -p "Mnemonic (hidden): " MNEMONIC
echo

SENDER=$(printf 'setMnemonic %s\ncoin bitcoin\naddrDefault\nexit\n' "$MNEMONIC" | "$CONSOLE" 2>&1 | sed -n 's/.*Result:  \(.*\)$/\1/p' | tail -1)
if [[ -z "$SENDER" ]]; then
    echo "FAIL: could not derive the sender address" >&2
    exit 1
fi

UTXO_JSON=$(curl -sf "$API/address/$SENDER/utxo") || { echo "FAIL: could not fetch UTXOs for $SENDER" >&2; exit 1; }
# One "txid:vout:value" per confirmed UTXO (objects are split on the "txid" key; status is nested)
UTXOS=$(echo "$UTXO_JSON" | sed 's/"txid"/\
"txid"/g' | grep '"confirmed":true' \
    | sed -n 's/.*"txid":"\([0-9a-f]*\)","vout":\([0-9]*\).*"value":\([0-9]*\).*/\1:\2:\3/p')
if [[ -z "$UTXOS" ]]; then
    echo "FAIL: no confirmed UTXOs at $SENDER" >&2
    exit 1
fi

BALANCE=0
COUNT=0
while IFS=: read -r _ _ v; do BALANCE=$((BALANCE + v)); COUNT=$((COUNT + 1)); done <<< "$UTXOS"

# Rough fee for the confirmation screen (the console computes the exact one): P2WPKH, ~68 vB per input, 31 vB per output, 11 vB overhead
EST_FEE=$(awk -v n="$COUNT" -v r="$FEE_RATE" 'BEGIN { x = (11 + 68 * n + 31 * 2) * r; printf "%d", (x == int(x)) ? x : int(x) + 1 }')
if (( SATS + EST_FEE > BALANCE )); then
    echo "FAIL: balance $BALANCE sats is not enough for $SATS + ~$EST_FEE fee" >&2
    echo "      (the signer would otherwise silently send the maximum amount instead)" >&2
    exit 1
fi

echo "From:      $SENDER  (balance $BALANCE sats, $COUNT UTXOs)"
echo "To:        $RECIPIENT"
echo "Amount:    $SATS sats ($(awk -v s="$SATS" 'BEGIN { printf "%.8f", s / 100000000 }') BTC)"
echo "Fee rate:  $FEE_RATE sat/vB  (about $EST_FEE sats)"
read -r -p "Sign and broadcast? [y/N] " CONFIRM
if [[ "$CONFIRM" != "y" ]]; then
    echo "Aborted."
    exit 1
fi

# shellcheck disable=SC2086  # UTXOS is intentionally word-split into arguments
RAW=$(printf 'setMnemonic %s\nbtcSign %s %s %s %s\nexit\n' "$MNEMONIC" "$RECIPIENT" "$SATS" "$FEE_RATE" "$(echo $UTXOS)" \
    | "$CONSOLE" 2>&1 | sed -n 's/.*Result:  \(.*\)$/\1/p' | tail -1)
unset MNEMONIC
if [[ -z "$RAW" ]]; then
    echo "FAIL: signing produced no transaction (check recipient address)" >&2
    exit 1
fi

if [[ "${DRY_RUN:-}" == "1" ]]; then
    echo "DRY_RUN, not broadcasting. Raw tx:"
    echo "$RAW"
    exit 0
fi

RESPONSE=$(curl -s -X POST "$API/tx" -H "Content-Type: text/plain" -d "$RAW")
if ! [[ "$RESPONSE" =~ ^[0-9a-f]{64}$ ]]; then
    echo "FAIL: $RESPONSE" >&2
    exit 1
fi
echo "Sent: $RESPONSE"
echo "https://mempool.space/tx/$RESPONSE"
