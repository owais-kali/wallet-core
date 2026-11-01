# Solana send scripts

Shell scripts for signing and sending Solana transactions with wallet-core's `walletconsole`:

- send **SOL**
- send **USDT** (or any classic SPL token) on Solana
- look up chain data (block height, blockhash) and sanity-check blockhash timing

> **These scripts use Solana mainnet and move real funds.** Start with a very small amount.

## Prerequisites

1. **A built `walletconsole`** at `build/walletconsole/walletconsole`. If it is missing:

   ```bash
   cd build
   make -j8 walletconsole
   ```

   For a first-time setup (generated files, Rust library), see the repository root `CLAUDE.md`.

2. **A Helius API key**, exported in the shell you run the scripts from:

   ```bash
   export HELIUS_API_KEY=your-key
   ```

   To use a different RPC endpoint instead, set `RPC_URL` (it takes precedence over `HELIUS_API_KEY`):

   ```bash
   export RPC_URL="https://your-rpc-endpoint"
   ```

3. **Your mnemonic.** The send scripts ask for it with a hidden prompt. It is never passed as an argument, stored, or written to a file.

Run the commands below from this `scripts/` folder.

## Send SOL

```bash
./sign-and-send.sh <recipient> <lamports>
```

Example: send 0.01 SOL.

```bash
./sign-and-send.sh 44wmLnctzjwcR1qVH83TeCxDtpd9avE6MBqiydhT1sdE 10000000
```

- `<recipient>` is the recipient's Solana address (their public address).
- `<lamports>` is the amount in lamports, as a whole number. **1 SOL = 1,000,000,000 lamports**, so 0.01 SOL is `10000000`.

What happens:

1. You enter the mnemonic (hidden).
2. The script prints the sender, recipient and amount, and asks `Sign and broadcast? [y/N]`.
3. After you answer `y`, it fetches a fresh blockhash, signs, and sends right away.
4. On success it prints the transaction signature and a Solscan link.

Things to know:

- The sender needs the amount plus about 5,000 lamports for the fee.
- Sending to an address that has never held SOL needs at least about **890,880 lamports** (0.00089 SOL), the rent-exempt minimum, or the transaction fails.

## Send USDT (or another token) on Solana

```bash
./sign-and-send-token.sh <usdt|mint> <recipient> <amount>
```

Example: send 0.1 USDT.

```bash
./sign-and-send-token.sh usdt HJwuLHa1j9QuBjnpEngvZX2rtubWgSvj2KC24MkGiMf6 0.1
```

- `<usdt|mint>`: `usdt` is a shortcut for USDT's mint address on Solana (`Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB`). For any other token, such as USDC, pass its mint address instead.
- `<recipient>`: the recipient's normal wallet address, **not** their token account.
- `<amount>`: in **whole tokens**, e.g. `0.1` or `12.5`. The script converts it using the token's decimals (USDT has 6, so `0.1` becomes `100000` smallest units). It rejects amounts with too many decimals.

Before it asks you to confirm, the script checks over RPC that:

- the sender has a token account for that token and enough of it
- the sender has enough SOL for the fee
- whether the recipient already has a token account for that token

If the recipient has **no** token account, the script creates it as part of the transfer and tells you so. In that case the **sender pays a one-time deposit of about 0.00204 SOL**, and the sender needs about 2,100,000 lamports in total. The deposit stays locked in the new token account; only the recipient can reclaim it, by closing that (empty) account. If the recipient already has one, you only pay the normal fee (about 0.000005 SOL).

Limits: only the classic SPL token program is supported (USDT, USDC). Token-2022 mints are not.

## Why the blockhash matters (about 32 seconds)

Every Solana transaction includes a recent blockhash, and the network accepts a blockhash for only about 150 blocks. At roughly 4.65 blocks per second that is **about 32 seconds**. After that you get:

```
BlockhashNotFound
```

`sign-and-send.sh` and `sign-and-send-token.sh` avoid this by asking all their questions first and fetching the blockhash only after you confirm, then signing and sending in about a second. Copying values between terminals by hand usually takes longer than 32 seconds, which is why the one-shot scripts are recommended.

## Which address is mine?

The sender address is the one from `walletconsole`:

```
coin solana
addrDefault
```

This uses the derivation path `m/44'/501'/0'`. **Phantom and Solflare use `m/44'/501'/0'/0'`**, so the same mnemonic can give a different address there. Both send scripts print the sender address before you confirm. Check that it is the address holding your funds.

To receive, give people that same address for both SOL and USDT. Your USDT is held in a separate token account derived from it, which you can display in the console with:

```
solanaAta <your address> <mint>
```

## Manual flow (advanced)

You can do each step yourself, but you must finish within about 32 seconds of fetching the blockhash.

```bash
./getLatestBlockhash.sh          # copy result.value.blockhash
```

Then in `walletconsole` (`build/walletconsole/walletconsole`):

```
setMnemonic <your words>
solanaSign <recipient> <lamports> <blockhash>
solanaTokenSign <mint> <decimals> <recipient> <amount> <blockhash> [create]
```

For `solanaTokenSign`, `<amount>` is in the token's smallest unit (for USDT, 1 USDT is `1000000`), and `create` is only needed when the recipient has no token account yet. Copy the base58 string printed after `Result:` (not the `Result:` label) and send it:

```bash
./send.sh <signedTx>
```

## Script reference

| Script | What it does |
|---|---|
| `sign-and-send.sh <recipient> <lamports>` | Signs and sends a SOL transfer in one run. |
| `sign-and-send-token.sh <usdt\|mint> <recipient> <amount>` | Signs and sends an SPL token (e.g. USDT) transfer in one run, with balance and account checks. |
| `send.sh <signedTx>` | Broadcasts an already signed transaction (base58) with `sendTransaction`. |
| `getLatestBlockhash.sh` | Prints the latest finalized blockhash and its `lastValidBlockHeight` (uses Helius). |
| `getBlockHeight.sh` | Prints the current block height (uses the public mainnet-beta endpoint). |
| `test.sh [waitSeconds]` | Fetches a blockhash, waits (default 5 seconds), and passes if the blockhash is less than 150 blocks old. Uses the public mainnet-beta endpoint. |
| `clear-clipboard.sh` | Empties the macOS clipboard, e.g. after copying a mnemonic. Run with `bash clear-clipboard.sh`. |

## Troubleshooting

| Message | Cause and fix |
|---|---|
| `command not found` | Run from the `scripts/` folder and start the command with `./`. |
| `Set HELIUS_API_KEY` | Export your key (or `RPC_URL`) in the same shell. |
| `walletconsole not found` | Build it: `cd build && make -j8 walletconsole`. |
| `BlockhashNotFound` | The blockhash expired. Use the one-shot scripts, or redo the manual flow faster. |
| `has no token account for this mint` | The sending wallet holds none of that token. Check that the sender address matches the wallet you funded (see "Which address is mine?"). |
| `token balance too low` / `sender needs about ... lamports` | Top up the token balance or the sender's SOL. |
| Rent or insufficient-funds error on a SOL send | The recipient is a new account and the amount is below about 890,880 lamports. |

## Safety

- Never use a mnemonic you have shared or published. The well-known `abandon abandon ... about` test mnemonic controls real, publicly known addresses.
- The mnemonic is read from a hidden prompt, but if you copied it to paste it in, it stays in your clipboard. Run `bash clear-clipboard.sh` afterwards.
- Check the recipient address carefully. A blockchain transaction can't be undone.
