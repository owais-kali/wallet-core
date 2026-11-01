// SPDX-License-Identifier: Apache-2.0
//
// Copyright © 2017 Trust Wallet.

#include "WalletConsole.h"
#include "CommandExecutor.h"
#include "Keys.h"
#include "Coins.h"
#include "Util.h"
#include "Address.h"

#include "Base64.h"
#include "HexCoding.h"
#include "Data.h"

#include <TrezorCrypto/bip39.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <cassert>

#ifdef WALLETCONSOLE_LINE_EDIT
#include <unistd.h>
#include <cstdio>
#include <editline/readline.h>
#endif

namespace TW::WalletConsole {

using namespace std;
using namespace TW;


#ifdef WALLETCONSOLE_LINE_EDIT
namespace {

const char* const commandNames[] = {
    "exit", "quit", "help", "buffer", "coins", "coin", "newKey", "pubPri", "priPub", "setMnemonic", "newMnemonic",
    "dumpSeed", "dumpMnemonic", "dumpDP", "dumpXpub", "priDP", "addrPub", "addrPri", "addr", "addrDefault",
    "addrDP", "addrXpub", "solanaSign", "solanaAta", "solanaTokenSign", "btcSign", "hex", "base64Encode",
    "base64Decode", "fileW", "fileR"};

bool startsWithNoCase(const char* s, const char* prefix) {
    for (; *prefix; ++s, ++prefix) {
        if (std::tolower(static_cast<unsigned char>(*s)) != std::tolower(static_cast<unsigned char>(*prefix))) { return false; }
    }
    return true;
}

char* completeMnemonicWord(const char* text, int state) {
    static int index;
    if (state == 0) { index = 0; }
    const size_t len = strlen(text);
    while (index < 2048) {
        const char* word = mnemonic_get_word(index++);
        if (word != nullptr && strncmp(word, text, len) == 0) { return strdup(word); }
    }
    return nullptr;
}

char* completeCommand(const char* text, int state) {
    static size_t index;
    if (state == 0) { index = 0; }
    while (index < sizeof(commandNames) / sizeof(commandNames[0])) {
        const char* name = commandNames[index++];
        if (startsWithNoCase(name, text)) { return strdup(name); }
    }
    return nullptr;
}

/// Complete command names for the first word, and BIP39 words after setMnemonic.
char** completer(const char* text, int start, int) {
    rl_attempted_completion_over = 1; // never fall back to filename completion
    if (start == 0) { return rl_completion_matches(text, completeCommand); }
    std::string head(rl_line_buffer, static_cast<size_t>(start));
    const auto first = head.find_first_not_of(' ');
    const auto end = head.find(' ', first == std::string::npos ? 0 : first);
    if (first != std::string::npos && end != std::string::npos &&
        startsWithNoCase(head.substr(first, end - first).c_str(), "setmnemonic") &&
        head.substr(first, end - first).size() == strlen("setmnemonic")) {
        return rl_completion_matches(text, completeMnemonicWord);
    }
    return nullptr;
}

bool isMnemonicCommand(const std::string& line) {
    std::string l = line;
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return std::tolower(c); });
    const auto first = l.find_first_not_of(' ');
    return first != std::string::npos && (l.compare(first, 11, "setmnemonic") == 0 || l.compare(first, 11, "setmenmonic") == 0);
}

} // namespace
#endif

void WalletConsole::init() {
    _outst << endl;
    _outst << "Wallet-core Console                          (c) TrustWallet" << endl;
    _outst << "Type 'help' for list of commands." << endl;
    _outst << endl;

    _executor.init();
}

void WalletConsole::loop() {
#ifdef WALLETCONSOLE_LINE_EDIT
    // Tab completion only for an interactive terminal; piped input (scripts) keeps using getline
    const bool interactive = &_inst == &cin && &_outst == &cout && isatty(STDIN_FILENO);
    if (interactive) { rl_attempted_completion_function = completer; }
#endif
    while (true) {
        string line;
#ifdef WALLETCONSOLE_LINE_EDIT
        if (interactive) {
            char* raw = readline("> ");
            if (raw == nullptr) { break; } // Ctrl-D
            line = raw;
            free(raw);
            // Never keep a mnemonic in the in-memory history
            if (!line.empty() && !isMnemonicCommand(line)) { add_history(line.c_str()); }
        } else
#endif
        {
            _outst << "> ";
            getline(_inst, line);
        }
        Util::trimLeft(line);
        if (line.length() == 0) { continue; }
        if (isExit(line)) {
            break;
        }

        _executor.executeLine(line);
    }
    _outst << "Bye!" << endl << endl;
}

bool WalletConsole::isExit(const string& line) {
    if (line.length() < 4) return false;
    string first4 = line.substr(0, 4);
    if (first4 == "quit" || first4 == "exit") { return true; }
    return false;
}

} // namespace TW::WalletConsole
