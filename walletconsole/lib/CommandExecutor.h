// SPDX-License-Identifier: Apache-2.0
//
// Copyright © 2017 Trust Wallet.

#pragma once

#include "Buffer.h"
#include "Keys.h"
#include "Coins.h"
#include "Address.h"
#include "Util.h"

#include "Data.h"

#include <string>
#include <vector>
#include <cassert>
#include <iostream>

namespace TW::Solana::Proto {
class SigningInput;
}

namespace TW::WalletConsole {

using namespace std;

class CommandExecutor {
protected:
    ostream& _out;
    Coins _coins;
    Buffer _buffer;
    Keys _keys;
    Address _address;
    Util _util;
    string _activeCoin;

public:
    CommandExecutor(ostream& out);
    void init();
    void executeLine(const string& line);

protected:
    /// Put result in res.  Return true if meaningful result is returned. 
    bool executeOne(const string& cmd, const vector<string>& params, string& res);
    void execute(const string& cmd, const vector<string>& params);
    static vector<string> tokenize(const string& line);
    static string parseLine(const string& line, vector<string>& params);
    bool prepareInputs(const vector<string>& p_in, vector<string>& p_out);
    bool setCoin(const string& coin, bool force);
    bool checkMinParams(const vector<string>& params, std::size_t n) const;
    void help() const;
    /// Fill key, sender and blockhash of a Solana signing input from the current mnemonic
    bool prepareSolanaInput(const string& blockhash, Solana::Proto::SigningInput& input, string& senderAddr);
    /// Run the Solana signer on a prepared input; res is the encoded signed transaction
    bool signSolanaInput(const Solana::Proto::SigningInput& input, string& res);
    /// Sign a native SOL transfer with the Solana key of the current mnemonic
    bool signSolana(const vector<string>& params, string& res);
    /// Derive an associated token account
    bool solanaAta(const vector<string>& params, string& res);
    /// Sign an SPL token transfer (optionally creating the recipient token account)
    bool signSolanaToken(const vector<string>& params, string& res);
    /// Sign a native-segwit BTC transfer from the current mnemonic, spending the given UTXOs
    bool signBitcoin(const vector<string>& params, string& res);
};

} // namespace TW::WalletConsole
