// SPDX-License-Identifier: Apache-2.0
//
// Copyright © 2017 Trust Wallet.

#include "CommandExecutor.h"
#include "WalletConsole.h"
#include "Keys.h"
#include "Coins.h"
#include "Util.h"
#include "Address.h"

#include "Base64.h"
#include "HexCoding.h"
#include "Data.h"

#include "proto/Common.pb.h"
#include "proto/Solana.pb.h"
#include "Solana/Address.h"

#include <TrustWalletCore/TWAnySigner.h>
#include <TrustWalletCore/TWCoinType.h>
#include <TrustWalletCore/TWData.h>
#include <TrustWalletCore/TWSolanaAddress.h>
#include <TrustWalletCore/TWString.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <vector>
#include <cassert>

namespace TW::WalletConsole {

using namespace std;
using namespace TW;


CommandExecutor::CommandExecutor(ostream& out)
    : 
    _out(out),
    _coins(out), 
    _buffer(out),
    _keys(out, _coins),
    _address(out, _coins, _keys),
    _util(out)
{
}

void CommandExecutor::init() {
    _coins.init();
    setCoin("btc", true);
}

void CommandExecutor::help() const {
    _out << "Commands:" << endl;
    _out << "  exit                    Exit" << endl;
    _out << "  quit                    Exit" << endl;
    _out << "  help                    This help" << endl;
    _out << "Inputs, buffer:" << endl;
    _out << "  #                       Take last result" << endl;
    _out << "  #<n>                    Take nth previous result" << endl;
    _out << "  buffer                  Take buffer values" << endl;
    _out << "Coins:" << endl;
    _out << "  coins                   List known coins" << endl;
    _out << "  coin <coin>             Set active coin, selected by its ID or symbol or name" << endl;
    _out << "Keys:" << endl;
    _out << "  newKey                  Create new pseudo-random 32-byte key (secret!)" << endl;
    _out << "  pubPri <priKey>         Derive public key from a secret private key (type is coin-dependent)" << endl;
    _out << "  priPub <pubKey>         Derive private key from public key :)" << endl;
    _out << "  setMnemonic <word1> ... Set current mnemonic, several words (secret!)" << endl;
    _out << "  newMnemonic <strength>  Create and store a new mnemonic, of strength (128 -- 256) (secret!)" << endl;
    _out << "  dumpSeed                Dump the seed of the current mnemonic (secret!)" << endl;
    _out << "  dumpMnemonic            Dump the current mnemonic (secret!)" << endl;
    _out << "  dumpDP                  Dump the default derivation path of the current coin (ex.: m/84'/0'/0'/0/0)" << endl;
    _out << "  dumpXpub                Dump the XPUB of the current mnemonic" << endl;
    _out << "  priDP [<derivPath>]     Derive a new private key for the coin, from the current mnemonic and given derivation path." << endl;
    _out << "                          If derivation path is missing, the default one is used (see dumpDP)." << endl;
    _out << "Addresses:" << endl;
    _out << "  addrPub <pubKey>        Create <coin> address from public key" << endl;
    _out << "  addrPri <priKey>        Create <coin> address from private key" << endl;
    _out << "  addr <addr>             Check string <coin> address" << endl;
    _out << "  addrDefault             Derive default address, for current coin, fom current mnemonic; see dumpDP" << endl;
    _out << "  addrDP <derivPath>      Derive a new address with the given derivation path (using current coin and mnemonic)" << endl;
    _out << "  addrXpub <xpub> <index> Derive a new address from the given XPUB and address index (using current coin)" << endl;
    _out << "Coin-specific methods:" << endl;
    _out << "  solanaSign <to> <lamports> <recentBlockhash>  Sign a SOL transfer from the current mnemonic (Solana key); returns signed tx (base58)" << endl;
    _out << "  solanaAta <owner> <mint>  Derive the associated token account of <owner> for token <mint>" << endl;
    _out << "  solanaTokenSign <mint> <decimals> <to> <amount> <recentBlockhash> [create]" << endl;
    _out << "                          Sign an SPL token transfer (e.g. USDT) to the associated token account of <to>." << endl;
    _out << "                          <amount> is in the token's smallest unit. Add \"create\" if <to> has no token account yet." << endl;
    _out << "Transformations:" << endl;
    _out << "  hex <inp>               Encode given string to hex" << endl;
    _out << "  base64Encode <inp>      Encode given hex data to Base64" << endl;
    _out << "  base64Decode <inp>      Decode given Base64 string to hex data" << endl;
    _out << "File methods:" << endl;
    _out << "  fileW <fileName> <data> Write data to a (new) binary file." << endl;
    _out << "  fileR <fileName>        Read data from a binary file." << endl;
    _out << endl;
    _out << "Examples:" << endl;
    _out << "> coin bitcoin <Enter> newKey <Enter> addrPri # <Enter> fileW btcaddr.txt #" << endl;
    _out << "> newKey <Enter> pubPri # <Enter> addrPub #" << endl;
    _out << "> coin algo <Enter> addr LCSUSBOLNVT6BND6DWWGM4DLVUYJN3PGBT4T7LTCMDMKS7TR7FZAOHOVPE" << endl;
    _out << "> coin btc <Enter> dumpDP <Enter> priDP <Enter> priDP m/84'/0'/0'/0/1 <Enter> pubPri # <Enter> addrPub # <Enter> addr #" << endl;
    _out << "> coin nano <Enter> dumpDP <Enter> setMnemonic word1 word2 ... word12 <Enter> addrDefault <Enter> addrDP m/44'/165'/0' <Enter> addrDP m/44'/165'/1'" << endl;
    _out << "> hex Hello <Enter> base64Encode # <Enter> base64Decode # <Enter> buffer" << endl;

    _out << endl;
}

/// Parse a non-negative decimal integer that fits in uint64 (std::stoull alone throws on overflow).
static bool parseUint64(const string& s, uint64_t& out) {
    if (s.empty() || !all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); })) {
        return false;
    }
    try {
        out = std::stoull(s);
    } catch (const exception&) {
        return false;
    }
    return true;
}

bool CommandExecutor::prepareSolanaInput(const string& blockhash, Solana::Proto::SigningInput& input, string& senderAddr) {
    if (_keys.getMnemo().empty()) {
        _out << "No mnemonic set; use setMnemonic or newMnemonic first" << endl;
        return false;
    }
    // Sender and private key, both from the current mnemonic at the Solana default path
    if (!_address.addrDefault("solana", senderAddr)) { return false; }
    string priHex;
    if (!_keys.priDP("solana", "", priHex)) { return false; }

    auto key = parse_hex(priHex);
    input.set_private_key(key.data(), key.size());
    input.set_sender(senderAddr);
    input.set_recent_blockhash(blockhash);
    return true;
}

bool CommandExecutor::signSolanaInput(const Solana::Proto::SigningInput& input, string& res) {
    auto inputData = input.SerializeAsString();
    TWData* inputTW = TWDataCreateWithBytes(reinterpret_cast<const uint8_t*>(inputData.data()), inputData.size());
    TWData* outputTW = TWAnySignerSign(inputTW, TWCoinTypeSolana);
    TWDataDelete(inputTW);

    Solana::Proto::SigningOutput output;
    bool parsed = output.ParseFromArray(TWDataBytes(outputTW), static_cast<int>(TWDataSize(outputTW)));
    TWDataDelete(outputTW);

    if (!parsed) {
        _out << "Signing failed: could not parse signer output" << endl;
        return false;
    }
    if (output.error() != Common::Proto::OK) {
        _out << "Signing failed: " << output.error_message() << endl;
        return false;
    }
    if (output.encoded().empty()) {
        _out << "Signing failed: empty transaction" << endl;
        return false;
    }
    res = output.encoded();
    return true;
}

bool CommandExecutor::signSolana(const vector<string>& params, string& res) {
    const string& to = params[1];
    const string& blockhash = params[3];

    uint64_t lamports = 0;
    if (!parseUint64(params[2], lamports)) {
        _out << "Lamports must be a non-negative integer that fits in 64 bits" << endl;
        return false;
    }

    Solana::Proto::SigningInput input;
    string senderAddr;
    if (!prepareSolanaInput(blockhash, input, senderAddr)) { return false; }
    auto& transfer = *input.mutable_transfer_transaction();
    transfer.set_recipient(to);
    transfer.set_value(lamports);
    return signSolanaInput(input, res);
}

/// Derive the associated token account of `owner` for `mint` (classic SPL token program).
static bool deriveSolanaAta(const string& owner, const string& mint, string& out) {
    TWString* ownerTW = TWStringCreateWithUTF8Bytes(owner.c_str());
    TWString* mintTW = TWStringCreateWithUTF8Bytes(mint.c_str());
    TWSolanaAddress* address = TWSolanaAddressCreateWithString(ownerTW);
    TWString* ataTW = address ? TWSolanaAddressDefaultTokenAddress(address, mintTW) : nullptr;
    bool ok = ataTW != nullptr;
    if (ok) {
        out = TWStringUTF8Bytes(ataTW);
        TWStringDelete(ataTW);
    }
    if (address) { TWSolanaAddressDelete(address); }
    TWStringDelete(ownerTW);
    TWStringDelete(mintTW);
    return ok;
}

bool CommandExecutor::solanaAta(const vector<string>& params, string& res) {
    const string& owner = params[1];
    const string& mint = params[2];
    if (!Solana::Address::isValid(owner) || !Solana::Address::isValid(mint)) {
        _out << "Invalid owner or mint address" << endl;
        return false;
    }
    if (!deriveSolanaAta(owner, mint, res)) {
        _out << "Could not derive token account" << endl;
        return false;
    }
    return true;
}

bool CommandExecutor::signSolanaToken(const vector<string>& params, string& res) {
    const string& mint = params[1];
    const string& recipient = params[3];
    const string& blockhash = params[5];
    const bool create = params.size() > 6 && params[6] == "create";
    if (params.size() > 6 && !create) {
        _out << "Unknown option \"" << params[6] << "\"; only \"create\" is supported" << endl;
        return false;
    }

    uint64_t decimals = 0;
    uint64_t amount = 0;
    if (!parseUint64(params[2], decimals) || decimals > 255) {
        _out << "Decimals must be an integer between 0 and 255" << endl;
        return false;
    }
    if (!parseUint64(params[4], amount)) {
        _out << "Amount must be a non-negative integer (in the token's smallest unit) that fits in 64 bits" << endl;
        return false;
    }
    if (!Solana::Address::isValid(mint) || !Solana::Address::isValid(recipient)) {
        _out << "Invalid mint or recipient address" << endl;
        return false;
    }

    Solana::Proto::SigningInput input;
    string senderAddr;
    if (!prepareSolanaInput(blockhash, input, senderAddr)) { return false; }

    // Token accounts are the associated token accounts (ATAs) of the main addresses
    string senderToken;
    string recipientToken;
    if (!deriveSolanaAta(senderAddr, mint, senderToken) || !deriveSolanaAta(recipient, mint, recipientToken)) {
        _out << "Could not derive token accounts" << endl;
        return false;
    }
    _out << "Sender token account:    " << senderToken << endl;
    _out << "Recipient token account: " << recipientToken << (create ? "  (will be created)" : "") << endl;

    if (create) {
        auto& t = *input.mutable_create_and_transfer_token_transaction();
        t.set_recipient_main_address(recipient);
        t.set_token_mint_address(mint);
        t.set_recipient_token_address(recipientToken);
        t.set_sender_token_address(senderToken);
        t.set_amount(amount);
        t.set_decimals(static_cast<uint32_t>(decimals));
    } else {
        auto& t = *input.mutable_token_transfer_transaction();
        t.set_token_mint_address(mint);
        t.set_sender_token_address(senderToken);
        t.set_recipient_token_address(recipientToken);
        t.set_amount(amount);
        t.set_decimals(static_cast<uint32_t>(decimals));
    }
    return signSolanaInput(input, res);
}

bool CommandExecutor::executeOne(const string& cmd, const vector<string>& params_in, string& res) {
    if (cmd == "exit" || cmd == "quit") {
        // handled in loop
        _out << "Bye!" << endl;
        return false;
    }

    // prepare inputs
    vector<string> params;
    if (!prepareInputs(params_in, params)) {
        _out << "Error processing input(s)" << endl;
        return false;
    }
    
    if (cmd == "help") { help(); return false; }
    if (cmd[0] == '#') {
        // echo input, with substitution
        _out << "Previous result is:  " << params[0] << endl;
        return false;
    }
    if (cmd == "buffer") { _buffer.buffer(); return false; }

    if (cmd == "coins") { _coins.coins(); return false; }
    if (cmd == "coin") { if (!checkMinParams(params, 1)) { return false; } setCoin(params[1], true); return false; }

    if (cmd == "newkey") { return _keys.newKey(_activeCoin, res); }
    if (cmd == "pubpri") { if (!checkMinParams(params, 1)) { return false; } return _keys.pubPri(_activeCoin, params[1], res); }
    if (cmd == "pripub") { if (!checkMinParams(params, 1)) { return false; } return _keys.priPub(params[1], res); }
    if (cmd == "setmnemonic" || cmd == "setmenmonic") { if (!checkMinParams(params, 1)) { return false; } _keys.setMnemonic(params); return false; }
    if (cmd == "newmnemonic" || cmd == "newmenmonic") { if (!checkMinParams(params, 1)) { return false; } return _keys.newMnemonic(params[1], res); }
    if (cmd == "dumpseed") { return _keys.dumpSeed(res); }
    if (cmd == "dumpmnemonic" || cmd == "dumpmenmonic") { return _keys.dumpMnemonic(res); }
    if (cmd == "dumpdp") { return _keys.dumpDP(_activeCoin, res); }
    if (cmd == "dumpxpub") { return _keys.dumpXpub(_activeCoin, res); }
    if (cmd == "pridp") { string dp; if (params.size() >= 2) dp = params[1]; return _keys.priDP(_activeCoin, dp, res); }

    if (cmd == "addrpub") { if (!checkMinParams(params, 1)) { return false; } return _address.addrPub(_activeCoin, params[1], res); }
    if (cmd == "addrpri") { if (!checkMinParams(params, 1)) { return false; } return _address.addrPri(_activeCoin, params[1], res); }
    if (cmd == "addr") { if (!checkMinParams(params, 1)) { return false; } return _address.addr(_activeCoin, params[1], res); }
    if (cmd == "addrdefault") { return _address.addrDefault(_activeCoin, res); }
    if (cmd == "addrdp") { if (!checkMinParams(params, 1)) { return false; } return _address.deriveFromPath(_activeCoin, params[1], res); }
    if (cmd == "addrxpub") { if (!checkMinParams(params, 2)) { return false; } return _address.deriveFromXpubIndex(_activeCoin, params[1], params[2], res); }

    if (cmd == "solanasign") { if (!checkMinParams(params, 3)) { return false; } return signSolana(params, res); }
    if (cmd == "solanaata") { if (!checkMinParams(params, 2)) { return false; } return solanaAta(params, res); }
    if (cmd == "solanatokensign") { if (!checkMinParams(params, 5)) { return false; } return signSolanaToken(params, res); }

    if (cmd == "hex") { if (!checkMinParams(params, 1)) { return false; } return Util::hex(params[1], res); }
    if (cmd == "base64encode") { if (!checkMinParams(params, 1)) { return false; } return _util.base64Encode(params[1], res); }
    if (cmd == "base64decode") { if (!checkMinParams(params, 1)) { return false; } return _util.base64Decode(params[1], res); }
    
    if (cmd == "filew") { if (!checkMinParams(params, 2)) { return false; } return _util.fileW(params[1], params[2], res); }
    if (cmd == "filer") { if (!checkMinParams(params, 1)) { return false; } return _util.fileR(params[1], res); }

    // fallback
    _out << "Unknown command:  " << cmd << endl << "Type 'help' for list of commands." << endl;
    return false;
}

void CommandExecutor::executeLine(const string& line) {
    vector<string> params;
    auto cmd = parseLine(line, params);
    execute(cmd, params);
}

void CommandExecutor::execute(const string& cmd, const vector<string>& params) {
    try {
        string resultStr;
        bool res = executeOne(cmd, params, resultStr);
        if (res && resultStr.length() > 0) {
            // there is a new result
            _out << "Result:  " << resultStr << endl;
            _buffer.addResult(resultStr);
        }
    } catch (exception& ex) {
        _out << "Error while executing command, " << ex.what() << endl;
    }
}

bool CommandExecutor::prepareInputs(const vector<string>& p_in, vector<string>& p_out) {
    p_out = vector<string>{};
    for (auto p: p_in) {
        string p2;
        if (!_buffer.prepareInput(p, p2)) {
            return false;
        }
        p_out.push_back(p2);
    }
    assert(p_out.size() == p_in.size());
    return true;
}

string CommandExecutor::parseLine(const string& line, vector<string>& params) {
    auto tokens = Util::tokenize(line);
    assert(tokens.size() > 0);
    auto cmd = tokens[0];
    Util::toLower(cmd);
    params = tokens;
    return cmd;
}

bool CommandExecutor::checkMinParams(const vector<string>& params, std::size_t n) const {
    if (params.size() - 1 >= n) {
        return true;
    }
    _out << "At least " << n << " parameters are needed! See 'help'" << endl;
    //help();
    return false;
}

bool CommandExecutor::setCoin(const string& coin, bool force) {
    Coin c;
    if (!_coins.findCoin(coin, c)) {
        return false;
    }
    if (_activeCoin != c.id || force) {
        // need to change
        _activeCoin = c.id;
        _out << "Set active coin to: " << c.id << "    Use 'coin' to change.  (name: '" << c.name << "'  symbol: " << c.symbol << "  numericalid: " << c.c << ")" << endl;
    }
    return true;
}

} // namespace TW::WalletConsole
