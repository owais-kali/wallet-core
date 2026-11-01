#!/bin/bash
: "${HELIUS_API_KEY:?Set HELIUS_API_KEY, e.g. export HELIUS_API_KEY=your-key}"
curl "https://mainnet.helius-rpc.com/?api-key=$HELIUS_API_KEY" -X POST -H "Content-Type: application/json" -d '{"jsonrpc":"2.0","id":1,"method":"getLatestBlockhash","params":[{"commitment":"finalized"}]}'
