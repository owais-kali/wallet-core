#!/bin/bash
#
# Broadcasts a signed Solana transaction produced by walletconsole's solanaSign.
#
# usage: send.sh <signedTx>
#   <signedTx>  base58 string printed by solanaSign
#
# Uses Helius mainnet; set HELIUS_API_KEY. Set RPC_URL to override the endpoint.

set -euo pipefail

if [[ -z "${RPC_URL:-}" ]]; then
    : "${HELIUS_API_KEY:?Set HELIUS_API_KEY, e.g. export HELIUS_API_KEY=your-key}"
    RPC_URL="https://mainnet.helius-rpc.com/?api-key=$HELIUS_API_KEY"
fi

if [[ $# -ne 1 || -z "$1" ]]; then
    echo "usage: $0 <signedTx>" >&2
    exit 1
fi
SIGNED_TX=$1

REQUEST=$(printf '{"jsonrpc":"2.0","id":1,"method":"sendTransaction","params":["%s",{"encoding":"base58","preflightCommitment":"processed"}]}' "$SIGNED_TX")

RESPONSE=$(curl -s "$RPC_URL" -X POST -H "Content-Type: application/json" -d "$REQUEST")

if echo "$RESPONSE" | grep -q '"error"'; then
    echo "FAIL: $RESPONSE" >&2
    exit 1
fi

SIGNATURE=$(echo "$RESPONSE" | sed -n 's/.*"result":"\([^"]*\)".*/\1/p')
if [[ -z "$SIGNATURE" ]]; then
    echo "FAIL: unexpected response: $RESPONSE" >&2
    exit 1
fi

echo "Sent. Signature: $SIGNATURE"
echo "Check status: https://solscan.io/tx/$SIGNATURE"
