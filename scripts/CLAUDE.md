# CLAUDE.md (scripts/)

Context for future Claude sessions about the Solana send tooling in this folder and the
`walletconsole` code behind it. The repo-wide guide is `../CLAUDE.md`; this file only covers
what was added for signing/sending Solana transactions. User-facing usage is in `README.md`.

## What exists

Shell scripts (all `bash`, `set -euo pipefail`, JSON parsed with `sed`, no `jq`) plus new
commands in the wallet-core console that do the actual signing.

| File | Purpose |
|---|---|
| `sign-and-send.sh <recipient> <lamports>` | One-shot SOL transfer: prompt mnemonic, show From/To/Amount, confirm, fetch blockhash, sign via console, call `send.sh`. |
| `sign-and-send-token.sh <usdt\|mint> <recipient> <amount>` | One-shot SPL token transfer. Amount is in whole tokens; decimals come from `getTokenSupply`. Read-only RPC checks first, then confirm, then fetch blockhash, sign, send. |
| `send.sh <signedTx>` | `sendTransaction` with `"encoding":"base58"`, `preflightCommitment: processed`. Prints signature + Solscan link, exits 1 on any RPC error. |
| `getLatestBlockhash.sh` | `getLatestBlockhash` (finalized) via Helius. |
| `getBlockHeight.sh` | `getBlockHeight` (processed), still on the public mainnet-beta endpoint. |
| `test.sh [waitSeconds]` | Fetch blockhash, wait (default 5), read block height, pass if blockhash age < 150 blocks. Still on the public endpoint. |
| `clear-clipboard.sh` | `pbcopy </dev/null; pbpaste`. No shebang, not executable; run with `bash`. |

Console code (the real logic): `../walletconsole/lib/CommandExecutor.cpp` and `.h`.

## Console commands added

Dispatch is in `CommandExecutor::executeOne`; **`cmd` is lowercased**, so match `"solanasign"`,
`"solanaata"`, `"solanatokensign"`. `params[0]` is the command itself, so
`checkMinParams(params, n)` means `n` arguments and `params[1]` is the first argument.

- `solanaSign <to> <lamports> <recentBlockhash>`: native SOL transfer.
- `solanaAta <owner> <mint>`: derives the associated token account (ATA). Pure offline.
- `solanaTokenSign <mint> <decimals> <to> <amount> <recentBlockhash> [create]`: SPL transfer.
  `<amount>` is in smallest units. Without `create` it builds `TokenTransfer`; with `create` it
  builds `CreateAndTransferToken` (creates the recipient ATA first, sender pays rent). The create
  instruction is **not idempotent**, so use `create` only when the recipient ATA does not exist
  (the script checks with `getAccountInfo`).

Implementation notes (all in `CommandExecutor.cpp`):
- Shared helpers: `prepareSolanaInput` (checks mnemonic is set, derives sender + private key via
  `_address.addrDefault("solana")` and `_keys.priDP("solana", "")`, sets key/sender/blockhash) and
  `signSolanaInput` (serialize, `TWAnySignerSign(..., TWCoinTypeSolana)`, parse, check
  parse result / `output.error()` / non-empty `encoded`). `parseUint64` rejects non-digits and
  64-bit overflow (`std::stoull` alone throws).
- The signer is invoked through the C API (`TWAnySignerSign`), the same way the tests' `ANY_SIGN`
  macro does. Input proto: `src/proto/Solana.proto`.
- ATA derivation uses the C API (`TWSolanaAddressCreateWithString` +
  `TWSolanaAddressDefaultTokenAddress`) in `deriveSolanaAta`. **Do not** use
  `Solana::Address::defaultTokenAddress`: it is declared in `src/Solana/Address.h` but has no
  definition, which causes a linker error. Only the classic token program is used (Token-2022
  would need `TWSolanaAddressToken2022Address` plus `token_program_id` in the proto).
- `CommandExecutor.h` forward-declares `TW::Solana::Proto::SigningInput` so the header does not
  pull in the generated protobuf header.
- Output convention the scripts rely on: a command's returned value is printed as
  `Result:  <value>` (two spaces). Scripts extract it with `sed -n 's/.*Result:  \(.*\)$/\1/p' | tail -1`.
  Other lines (e.g. `Using derivation path ...`, `Sender token account: ...`) are informational.
- Signed transaction output is **base58** (proto default `tx_encoding`). `send.sh` declares base58 to
  match. Switching to base64 means `input.set_tx_encoding(Solana::Proto::Base64)` AND changing
  `send.sh` to `"encoding":"base64"`.
- Sender key = Solana default path `m/44'/501'/0'` (wallet-core / Trust Wallet). **Phantom and
  Solflare use `m/44'/501'/0'/0'`**, so the same mnemonic can give a different address there. Both
  scripts print the sender before confirmation for this reason.

## Solana facts established in this work (verify before relying on them)

- **Blockhash lifetime is 150 blocks.** Measured block rate on mainnet was about 4.65 blocks/s, so
  the window is **about 32 seconds**, not the 60-90 s often quoted. An earlier answer in this
  project said 60-90 s and was wrong; the measurement is the authority (re-measure with
  `getBlockHeight` twice, 20 s apart).
- Validity check: a blockhash is usable while current block height <= `lastValidBlockHeight`.
  `isBlockhashValid` also works. `lastValidBlockHeight - 150` is the creation height.
- Fees/rent: base fee 5,000 lamports. Rent-exempt minimum for a 0-data system account is
  890,880 lamports (a SOL transfer to a brand-new account below this fails). A new token account
  needs 2,039,280 lamports, held inside the token account (recoverable only by the owner closing the
  empty account). The scripts' `MIN_SOL_CREATE=2100000` is a minimum-balance check, not the deposit.
- USDT mint on Solana: `Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB`, 6 decimals.
- Public endpoint `api.mainnet-beta.solana.com` returned the same `x-rpc-node` header across
  consecutive requests in a sample, so "different backend nodes" was not confirmed as a cause.
- Unit: 1 SOL = 1,000,000,000 lamports.

## Unresolved issue (do not assume it is fixed)

The user repeatedly hit `BlockhashNotFound` when doing the manual flow (fetch blockhash, paste into
console `solanaSign`, paste output into `send.sh`), on both the public endpoint and Helius, and
reported doing it within about 15 s. What was verified:

- The signed transaction contained the correct blockhash (decoded the message: signatures, 3-byte
  header, account list, then the 32-byte blockhash).
- In a single-shell run (fetch, sign, `simulateTransaction`) the blockhash was accepted; simulation
  failed later with `InvalidAccountForFee`, so the pipeline itself works.
- Same-node and Helius-vs-public were ruled out as causes.

Most likely cause is human latency pushing past the 32 s window, but this was **never confirmed with
a real broadcast**. The one-shot scripts (`sign-and-send*.sh`) were written to remove that latency.
If the user still sees it, measure timestamps per step and check `isBlockhashValid` on the exact
blockhash right before sending.

## Environment / build gotchas (macOS, Apple Silicon)

- The C++ build needs generated files. `src/rust/bindgen/WalletCoreRSBindgen.h` is produced by
  `cbindgen` and is gitignored. Run `tools/generate-files native` (also produces `src/proto/*.pb.h`
  and `TWHRP.h`).
- **`tools/generate-files` needs a nightly rustc first on PATH**: its `rust-bindgen` step exports
  `RUSTFLAGS=-Zlocation-detail=none`. The default `rustc` is Homebrew stable (1.99) and fails with
  "the option `Z` is only accepted on the nightly compiler". rustup toolchains exist but there are no
  rustup proxies in `~/.cargo/bin`, so put the toolchain dir on PATH explicitly:
  `export PATH="$HOME/.rustup/toolchains/nightly-2025-12-11-aarch64-apple-darwin/bin:$HOME/.cargo/bin:$PATH"`.
- `cbindgen` was installed with `cargo install cbindgen` into `~/.cargo/bin` (not on PATH by default).
- `rust/Cargo.lock` pins `ethnum` at 1.5.3; 1.4.0 does not compile on current rustc.
- Build the console: `cd build && cmake .. && make -j8 walletconsole`. Binary:
  `build/walletconsole/walletconsole` (note the extra directory). Linker warnings about macOS 15.5 vs
  11.0 and duplicate libraries are harmless. Third-party protobuf `-Wunused-parameter` warnings are noise.
- Console reads commands from stdin, so it is scriptable: `printf 'cmd1\ncmd2\nexit\n' | walletconsole`.
  The startup active coin is `merlin`/btc, so scripts run `coin solana` before `addrDefault`.
- macOS has no `timeout` command.

## Testing without spending money

- Never call `sendTransaction` with real funds unless the user explicitly approves that run.
- Use `simulateTransaction` with `{"encoding":"base58","sigVerify":false}`; it does not broadcast.
- The mnemonic `abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about`
  is public, and its Solana address (`GjJyeC1r2RgkuoCWMyPYkCWSGSGLcz266EaAkLA27AhL`) holds real SOL. Use it
  only for simulation and for read-only script paths. Expect `InvalidAccountForFee` from simulation with it,
  and "no token account" from the token script.
- ATA derivation test vector (from `tests/chains/Solana/TWSolanaAddressTests.cpp`): owner
  `B1iGmDJdvmxyUiYM8UEo2Uw2D58EmUrw4KyLYMmrhf8V`, mint `SRMuApVNdxXokk5GT7XD5cUUgXMBCoAz2LHeuAoKWRt`
  gives `EDNd1ycsydWYwVmrYZvqYazFqwk1QjBgAUKFjBoz1jKP`.
- To inspect a signed legacy transaction in Python (use `python3 -I`): base58-decode it, then
  `offset = 1 + 64*sigCount + 3`, read the account count byte, `offset += 1 + 32*count`, and the next
  32 bytes are the blockhash.

## Script conventions

- RPC selection: `RPC_URL` if set, else `https://mainnet.helius-rpc.com/?api-key=$HELIUS_API_KEY`
  (aborts with a message if neither is set). Never hardcode an API key.
- The mnemonic is read with `read -rs` into a shell variable, piped to the console on stdin, and
  `unset` after use. Never accept it as an argument or write it to disk.
- All prompts and read-only checks come **before** fetching the blockhash; after the user confirms,
  fetch, sign, send immediately. Keep this ordering when editing.

## Known gaps and decisions left open

- `walletconsole_cheatsheet.txt` (untracked, repo root) still says there is no signing command.
- `getBlockHeight.sh` and `test.sh` still use the public mainnet-beta endpoint (they are read-only
  and not on the send path). The user was asked and has not requested switching them.
- `test.sh` measures blockhash age as `heightAfterWait - (lastValidBlockHeight - 150)`; an earlier version
  that read the height before the wait produced always-negative values and could never fail.
- Not supported: Token-2022 mints, memo, priority fees / compute budget, durable nonces, external fee
  payer, versioned (v0) messages, `getSignatureStatuses` confirmation polling after send.
- The base64 encoding option and a console-side sender printout were suggested in review but not applied
  to the console (the scripts print the sender instead).
- Git state when this was written: the new scripts and the two `walletconsole/lib` files were staged;
  `scripts/README.md` and `scripts/CLAUDE.md` untracked. No commit was made by Claude.
