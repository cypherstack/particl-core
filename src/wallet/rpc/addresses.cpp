// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <core_io.h>
#include <key_io.h>
#include <rpc/util.h>
#include <util/bip32.h>
#include <util/translation.h>
#include <wallet/receive.h>
#include <wallet/rpc/util.h>
#include <wallet/wallet.h>
#include <wallet/hdwallet.h>

#include <univalue.h>

namespace wallet {
RPCHelpMan getnewaddress()
{
    return RPCHelpMan{"getnewaddress",
                "\nReturns a new Particl address for receiving payments.\n"
                "If 'label' is specified, it is added to the address book \n"
                "so payments received with the address will be associated with 'label'.\n",
                {
                    {"label", RPCArg::Type::STR, RPCArg::Default{""}, "The label name for the address to be linked to. It can also be set to the empty string \"\" to represent the default label. The label does not need to exist, it will be created if there is no label by the given name."},
                    {"bech32", RPCArg::Type::BOOL, RPCArg::Default{false}, "Use Bech32 encoding."},
                    {"hardened", RPCArg::Type::BOOL, RPCArg::Default{false}, "Derive a hardened key."},
                    {"256bit", RPCArg::Type::BOOL, RPCArg::Default{false}, "Use 256bit hash type."},
                    {"address_type", RPCArg::Type::STR, RPCArg::DefaultHint{"set by -addresstype"}, "The address type to use. Options are \"legacy\", \"p2sh-segwit\", and \"bech32\", and \"bech32m\"."},
                },
                RPCResult{
                    RPCResult::Type::STR, "address", "The new particl address"
                },
                RPCExamples{
                    HelpExampleCli("getnewaddress", "")
            + HelpExampleRpc("getnewaddress", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    //LOCK(pwallet->cs_wallet);

    if (!pwallet->IsParticlWallet()) {
        LOCK(pwallet->cs_wallet);
        if (!pwallet->CanGetAddresses()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Error: This wallet has no available keys");
        }
    }

    // Parse the label first so we don't generate a key if there's an error
    std::string label;
    if (!request.params[0].isNull())
        label = LabelFromValue(request.params[0]);

    OutputType output_type = pwallet->m_default_address_type;
    size_t type_ofs = fParticlMode ? 4 : 1;
    if (!request.params[type_ofs].isNull()) {
        std::optional<OutputType> parsed = ParseOutputType(request.params[type_ofs].get_str());
        if (!parsed) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Unknown address type '%s'", request.params[type_ofs].get_str()));
        } else if (parsed.value() == OutputType::BECH32M && pwallet->GetLegacyScriptPubKeyMan()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Legacy wallets cannot provide bech32m addresses");
        }
        output_type = parsed.value();
    }

    if (pwallet->IsParticlWallet()) {
        CKeyID keyID;

        bool fBech32 = request.params.size() > 1 ? GetBool(request.params[1]) : false;
        bool fHardened = request.params.size() > 2 ? GetBool(request.params[2]) : false;
        bool f256bit = request.params.size() > 3 ? GetBool(request.params[3]) : false;

        if (output_type == OutputType::P2SH_SEGWIT) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "p2sh-segwit is disabled");
        }
        if (f256bit && output_type != OutputType::LEGACY) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "256bit must be used with address_type \"legacy\"");
        }

        CPubKey newKey;
        CHDWallet *phdw = GetParticlWallet(pwallet.get());
        {
            LOCK(phdw->cs_wallet);
            if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Error: Private keys are disabled for this wallet");
            }
            if (phdw->idDefaultAccount.IsNull()) {
                if (!phdw->pEKMaster) {
                    throw JSONRPCError(RPC_WALLET_ERROR, "Wallet has no active master key.");
                }
                throw JSONRPCError(RPC_WALLET_ERROR, "No default account set.");
            }
        }
        if (0 != phdw->NewKeyFromAccount(newKey, false, fHardened, f256bit, fBech32, label.c_str(), output_type)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "NewKeyFromAccount failed.");
        }

        if (output_type != OutputType::LEGACY) {
            CTxDestination dest;
            LegacyScriptPubKeyMan* spk_man = pwallet->GetLegacyScriptPubKeyMan();
            if (spk_man) {
                spk_man->LearnRelatedScripts(newKey, output_type);
            }
            dest = GetDestinationForKey(newKey, output_type);
            return EncodeDestination(dest);
        }
        if (f256bit) {
            CKeyID256 idKey256 = newKey.GetID256();
            return CBitcoinAddress(idKey256, fBech32).ToString();
        }
        return CBitcoinAddress(PKHash(newKey), fBech32).ToString();
    }

    LOCK(pwallet->cs_wallet);

    auto op_dest = pwallet->GetNewDestination(output_type, label);
    if (!op_dest) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, util::ErrorString(op_dest).original);
    }

    return EncodeDestination(*op_dest);
},
    };
}

RPCHelpMan getrawchangeaddress()
{
    return RPCHelpMan{"getrawchangeaddress",
                "\nReturns a new Particl address, for receiving change.\n"
                "This is for use with raw transactions, NOT normal use.\n",
                {
                    {"address_type", RPCArg::Type::STR, RPCArg::DefaultHint{"set by -changetype"}, "The address type to use. Options are \"legacy\", \"p2sh-segwit\", \"bech32\", and \"bech32m\"."},
                },
                RPCResult{
                    RPCResult::Type::STR, "address", "The address"
                },
                RPCExamples{
                    HelpExampleCli("getrawchangeaddress", "")
            + HelpExampleRpc("getrawchangeaddress", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LOCK(pwallet->cs_wallet);

    if (pwallet->IsParticlWallet()) {
        CHDWallet *phdw = GetParticlWallet(pwallet.get());
        CPubKey pkOut;

        if (0 != phdw->NewKeyFromAccount(pkOut, true)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "NewKeyFromAccount failed.");
        }
        return EncodeDestination(PKHash(pkOut.GetID()));
    }

    if (!pwallet->CanGetAddresses(true)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: This wallet has no available keys");
    }

    OutputType output_type = pwallet->m_default_change_type.value_or(pwallet->m_default_address_type);
    if (!request.params[0].isNull()) {
        std::optional<OutputType> parsed = ParseOutputType(request.params[0].get_str());
        if (!parsed) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Unknown address type '%s'", request.params[0].get_str()));
        } else if (parsed.value() == OutputType::BECH32M && pwallet->GetLegacyScriptPubKeyMan()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Legacy wallets cannot provide bech32m addresses");
        }
        output_type = parsed.value();
    }

    auto op_dest = pwallet->GetNewChangeDestination(output_type);
    if (!op_dest) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, util::ErrorString(op_dest).original);
    }
    return EncodeDestination(*op_dest);
},
    };
}


RPCHelpMan setlabel()
{
    return RPCHelpMan{"setlabel",
                "\nSets the label associated with the given address.\n",
                {
                    {"address", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "The particl address to be associated with a label."},
                    {"label", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "The label to assign to the address."},
                },
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{
                    HelpExampleCli("setlabel", "\"" + EXAMPLE_ADDRESS[0] + "\" \"tabby\"")
            + HelpExampleRpc("setlabel", "\"" + EXAMPLE_ADDRESS[0] + "\", \"tabby\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LOCK(pwallet->cs_wallet);

    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Particl address");
    }

    std::string label = LabelFromValue(request.params[1]);

    if (pwallet->IsMine(dest)) {
        pwallet->SetAddressBook(dest, label, "receive");
    } else {
        pwallet->SetAddressBook(dest, label, "send");
    }

    return UniValue::VNULL;
},
    };
}

RPCHelpMan listaddressgroupings()
{
    return RPCHelpMan{"listaddressgroupings",
                "\nLists groups of addresses which have had their common ownership\n"
                "made public by common use as inputs or as the resulting change\n"
                "in past transactions\n",
                {},
                RPCResult{
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::ARR, "", "",
                        {
                            {RPCResult::Type::ARR_FIXED, "", "",
                            {
                                {RPCResult::Type::STR, "address", "The particl address"},
                                {RPCResult::Type::STR_AMOUNT, "amount", "The amount in " + CURRENCY_UNIT},
                                {RPCResult::Type::STR, "label", /*optional=*/true, "The label"},
                            }},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("listaddressgroupings", "")
            + HelpExampleRpc("listaddressgroupings", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    UniValue jsonGroupings(UniValue::VARR);
    std::map<CTxDestination, CAmount> balances = GetAddressBalances(*pwallet);
    for (const std::set<CTxDestination>& grouping : GetAddressGroupings(*pwallet)) {
        UniValue jsonGrouping(UniValue::VARR);
        for (const CTxDestination& address : grouping)
        {
            UniValue addressInfo(UniValue::VARR);
            addressInfo.push_back(EncodeDestination(address));
            addressInfo.push_back(ValueFromAmount(balances[address]));
            {
                const auto* address_book_entry = pwallet->FindAddressBookEntry(address);
                if (address_book_entry) {
                    addressInfo.push_back(address_book_entry->GetLabel());
                }
            }
            jsonGrouping.push_back(addressInfo);
        }
        jsonGroupings.push_back(jsonGrouping);
    }
    return jsonGroupings;
},
    };
}

RPCHelpMan addmultisigaddress()
{
    return RPCHelpMan{"addmultisigaddress",
                "\nAdd an nrequired-to-sign multisignature address to the wallet. Requires a new wallet backup.\n"
                "Each key is a Particl address or hex-encoded public key.\n"
                "This functionality is only intended for use with non-watchonly addresses.\n"
                "See `importaddress` for watchonly p2sh address support.\n"
                "If 'label' is specified, assign address to that label.\n",
                {
                    {"nrequired", RPCArg::Type::NUM, RPCArg::Optional::NO, "The number of required signatures out of the n keys or addresses."},
                    {"keys", RPCArg::Type::ARR, RPCArg::Optional::NO, "The particl addresses or hex-encoded public keys",
                        {
                            {"key", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "particl address or hex-encoded public key"},
                        },
                        },
                    {"label", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "A label to assign the addresses to."},
                    {"bech32", RPCArg::Type::BOOL, RPCArg::Default{false}, "Use Bech32 encoding."},
                    {"256bit", RPCArg::Type::BOOL, RPCArg::Default{false}, "Use 256bit hash type."},
                    {"address_type", RPCArg::Type::STR, RPCArg::DefaultHint{"set by -addresstype"}, "The address type to use. Options are \"legacy\", \"p2sh-segwit\", and \"bech32\"."},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR, "address", "The value of the new multisig address"},
                        {RPCResult::Type::STR_HEX, "redeemScript", "The string value of the hex-encoded redemption script"},
                        {RPCResult::Type::STR, "descriptor", "The descriptor for this multisig"},
                        {RPCResult::Type::ARR, "warnings", /*optional=*/true, "Any warnings resulting from the creation of this multisig",
                        {
                            {RPCResult::Type::STR, "", ""},
                        }},
                    }
                },
                RPCExamples{
            "\nAdd a multisig address from 2 addresses\n"
            + HelpExampleCli("addmultisigaddress", "2 \"[\\\"" + EXAMPLE_ADDRESS[0] + "\\\",\\\"" + EXAMPLE_ADDRESS[1] + "\\\"]\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("addmultisigaddress", "2, \"[\\\"" + EXAMPLE_ADDRESS[0] + "\\\",\\\"" + EXAMPLE_ADDRESS[1] + "\\\"]\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LegacyScriptPubKeyMan& spk_man = EnsureLegacyScriptPubKeyMan(*pwallet);

    LOCK2(pwallet->cs_wallet, spk_man.cs_KeyStore);

    std::string label;
    if (!request.params[2].isNull())
        label = LabelFromValue(request.params[2]);

    int required = request.params[0].getInt<int>();

    // Get the public keys
    const UniValue& keys_or_addrs = request.params[1].get_array();
    std::vector<CPubKey> pubkeys;
    for (unsigned int i = 0; i < keys_or_addrs.size(); ++i) {
        if (IsHex(keys_or_addrs[i].get_str()) && (keys_or_addrs[i].get_str().length() == 66 || keys_or_addrs[i].get_str().length() == 130)) {
            pubkeys.push_back(HexToPubKey(keys_or_addrs[i].get_str()));
        } else {
            pubkeys.push_back(AddrToPubKey(spk_man, keys_or_addrs[i].get_str()));
        }
    }

    OutputType output_type = pwallet->m_default_address_type;
    size_t type_ofs = fParticlMode ? 5 : 3;
    if (!request.params[type_ofs].isNull()) {
        std::optional<OutputType> parsed = ParseOutputType(request.params[type_ofs].get_str());
        if (!parsed) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Unknown address type '%s'", request.params[type_ofs].get_str()));
        } else if (parsed.value() == OutputType::BECH32M) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Bech32m multisig addresses cannot be created with legacy wallets");
        }
        output_type = parsed.value();
    }

    // Construct using pay-to-script-hash:
    CScript inner;
    CTxDestination dest = AddAndGetMultisigDestination(required, pubkeys, output_type, spk_man, inner);

    // Make the descriptor
    std::unique_ptr<Descriptor> descriptor = InferDescriptor(GetScriptForDestination(dest), spk_man);

    UniValue result(UniValue::VOBJ);
    bool fbech32 = fParticlMode && request.params.size() > 3 ? request.params[3].get_bool() : false;
    bool f256Hash = fParticlMode && request.params.size() > 4 ? request.params[4].get_bool() : false;

    if (f256Hash) {
        CScriptID256 innerID;
        innerID.Set(inner);
        pwallet->SetAddressBook(innerID, label, "send", fbech32);
        result.pushKV("address", CBitcoinAddress(innerID, fbech32).ToString());
    } else {
        pwallet->SetAddressBook(dest, label, "send", fbech32);
        result.pushKV("address", EncodeDestination(dest, fbech32));
    }

    result.pushKV("redeemScript", HexStr(inner));
    result.pushKV("descriptor", descriptor->ToString());

    UniValue warnings(UniValue::VARR);
    if (descriptor->GetOutputType() != output_type) {
        // Only warns if the user has explicitly chosen an address type we cannot generate
        warnings.push_back("Unable to make chosen address type, please ensure no uncompressed public keys are present.");
    }
    if (!warnings.empty()) result.pushKV("warnings", warnings);

    return result;
},
    };
}

RPCHelpMan keypoolrefill()
{
    return RPCHelpMan{"keypoolrefill",
                "\nFills the keypool."+
        HELP_REQUIRING_PASSPHRASE,
                {
                    {"newsize", RPCArg::Type::NUM, RPCArg::DefaultHint{strprintf("%u, or as set by -keypool", DEFAULT_KEYPOOL_SIZE)}, "The new keypool size"},
                },
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{
                    HelpExampleCli("keypoolrefill", "")
            + HelpExampleRpc("keypoolrefill", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    if (pwallet->IsLegacy() && pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Private keys are disabled for this wallet");
    }

    LOCK(pwallet->cs_wallet);

    // 0 is interpreted by TopUpKeyPool() as the default keypool size given by -keypool
    unsigned int kpSize = 0;
    if (!request.params[0].isNull()) {
        if (request.params[0].getInt<int>() < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected valid size.");
        kpSize = (unsigned int)request.params[0].getInt<int>();
    }

    EnsureWalletIsUnlocked(*pwallet);
    pwallet->TopUpKeyPool(kpSize);

    if (pwallet->GetKeyPoolSize() < kpSize) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error refreshing keypool.");
    }

    return UniValue::VNULL;
},
    };
}

RPCHelpMan newkeypool()
{
    return RPCHelpMan{"newkeypool",
                "\nEntirely clears and refills the keypool.\n"
                "WARNING: On non-HD wallets, this will require a new backup immediately, to include the new keys.\n"
                "When restoring a backup of an HD wallet created before the newkeypool command is run, funds received to\n"
                "new addresses may not appear automatically. They have not been lost, but the wallet may not find them.\n"
                "This can be fixed by running the newkeypool command on the backup and then rescanning, so the wallet\n"
                "re-generates the required keys." +
            HELP_REQUIRING_PASSPHRASE,
                {},
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{
            HelpExampleCli("newkeypool", "")
            + HelpExampleRpc("newkeypool", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LOCK(pwallet->cs_wallet);

    LegacyScriptPubKeyMan& spk_man = EnsureLegacyScriptPubKeyMan(*pwallet, true);
    spk_man.NewKeyPool();

    return UniValue::VNULL;
},
    };
}


class DescribeWalletAddressVisitor
{
public:
    const SigningProvider * const provider;

    void ProcessSubScript(const CScript& subscript, UniValue& obj) const
    {
        // Always present: script type and redeemscript
        std::vector<std::vector<unsigned char>> solutions_data;
        TxoutType which_type = Solver(subscript, solutions_data);
        obj.pushKV("script", GetTxnOutputType(which_type));
        obj.pushKV("hex", HexStr(subscript));

        CTxDestination embedded;
        if (ExtractDestination(subscript, embedded)) {
            // Only when the script corresponds to an address.
            UniValue subobj(UniValue::VOBJ);
            UniValue detail = DescribeAddress(embedded);
            subobj.pushKVs(detail);
            UniValue wallet_detail = std::visit(*this, embedded);
            subobj.pushKVs(wallet_detail);
            subobj.pushKV("address", EncodeDestination(embedded));
            subobj.pushKV("scriptPubKey", HexStr(subscript));
            // Always report the pubkey at the top level, so that `getnewaddress()['pubkey']` always works.
            if (subobj.exists("pubkey")) obj.pushKV("pubkey", subobj["pubkey"]);
            obj.pushKV("embedded", std::move(subobj));
        } else if (which_type == TxoutType::MULTISIG) {
            // Also report some information on multisig scripts (which do not have a corresponding address).
            obj.pushKV("sigsrequired", solutions_data[0][0]);
            UniValue pubkeys(UniValue::VARR);
            for (size_t i = 1; i < solutions_data.size() - 1; ++i) {
                CPubKey key(solutions_data[i].begin(), solutions_data[i].end());
                pubkeys.push_back(HexStr(key));
            }
            obj.pushKV("pubkeys", std::move(pubkeys));
        }
    }

    explicit DescribeWalletAddressVisitor(const SigningProvider* _provider) : provider(_provider) {}

    UniValue operator()(const CNoDestination& dest) const { return UniValue(UniValue::VOBJ); }

    UniValue operator()(const PKHash& pkhash) const
    {
        CKeyID keyID{ToKeyID(pkhash)};
        UniValue obj(UniValue::VOBJ);
        CPubKey vchPubKey;
        if (provider && provider->GetPubKey(keyID, vchPubKey)) {
            obj.pushKV("pubkey", HexStr(vchPubKey));
            obj.pushKV("iscompressed", vchPubKey.IsCompressed());
        }
        return obj;
    }

    UniValue operator()(const ScriptHash& scripthash) const
    {
        CScriptID scriptID(scripthash);
        UniValue obj(UniValue::VOBJ);
        CScript subscript;
        if (provider && provider->GetCScript(scriptID, subscript)) {
            ProcessSubScript(subscript, obj);
        }
        return obj;
    }

    UniValue operator()(const WitnessV0KeyHash& id) const
    {
        UniValue obj(UniValue::VOBJ);
        CPubKey pubkey;
        if (provider && provider->GetPubKey(ToKeyID(id), pubkey)) {
            obj.pushKV("pubkey", HexStr(pubkey));
        }
        return obj;
    }

    UniValue operator()(const WitnessV0ScriptHash& id) const
    {
        UniValue obj(UniValue::VOBJ);
        CScript subscript;
        CRIPEMD160 hasher;
        uint160 hash;
        hasher.Write(id.begin(), 32).Finalize(hash.begin());
        if (provider && provider->GetCScript(CScriptID(hash), subscript)) {
            ProcessSubScript(subscript, obj);
        }
        return obj;
    }

    UniValue operator()(const CExtPubKey &ekp) const {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("isextkey", true);
        return obj;
    }

    UniValue operator()(const CStealthAddress &sxAddr) const {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("isstealthaddress", true);
        obj.pushKV("prefix_num_bits", sxAddr.prefix.number_bits);
        obj.pushKV("prefix_bitfield", strprintf("0x%04x", sxAddr.prefix.bitfield));
        return obj;
    }

    UniValue operator()(const CKeyID256 &idk256) const {
        UniValue obj(UniValue::VOBJ);
        CPubKey vchPubKey;
        obj.pushKV("is256bit", true);
        CKeyID id160(idk256);
        if (provider && provider->GetPubKey(id160, vchPubKey)) {
            obj.pushKV("pubkey", HexStr(vchPubKey));
            obj.pushKV("iscompressed", vchPubKey.IsCompressed());
        }
        return obj;
    }

    UniValue operator()(const CScriptID256 &scriptID256) const {
        UniValue obj(UniValue::VOBJ);
        CScript subscript;
        obj.pushKV("is256bit", true);
        CScriptID scriptID;
        scriptID.Set(scriptID256);
        if (provider && provider->GetCScript(scriptID, subscript)) {
            ProcessSubScript(subscript, obj);
        }
        return obj;
    }

    UniValue operator()(const WitnessV1Taproot& id) const { return UniValue(UniValue::VOBJ); }
    UniValue operator()(const WitnessUnknown& id) const { return UniValue(UniValue::VOBJ); }
};

static UniValue DescribeWalletAddress(const CWallet& wallet, const CTxDestination& dest)
{
    UniValue ret(UniValue::VOBJ);
    UniValue detail = DescribeAddress(dest);
    CScript script = GetScriptForDestination(dest);
    std::unique_ptr<SigningProvider> provider = nullptr;
    provider = wallet.GetSolvingProvider(script);
    ret.pushKVs(detail);
    ret.pushKVs(std::visit(DescribeWalletAddressVisitor(provider.get()), dest));
    return ret;
}

RPCHelpMan getaddressinfo()
{
    return RPCHelpMan{"getaddressinfo",
                "\nReturn information about the given particl address.\n"
                "Some of the information will only be present if the address is in the active wallet.\n",                {
                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The particl address to get the information of."},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR, "address", "The particl address validated."},
                        {RPCResult::Type::STR_HEX, "scriptPubKey", "The hex-encoded scriptPubKey generated by the address."},
                        {RPCResult::Type::BOOL, "ismine", "If the address is yours."},
                        {RPCResult::Type::BOOL, "iswatchonly", "If the address is watchonly."},
                        {RPCResult::Type::BOOL, "solvable", "If we know how to spend coins sent to this address, ignoring the possible lack of private keys."},
                        {RPCResult::Type::STR, "desc", /*optional=*/true, "A descriptor for spending coins sent to this address (only when solvable)."},
                        {RPCResult::Type::STR, "parent_desc", /*optional=*/true, "The descriptor used to derive this address if this is a descriptor wallet"},
                        {RPCResult::Type::BOOL, "isscript", /*optional=*/true, "If the key is a script."},
                        {RPCResult::Type::BOOL, "ischange", /*optional=*/true, "If the address was used for change output."},
                        {RPCResult::Type::BOOL, "iswitness", /*optional=*/true, "If the address is a witness address."},
                        {RPCResult::Type::NUM, "witness_version", /*optional=*/true, "The version number of the witness program."},
                        {RPCResult::Type::STR_HEX, "witness_program", /*optional=*/true, "The hex value of the witness program."},
                        {RPCResult::Type::STR, "script", /*optional=*/true, "The output script type. Only if isscript is true and the redeemscript is known. Possible\n"
                                                                     "types: nonstandard, pubkey, pubkeyhash, scripthash, multisig, nulldata, witness_v0_keyhash,\n"
                            "witness_v0_scripthash, witness_unknown."},
                        {RPCResult::Type::STR_HEX, "hex", /*optional=*/true, "The redeemscript for the p2sh address."},
                        {RPCResult::Type::ARR, "pubkeys", /*optional=*/true, "Array of pubkeys associated with the known redeemscript (only if script is multisig).",
                        {
                            {RPCResult::Type::STR, "pubkey", ""},
                        }},
                        {RPCResult::Type::NUM, "sigsrequired", /*optional=*/true, "The number of signatures required to spend multisig output (only if script is multisig)."},
                        {RPCResult::Type::STR_HEX, "pubkey", /*optional=*/true, "The hex value of the raw public key for single-key addresses (possibly embedded in P2SH or P2WSH)."},
                        {RPCResult::Type::OBJ, "embedded", /*optional=*/true, "Information about the address embedded in P2SH or P2WSH, if relevant and known.",
                        {
                            {RPCResult::Type::ELISION, "", "Includes all getaddressinfo output fields for the embedded address, excluding metadata (timestamp, hdkeypath, hdseedid)\n"
                            "and relation to the wallet (ismine, iswatchonly)."},
                        }},
                        {RPCResult::Type::BOOL, "iscompressed", /*optional=*/true, "If the pubkey is compressed."},
                        {RPCResult::Type::NUM_TIME, "timestamp", /*optional=*/true, "The creation time of the key, if available, expressed in " + UNIX_EPOCH_TIME + "."},
                        {RPCResult::Type::STR, "hdkeypath", /*optional=*/true, "The HD keypath, if the key is HD and available."},
                        {RPCResult::Type::STR_HEX, "hdseedid", /*optional=*/true, "The Hash160 of the HD seed."},
                        {RPCResult::Type::STR_HEX, "hdmasterfingerprint", /*optional=*/true, "The fingerprint of the master key."},
                        {RPCResult::Type::ARR, "labels", "Array of labels associated with the address. Currently limited to one label but returned\n"
                            "as an array to keep the API stable if multiple labels are enabled in the future.",
                        {
                            {RPCResult::Type::STR, "label name", "Label name (defaults to \"\")."},
                        }},
                        {RPCResult::Type::STR, "account", /*optional=*/true, "Alias for \"label\"."},
                        {RPCResult::Type::BOOL, "isstealthaddress", /*optional=*/true, "True if the address is a stealth address."},
                        {RPCResult::Type::NUM, "prefix_num_bits", /*optional=*/true, "Number of prefix bits if the address is a stealth address."},
                        {RPCResult::Type::STR, "prefix_bitfield", /*optional=*/true, "Prefix if the address is a stealth address."},
                        {RPCResult::Type::BOOL, "isextkey", /*optional=*/true, "True if the address is a extended address."},
                        {RPCResult::Type::BOOL, "is256bit", /*optional=*/true, "True if the address is a 256bit address."},
                        {RPCResult::Type::STR_HEX, "scan_public_key", /*optional=*/true, "scan_public_key if the address is a stealth address."},
                        {RPCResult::Type::STR_HEX, "spend_public_key", /*optional=*/true, "spend_public_key if the address is a stealth address."},
                        {RPCResult::Type::STR, "scan_path", /*optional=*/true, "keypath of the scan key if the address is a stealth address."},
                        {RPCResult::Type::STR, "spend_path", /*optional=*/true, "keypath of the spend key if the address is a stealth address."},
                        {RPCResult::Type::STR, "from_ext_address_id", /*optional=*/true, "ID of the extkey the pubkey was derived from."},
                        {RPCResult::Type::STR, "from_stealth_address", /*optional=*/true, "Stealthaddress the pubkey was derived from."},
                        {RPCResult::Type::STR, "path", /*optional=*/true, "keypath of the address."},
                        {RPCResult::Type::STR, "error", /*optional=*/true, "Set if unexpected error occurs."},
                        {RPCResult::Type::BOOL, "isondevice", /*optional=*/true, "True if address requires a hardware device to sign for."},
                        {RPCResult::Type::BOOL, "isstakeonly", /*optional=*/true, "True if address is in stakeonly encoding."},
                    }
                },
                RPCExamples{
                    HelpExampleCli("getaddressinfo", "\"" + EXAMPLE_ADDRESS[0] + "\"") +
                    HelpExampleRpc("getaddressinfo", "\"" + EXAMPLE_ADDRESS[0] + "\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LOCK(pwallet->cs_wallet);


    UniValue ret(UniValue::VOBJ);
    std::string error_msg;
    std::string s = request.params[0].get_str();
    bool fBech32 = bech32::Decode(s).data.size() > 0;
    bool is_stake_only_version = false;
    CTxDestination dest = DecodeDestination(s, error_msg);
    if (fBech32 && !IsValidDestination(dest)) {
        dest = DecodeDestination(s, true);
        is_stake_only_version = true;
    }

    // Make sure the destination is valid
    if (!IsValidDestination(dest)) {
        // Set generic error message in case 'DecodeDestination' didn't set it
        if (error_msg.empty()) error_msg = "Invalid address";

        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, error_msg);
    }

    std::string currentAddress = EncodeDestination(dest, fBech32, is_stake_only_version);
    ret.pushKV("address", currentAddress);

    CScript scriptPubKey = GetScriptForDestination(dest);
    ret.pushKV("scriptPubKey", HexStr(scriptPubKey));

    std::unique_ptr<SigningProvider> provider = pwallet->GetSolvingProvider(scriptPubKey);

    isminetype mine = ISMINE_NO;
    if (IsParticlWallet(pwallet.get())) {
        const CHDWallet *phdw = GetParticlWallet(pwallet.get());
        LOCK_ASSERTION(phdw->cs_wallet);
        if (dest.index() == DI::_CExtPubKey) {
            CExtPubKey ek = std::get<CExtPubKey>(dest);
            CKeyID id = ek.GetID();
            mine = phdw->HaveExtKey(id);
        } else
        if (dest.index() == DI::_CStealthAddress) {
            const CStealthAddress &sxAddr = std::get<CStealthAddress>(dest);
            ret.pushKV("scan_public_key", HexStr(sxAddr.scan_pubkey));
            ret.pushKV("spend_public_key", HexStr(sxAddr.spend_pubkey));
            const CExtKeyAccount *pa = nullptr;
            const CEKAStealthKey *pask = nullptr;
            mine = phdw->IsMine(sxAddr, pa, pask);
            if (pa && pask) {
                ret.pushKV("account", pa->GetIDString58());
                CStoredExtKey *sek = pa->GetChain(pask->nScanParent);
                std::string sPath;
                if (sek) {
                    std::vector<uint32_t> vPath;
                    AppendChainPath(sek, vPath);
                    vPath.push_back(pask->nScanKey);
                    PathToString(vPath, sPath);
                    ret.pushKV("scan_path", sPath);
                }
                sek = pa->GetChain(pask->akSpend.nParent);
                if (sek) {
                    std::vector<uint32_t> vPath;
                    AppendChainPath(sek, vPath);
                    vPath.push_back(pask->akSpend.nKey);
                    PathToString(vPath, sPath);
                    ret.pushKV("spend_path", sPath);
                }
            }
        } else
        if (dest.index() == DI::_PKHash ||
            dest.index() == DI::_CKeyID256 ||
            dest.index() == DI::_WitnessV0KeyHash) {
            CKeyID idk;
            const CEKAKey *pak = nullptr;
            const CEKASCKey *pasc = nullptr;
            CExtKeyAccount *pa = nullptr;
            bool isInvalid = false;
            mine = phdw->IsMine(scriptPubKey, idk, pak, pasc, pa, isInvalid);

            if (pa && pak) {
                CStoredExtKey *sek = pa->GetChain(pak->nParent);
                if (sek) {
                    ret.pushKV("from_ext_address_id", sek->GetIDString58());
                    std::string sPath;
                    std::vector<uint32_t> vPath;
                    AppendChainPath(sek, vPath);
                    vPath.push_back(pak->nKey);
                    PathToString(vPath, sPath);
                    ret.pushKV("path", sPath);
                } else {
                    ret.pushKV("error", "Unknown chain.");
                }
            } else
            if (dest.index() == DI::_PKHash) {
                CStealthAddress sx;
                idk = ToKeyID(std::get<PKHash>(dest));
                if (phdw->GetStealthLinked(idk, sx)) {
                    ret.pushKV("from_stealth_address", sx.Encoded());
                }
            }
        } else {
            mine = phdw->IsMine(dest);
        }
        if (mine & ISMINE_HARDWARE_DEVICE) {
            ret.pushKV("isondevice", true);
        }
    } else {
        mine = pwallet->IsMine(dest);
    }

    ret.pushKV("ismine", bool(mine & ISMINE_SPENDABLE));

    if (provider) {
        auto inferred = InferDescriptor(scriptPubKey, *provider);
        bool solvable = inferred->IsSolvable();
        ret.pushKV("solvable", solvable);
        if (solvable) {
            ret.pushKV("desc", inferred->ToString());
        }
    } else {
        ret.pushKV("solvable", false);
    }

    const auto& spk_mans = pwallet->GetScriptPubKeyMans(scriptPubKey);
    // In most cases there is only one matching ScriptPubKey manager and we can't resolve ambiguity in a better way
    ScriptPubKeyMan* spk_man{nullptr};
    if (spk_mans.size()) spk_man = *spk_mans.begin();

    DescriptorScriptPubKeyMan* desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man);
    if (desc_spk_man) {
        std::string desc_str;
        if (desc_spk_man->GetDescriptorString(desc_str, /*priv=*/false)) {
            ret.pushKV("parent_desc", desc_str);
        }
    }

    ret.pushKV("iswatchonly", bool(mine & ISMINE_WATCH_ONLY));
    if (is_stake_only_version) {
        ret.pushKV("isstakeonly", true);
    }

    UniValue detail = DescribeWalletAddress(*pwallet, dest);
    ret.pushKVs(detail);

    ret.pushKV("ischange", ScriptIsChange(*pwallet, scriptPubKey));

    if (spk_man) {
        if (const std::unique_ptr<CKeyMetadata> meta = spk_man->GetMetadata(dest)) {
            ret.pushKV("timestamp", meta->nCreateTime);
            if (meta->has_key_origin) {
                ret.pushKV("hdkeypath", WriteHDKeypath(meta->key_origin.path));
                ret.pushKV("hdseedid", meta->hd_seed_id.GetHex());
                ret.pushKV("hdmasterfingerprint", HexStr(meta->key_origin.fingerprint));
            }
        }
    }

    // Return a `labels` array containing the label associated with the address,
    // equivalent to the `label` field above. Currently only one label can be
    // associated with an address, but we return an array so the API remains
    // stable if we allow multiple labels to be associated with an address in
    // the future.
    UniValue labels(UniValue::VARR);
    const auto* address_book_entry = pwallet->FindAddressBookEntry(dest);
    if (address_book_entry) {
        labels.push_back(address_book_entry->GetLabel());
    }
    ret.pushKV("labels", std::move(labels));

    return ret;
},
    };
}

RPCHelpMan getaddressesbylabel()
{
    return RPCHelpMan{"getaddressesbylabel",
                "\nReturns the list of addresses assigned the specified label.\n",
                {
                    {"label", RPCArg::Type::STR, RPCArg::Optional::NO, "The label."},
                },
                RPCResult{
                    RPCResult::Type::OBJ_DYN, "", "json object with addresses as keys",
                    {
                        {RPCResult::Type::OBJ, "address", "json object with information about address",
                        {
                            {RPCResult::Type::STR, "purpose", "Purpose of address (\"send\" for sending address, \"receive\" for receiving address)"},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("getaddressesbylabel", "\"tabby\"")
            + HelpExampleRpc("getaddressesbylabel", "\"tabby\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LOCK(pwallet->cs_wallet);

    std::string label = LabelFromValue(request.params[0]);

    // Find all addresses that have the given label
    UniValue ret(UniValue::VOBJ);
    std::set<std::string> addresses;
    pwallet->ForEachAddrBookEntry([&](const CTxDestination& _dest, const std::string& _label, const std::string& _purpose, bool _is_change) {
        if (_is_change) return;
        if (_label == label) {
            std::string address = EncodeDestination(_dest);
            // CWallet::m_address_book is not expected to contain duplicate
            // address strings, but build a separate set as a precaution just in
            // case it does.
            bool unique = addresses.emplace(address).second;
            CHECK_NONFATAL(unique);
            // UniValue::pushKV checks if the key exists in O(N)
            // and since duplicate addresses are unexpected (checked with
            // std::set in O(log(N))), UniValue::__pushKV is used instead,
            // which currently is O(1).
            UniValue value(UniValue::VOBJ);
            value.pushKV("purpose", _purpose);
            ret.__pushKV(address, value);
        }
    });

    if (ret.empty()) {
        throw JSONRPCError(RPC_WALLET_INVALID_LABEL_NAME, std::string("No addresses with label " + label));
    }

    return ret;
},
    };
}

RPCHelpMan listlabels()
{
    return RPCHelpMan{"listlabels",
                "\nReturns the list of all labels, or labels that are assigned to addresses with a specific purpose.\n",
                {
                    {"purpose", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "Address purpose to list labels for ('send','receive'). An empty string is the same as not providing this argument."},
                },
                RPCResult{
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::STR, "label", "Label name"},
                    }
                },
                RPCExamples{
            "\nList all labels\n"
            + HelpExampleCli("listlabels", "") +
            "\nList labels that have receiving addresses\n"
            + HelpExampleCli("listlabels", "receive") +
            "\nList labels that have sending addresses\n"
            + HelpExampleCli("listlabels", "send") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("listlabels", "receive")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LOCK(pwallet->cs_wallet);

    std::string purpose;
    if (!request.params[0].isNull()) {
        purpose = request.params[0].get_str();
    }

    // Add to a set to sort by label name, then insert into Univalue array
    std::set<std::string> label_set = pwallet->ListAddrBookLabels(purpose);

    UniValue ret(UniValue::VARR);
    for (const std::string& name : label_set) {
        ret.push_back(name);
    }

    return ret;
},
    };
}


#ifdef ENABLE_EXTERNAL_SIGNER
RPCHelpMan walletdisplayaddress()
{
    return RPCHelpMan{
        "walletdisplayaddress",
        "Display address on an external signer for verification.",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "particl address to display"},
        },
        RPCResult{
            RPCResult::Type::OBJ,"","",
            {
                {RPCResult::Type::STR, "address", "The address as confirmed by the signer"},
            }
        },
        RPCExamples{""},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
            if (!wallet) return UniValue::VNULL;
            CWallet* const pwallet = wallet.get();

            LOCK(pwallet->cs_wallet);

            CTxDestination dest = DecodeDestination(request.params[0].get_str());

            // Make sure the destination is valid
            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
            }

            if (!pwallet->DisplayAddress(dest)) {
                throw JSONRPCError(RPC_MISC_ERROR, "Failed to display address");
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("address", request.params[0].get_str());
            return result;
        }
    };
}
#endif // ENABLE_EXTERNAL_SIGNER
} // namespace wallet
