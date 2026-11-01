#!/bin/bash
#
# Signs an SPL token transfer (e.g. USDT) with walletconsole and broadcasts it, in one run.
# A blockhash is only valid for ~32 seconds, so it is fetched after all
# checks and confirmation prompts, then signed and sent immediately.
#
# usage: sign-and-send-token.sh <usdt|mint> <recipient> <amount>
#   <usdt|mint>  "usdt" or a token mint address
#   <recipient>  recipient's main (wallet) address, not a token account
#   <amount>     in whole tokens, e.g. 12.5
#
# Needs HELIUS_API_KEY (or RPC_URL) set, and a built walletconsole.
# The mnemonic is read from a hidden prompt and is never stored or passed as an argument.
# Uses the classic SPL token program (USDT, USDC); Token-2022 mints are not supported.

set -euo pipefail

DIR=$(cd "$(dirname "$0")" && pwd)
CONSOLE="$DIR/../build/walletconsole/walletconsole"
USDT_MINT="Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB"
# Approximate lamports the sender needs: fee only, or fee + rent for a new recipient token account
MIN_SOL_PLAIN=10000
MIN_SOL_CREATE=2100000

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <usdt|mint> <recipient> <amount>" >&2
    exit 1
fi
TOKEN=$1
RECIPIENT=$2
AMOUNT=$3

MINT=$TOKEN
[[ "$TOKEN" == "usdt" ]] && MINT=$USDT_MINT

if [[ ! -x "$CONSOLE" ]]; then
    echo "walletconsole not found at $CONSOLE (build it first)" >&2
    exit 1
fi

if [[ -z "${RPC_URL:-}" ]]; then
    : "${HELIUS_API_KEY:?Set HELIUS_API_KEY, e.g. export HELIUS_API_KEY=your-key}"
    RPC_URL="https://mainnet.helius-rpc.com/?api-key=$HELIUS_API_KEY"
fi
export RPC_URL

rpc() {
    curl -s "$RPC_URL" -X POST -H "Content-Type: application/json" -d "$1"
}

# Run console commands (one per line on stdin) and print the last "Result:" value
console_result() {
    "$CONSOLE" 2>&1 | sed -n 's/.*Result:  \(.*\)$/\1/p' | tail -1
}

# whole-token amount -> smallest units, given decimals
to_base_units() {
    local amt=$1 dec=$2 int frac out
    [[ "$amt" =~ ^[0-9]+(\.[0-9]+)?$ ]] || return 1
    int=${amt%%.*}
    frac=""
    [[ "$amt" == *.* ]] && frac=${amt#*.}
    (( ${#frac} <= dec )) || return 1
    while (( ${#frac} < dec )); do frac+="0"; done
    out=$(echo "$int$frac" | sed 's/^0*//')
    echo "${out:-0}"
}

# --- Read-only lookups ---------------------------------------------------------
SUPPLY_JSON=$(rpc "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getTokenSupply\",\"params\":[\"$MINT\"]}")
DECIMALS=$(echo "$SUPPLY_JSON" | sed -n 's/.*"decimals":\([0-9]*\).*/\1/p')
if [[ -z "$DECIMALS" ]]; then
    echo "FAIL: $MINT is not a valid token mint: $SUPPLY_JSON" >&2
    exit 1
fi

if ! BASE_UNITS=$(to_base_units "$AMOUNT" "$DECIMALS") || (( ${#BASE_UNITS} > 18 )); then
    echo "FAIL: amount must be a number with at most $DECIMALS decimals (and a sane size)" >&2
    exit 1
fi

read -rs -p "Mnemonic (hidden): " MNEMONIC
echo

# Sender address, recipient token account
SENDER=$(printf 'setMnemonic %s\ncoin solana\naddrDefault\nexit\n' "$MNEMONIC" | console_result)
if [[ -z "$SENDER" ]]; then
    echo "FAIL: could not derive the sender address" >&2
    exit 1
fi
SENDER_ATA=$(printf 'solanaAta %s %s\nexit\n' "$SENDER" "$MINT" | console_result)
RECIPIENT_ATA=$(printf 'solanaAta %s %s\nexit\n' "$RECIPIENT" "$MINT" | console_result)
if [[ -z "$SENDER_ATA" || -z "$RECIPIENT_ATA" ]]; then
    echo "FAIL: could not derive token accounts (check the recipient address)" >&2
    exit 1
fi

# Sender token balance
BAL_JSON=$(rpc "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getTokenAccountBalance\",\"params\":[\"$SENDER_ATA\"]}")
SENDER_TOKENS=$(echo "$BAL_JSON" | sed -n 's/.*"amount":"\([0-9]*\)".*/\1/p')
if [[ -z "$SENDER_TOKENS" ]]; then
    echo "FAIL: $SENDER has no token account for this mint ($SENDER_ATA): $BAL_JSON" >&2
    exit 1
fi
if (( SENDER_TOKENS < BASE_UNITS )); then
    echo "FAIL: token balance too low ($SENDER_TOKENS < $BASE_UNITS, in smallest units)" >&2
    exit 1
fi

# Does the recipient already have a token account?
INFO_JSON=$(rpc "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getAccountInfo\",\"params\":[\"$RECIPIENT_ATA\",{\"encoding\":\"base64\"}]}")
CREATE=""
MIN_SOL=$MIN_SOL_PLAIN
if echo "$INFO_JSON" | grep -q '"value":null'; then
    CREATE="create"
    MIN_SOL=$MIN_SOL_CREATE
fi

# Sender SOL for fees (and rent, if the recipient token account must be created)
SOL_JSON=$(rpc "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getBalance\",\"params\":[\"$SENDER\"]}")
SENDER_SOL=$(echo "$SOL_JSON" | sed -n 's/.*"value":\([0-9]*\).*/\1/p')
if [[ -z "$SENDER_SOL" ]] || (( SENDER_SOL < MIN_SOL )); then
    echo "FAIL: sender needs about $MIN_SOL lamports of SOL for fees${CREATE:+ and the new token account}, has ${SENDER_SOL:-unknown}" >&2
    exit 1
fi

echo "From:        $SENDER"
echo "To:          $RECIPIENT"
echo "Token:       $TOKEN ($MINT), $DECIMALS decimals"
echo "Amount:      $AMOUNT ($BASE_UNITS smallest units)"
echo "Your balance: $SENDER_TOKENS smallest units"
if [[ -n "$CREATE" ]]; then
    echo "NOTE: the recipient has no token account. It will be created and YOU pay its rent (~0.00204 SOL)."
fi
read -r -p "Sign and broadcast? [y/N] " CONFIRM
if [[ "$CONFIRM" != "y" ]]; then
    echo "Aborted."
    exit 1
fi

# From here on, speed matters: the blockhash expires in ~32 seconds.
BH_JSON=$(rpc '{"jsonrpc":"2.0","id":1,"method":"getLatestBlockhash","params":[{"commitment":"finalized"}]}')
BLOCKHASH=$(echo "$BH_JSON" | sed -n 's/.*"blockhash":"\([^"]*\)".*/\1/p')
if [[ -z "$BLOCKHASH" ]]; then
    echo "FAIL: could not fetch blockhash: $BH_JSON" >&2
    exit 1
fi

SIGNED=$(printf 'setMnemonic %s\nsolanaTokenSign %s %s %s %s %s %s\nexit\n' \
    "$MNEMONIC" "$MINT" "$DECIMALS" "$RECIPIENT" "$BASE_UNITS" "$BLOCKHASH" "$CREATE" | console_result)
unset MNEMONIC
if [[ -z "$SIGNED" ]]; then
    echo "FAIL: signing produced no transaction" >&2
    exit 1
fi

"$DIR/send.sh" "$SIGNED"
