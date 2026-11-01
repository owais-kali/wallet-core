#!/bin/bash
#
# Checks how many blocks old a Solana blockhash is.
#   1. Fetch a blockhash (getLatestBlockhash). Its lastValidBlockHeight is
#      creation height + 150, so creation height = lastValidBlockHeight - 150.
#   2. Wait WAIT_SECONDS.
#   3. Read the current block height (getBlockHeight).
#   4. value = current block height - creation height. Pass if value < 150.

set -euo pipefail

RPC_URL="https://api.mainnet-beta.solana.com"
MAX_AGE=150
WAIT_SECONDS=${1:-5}

rpc() {
    curl -s "$RPC_URL" -X POST -H "Content-Type: application/json" -d "$1"
}

# 1. Latest blockhash
BLOCKHASH_JSON=$(rpc '{"jsonrpc":"2.0","id":1,"method":"getLatestBlockhash","params":[{"commitment":"finalized"}]}')
LAST_VALID=$(echo "$BLOCKHASH_JSON" | sed -n 's/.*"lastValidBlockHeight":\([0-9]*\).*/\1/p')

if [[ -z "$LAST_VALID" ]]; then
    echo "FAIL: could not read lastValidBlockHeight: $BLOCKHASH_JSON"
    exit 1
fi
echo "lastValidBlockHeight: $LAST_VALID"

# 2. Wait
sleep "$WAIT_SECONDS"

# 3. Current block height, read after the wait so the blockhash age is measured at the time of use
HEIGHT_JSON=$(rpc '{"jsonrpc":"2.0","id":1,"method":"getBlockHeight","params":[{"commitment":"processed"}]}')
BLOCK_HEIGHT=$(echo "$HEIGHT_JSON" | sed -n 's/.*"result":\([0-9]*\).*/\1/p')

if [[ -z "$BLOCK_HEIGHT" ]]; then
    echo "FAIL: could not read block height: $HEIGHT_JSON"
    exit 1
fi
echo "Block height after ${WAIT_SECONDS}s: $BLOCK_HEIGHT"

# 4. Assert
CREATION_HEIGHT=$((LAST_VALID - MAX_AGE))
VALUE=$((BLOCK_HEIGHT - CREATION_HEIGHT))
echo "Blockhash age (blocks): $VALUE"

if (( VALUE < MAX_AGE )); then
    echo "PASS: $VALUE < $MAX_AGE"
else
    echo "FAIL: $VALUE >= $MAX_AGE"
    exit 1
fi
