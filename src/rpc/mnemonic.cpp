// Copyright (c) 2015 The ShadowCoin developers
// Copyright (c) 2017-2021 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>

#include <util/strencodings.h>
#include <rpc/util.h>
#include <key_io.h>
#include <key/extkey.h>
#include <random.h>
#include <chainparams.h>
#include <support/cleanse.h>
#include <key/mnemonic.h>

#include <string>
#include <univalue.h>

//typedef std::basic_string<char, std::char_traits<char>, secure_allocator<char> > SecureString;

static RPCHelpMan mnemonicrpc()
{
    return RPCHelpMan{"mnemonic",
            "\nGenerate mnemonic phrases.\n"
        "mnemonic new ( \"password\" language nBytesEntropy bip44 )\n"
        "    Generate a new extended key and mnemonic\n"
        "    password, can be blank "", default blank\n"
        "    language, " + mnemonic::ListEnabledLanguages("|") + ", default english\n"
        "    nBytesEntropy, 16 -> 64, default 32\n"
        "    bip44, true|false, default true\n"
        "mnemonic decode \"password\" \"mnemonic\" ( bip44 )\n"
        "    Decode mnemonic\n"
        "    bip44, true|false, default true\n"
        "mnemonic addchecksum \"mnemonic\"\n"
        "    Add checksum words to mnemonic.\n"
        "    Final no of words in mnemonic must be divisible by three.\n"
        "mnemonic dumpwords ( \"language\" )\n"
        "    Print list of words.\n"
        "    language, default english\n"
         "mnemonic listlanguages\n"
        "    Print list of supported languages.\n",
    {
        {"mode", RPCArg::Type::STR, RPCArg::Optional::NO, "One of: new, decode, addchecksum, dumpwords, listlanguages"},
        {"arg0", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, ""},
        {"arg1", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, ""},
        {"arg2", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, ""},
        {"arg3", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, ""},
    },
    RPCResult{RPCResult::Type::ANY, "", ""},
    RPCExamples{
        HelpExampleCli("mnemonic", "\"new\" \"my pass phrase\" french 64 true") +
        HelpExampleRpc("smsgpurge", "\"new\", \"my pass phrase\", french, 64, true")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::string mode;

    if (request.params.size() > 0) {
        std::string s = request.params[0].get_str();
        std::string st = " " + s + " "; // Note the spaces
        st = ToLower(st);
        static const char *pmodes = " new decode addchecksum dumpwords listlanguages ";
        if (strstr(pmodes, st.c_str()) != nullptr) {
            st.erase(std::remove(st.begin(), st.end(), ' '), st.end());
            mode = st;
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown mode.");
        }
    }

    UniValue result(UniValue::VOBJ);

    if (mode == "new") {
        int nLanguage = mnemonic::WLL_ENGLISH;
        int nBytesEntropy = 32;
        std::string sMnemonic, sPassword, sError;
        CExtKey ekMaster;

        if (request.params.size() > 1) {
            sPassword = request.params[1].get_str();
        }
        if (request.params.size() > 2) {
            nLanguage = mnemonic::GetLanguageOffset(request.params[2].get_str());
        }

        if (request.params.size() > 3) {
            if (!ParseInt32(request.params[3].get_str(), &nBytesEntropy)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid num bytes entropy");
            }
            if (nBytesEntropy < 16 || nBytesEntropy > 64) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Num bytes entropy out of range [16,64].");
            }
        }

        bool fBip44 = request.params.size() > 4 ? GetBool(request.params[4]) : true;

        if (request.params.size() > 5) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many parameters");
        }

        std::vector<uint8_t> vEntropy(nBytesEntropy), vSeed;
        for (uint32_t i = 0; i < MAX_DERIVE_TRIES; ++i) {
            GetStrongRandBytes2(&vEntropy[0], nBytesEntropy);

            if (0 != mnemonic::Encode(nLanguage, vEntropy, sMnemonic, sError)) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("mnemonic::Encode failed %s.", sError.c_str()).c_str());
            }
            if (0 != mnemonic::ToSeed(sMnemonic, sPassword, vSeed)) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "mnemonic::ToSeed failed.");
            }

            ekMaster.SetSeed(&vSeed[0], vSeed.size());
            if (!ekMaster.IsValid()) {
                continue;
            }
            break;
        }

        CExtKey58 eKey58;
        result.pushKV("mnemonic", sMnemonic);

        if (fBip44) {
            eKey58.SetKey(CExtKeyPair(ekMaster), CChainParams::EXT_SECRET_KEY_BTC);
            result.pushKV("master", eKey58.ToString());

            // m / purpose' / coin_type' / account' / change / address_index
            // path "44' Params().BIP44ID()
        } else {
            eKey58.SetKey(CExtKeyPair(ekMaster), CChainParams::EXT_SECRET_KEY);
            result.pushKV("master", eKey58.ToString());
        }

        // In c++11 strings are definitely contiguous, and before they're very unlikely not to be
        if (sMnemonic.size() > 0) {
            memory_cleanse(&sMnemonic[0], sMnemonic.size());
        }
        if (sPassword.size() > 0) {
            memory_cleanse(&sPassword[0], sPassword.size());
        }
    } else
    if (mode == "decode") {
        std::string sPassword, sMnemonic, sError;

        if (request.params.size() > 1) {
            sPassword = request.params[1].get_str();
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Must specify password.");
        }
        if (request.params.size() > 2) {
            sMnemonic = request.params[2].get_str();
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Must specify mnemonic.");
        }

        bool fBip44 = request.params.size() > 3 ? GetBool(request.params[3]) : true;

        if (request.params.size() > 4) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many parameters");
        }
        if (sMnemonic.empty()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Mnemonic can't be blank.");
        }

        // Decode to determine validity of mnemonic
        std::vector<uint8_t> vEntropy, vSeed;
        int nLanguage = -1;
        if (0 != mnemonic::Decode(nLanguage, sMnemonic, vEntropy, sError)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("mnemonic::Decode failed %s.", sError.c_str()).c_str());
        }
        if (0 != mnemonic::ToSeed(sMnemonic, sPassword, vSeed)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "mnemonic::ToSeed failed.");
        }

        CExtKey ekMaster;
        CExtKey58 eKey58;
        ekMaster.SetSeed(&vSeed[0], vSeed.size());

        if (!ekMaster.IsValid()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid key.");
        }

        if (fBip44) {
            eKey58.SetKey(CExtKeyPair(ekMaster), CChainParams::EXT_SECRET_KEY_BTC);
            result.pushKV("master", eKey58.ToString());

            // m / purpose' / coin_type' / account' / change / address_index
            CExtKey ekDerived;
            if (!ekMaster.Derive(ekDerived, BIP44_PURPOSE)) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to derive master key");
            }
            if (!ekDerived.Derive(ekDerived, (uint32_t)Params().BIP44ID())) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to derive bip44 key");
            }

            eKey58.SetKey(CExtKeyPair(ekDerived), CChainParams::EXT_SECRET_KEY);
            result.pushKV("derived", eKey58.ToString());
        } else {
            eKey58.SetKey(CExtKeyPair(ekMaster), CChainParams::EXT_SECRET_KEY);
            result.pushKV("master", eKey58.ToString());
        }

        result.pushKV("language", mnemonic::GetLanguage(nLanguage));

        if (sMnemonic.size() > 0) {
            memory_cleanse(&sMnemonic[0], sMnemonic.size());
        }
        if (sPassword.size() > 0) {
            memory_cleanse(&sPassword[0], sPassword.size());
        }
    } else
    if (mode == "addchecksum") {
        std::string sMnemonicIn, sMnemonicOut, sError;
        if (request.params.size() != 2) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Must provide input mnemonic.");
        }

        sMnemonicIn = request.params[1].get_str();

        if (0 != mnemonic::AddChecksum(-1, sMnemonicIn, sMnemonicOut, sError)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("mnemonic::AddChecksum failed %s", sError.c_str()).c_str());
        }
        result.pushKV("result", sMnemonicOut);
    } else
    if (mode == "dumpwords") {
        int nLanguage = mnemonic::WLL_ENGLISH;

        if (request.params.size() > 1) {
            nLanguage = mnemonic::GetLanguageOffset(request.params[1].get_str());
        }

        int nWords = 0;
        UniValue arrayWords(UniValue::VARR);

        std::string sWord, sError;
        while (0 == mnemonic::GetWord(nLanguage, nWords, sWord, sError)) {
            arrayWords.push_back(sWord);
            nWords++;
        }

        result.pushKV("words", arrayWords);
        result.pushKV("num_words", nWords);
    } else
    if (mode == "listlanguages") {
        for (size_t k = 1; k < mnemonic::WLL_MAX; ++k) {
            if (!mnemonic::HaveLanguage(k)) {
                continue;
            }
            std::string sName(mnemonic::mnLanguagesTag[k]);
            std::string sDesc(mnemonic::mnLanguagesDesc[k]);
            result.pushKV(sName, sDesc);
        }
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown mode.");
    }

    return result;
},
    };
};

void RegisterMnemonicRPCCommands(CRPCTable &t)
{
    static const CRPCCommand commands[]{
        {"mnemonic", &mnemonicrpc},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
