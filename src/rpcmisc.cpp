// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "init.h"
#include "main.h"
#include "net.h"
#include "netbase.h"
#include "rpcserver.h"
#include "util.h"
#include "stealth.h"
#include "spork.h"
#ifdef ENABLE_WALLET
#include "wallet.h"
//#include "walletdb.h"
#endif

#include <stdint.h>
//#include "standard.h"

#include <univalue.h>

#include <boost/assign/list_of.hpp>

//#include "json/json_spirit_utils.h"
//#include "json/json_spirit_value.h"

using namespace std;
using namespace boost;
using namespace boost::assign;
//using namespace json_spirit;

extern UniValue ValueFromAmount(const CAmount& amount);
//{
//    bool sign = amount < 0;
//    int64_t n_abs = (sign ? -amount : amount);
//    int64_t quotient = n_abs / COIN;
//    int64_t remainder = n_abs % COIN;
//    return UniValue(UniValue::VNUM,
//            strprintf("%s%d.%08d", sign ? "-" : ",", quotient, remainder));
//}


#ifdef ENABLE_WALLET
class DescribeAddressVisitor : public boost::static_visitor<UniValue>
{
private:
    isminetype mine;

public:
    DescribeAddressVisitor(isminetype mineIn) : mine(mineIn) {}

    UniValue operator()(const CNoDestination &dest) const { return UniValue(UniValue::VOBJ); }

    UniValue operator()(const CKeyID &keyID) const {
        UniValue obj(UniValue::VOBJ);
        CPubKey vchPubKey;
        obj.push_back(Pair("isscript", false));
        if (mine == ISMINE_SPENDABLE) {
            pwalletMain->GetPubKey(keyID, vchPubKey);
            obj.push_back(Pair("pubkey", HexStr(vchPubKey)));
            obj.push_back(Pair("iscompressed", vchPubKey.IsCompressed()));
        }
        return obj;
    }

    UniValue operator()(const CScriptID &scriptID) const {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("isscript", true));
        CScript subscript;
        pwalletMain->GetCScript(scriptID, subscript);
        std::vector<CTxDestination> addresses;
        txnouttype whichType;
        int nRequired;
        ExtractDestinations(subscript, whichType, addresses, nRequired);
        obj.push_back(Pair("script", GetTxnOutputType(whichType)));
        obj.push_back(Pair("hex", HexStr(subscript.begin(), subscript.end())));
        UniValue a(UniValue::VARR);
        BOOST_FOREACH (const CTxDestination& addr, addresses)
            a.push_back(CBitcoinAddress(addr).ToString());
        obj.push_back(Pair("addresses", a));
        if (whichType == TX_MULTISIG)
            obj.push_back(Pair("sigsrequired", nRequired));
        return obj;
    }
};
#endif

extern int64_t nWalletUnlockTime;

UniValue getinfo ( const UniValue& params, bool fHelp )
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getinfo\n"
            "Returns an object containing various state info.");

LogPrintf("RGP Debug getinfo start \n");

    proxyType proxy;
    GetProxy(NET_IPV4, proxy);

    UniValue obj(UniValue::VOBJ);
    UniValue diff(UniValue::VOBJ);

LogPrintf("RGP Debug getinfo debug 001 \n");

    obj.push_back(Pair("version",       FormatFullVersion()));
    obj.push_back(Pair("protocolversion",(int)PROTOCOL_VERSION));
    
LogPrintf("RGP Debug getinfo debug 002 \n");

#ifdef ENABLE_WALLET
    if (pwalletMain) 
    {
       obj.push_back(Pair("walletversion", pwalletMain->GetVersion()));
       obj.push_back(Pair("balance",       ValueFromAmount(pwalletMain->GetBalance())));
       if(!fLiteMode)
           obj.push_back(Pair("darksend_balance", ValueFromAmount(pwalletMain->GetAnonymizedBalance())));
	   obj.push_back(Pair("newmint",       ValueFromAmount(pwalletMain->GetNewMint())));
          obj.push_back(Pair("lastreward",   ValueFromAmount(pindexBest->nLastReward)));
       obj.push_back(Pair("stake",         ValueFromAmount(pwalletMain->GetStake())));
   }
#endif

LogPrintf("RGP Debug getinfo 001 \n");

#ifndef LOWMEM
    obj.push_back(Pair("moneysupply",   ValueFromAmount(pindexBest->nMoneySupply)));
#endif

   nBestHeight = pindexBest->nHeight;
   
   printf("RGP Debug getinfo bestheight is %d \n", nBestHeight );
   
   obj.push_back(Pair("blocks",        (int)nBestHeight));   
    obj.push_back(Pair("timeoffset",    (int64_t)GetTimeOffset()));
   obj.push_back(Pair("connections",   (int)vNodes.size()));
   obj.push_back(Pair("proxy",         (proxy.IsValid() ? proxy.proxy.ToStringIPPort() : string())));
//	obj.push_back(Pair("proxy",         (proxy.first.IsValid() ? proxy.first.ToStringIPPort() : string())));
   obj.push_back(Pair("ip",            GetLocalAddress(NULL).ToStringIP()));

   diff.push_back(Pair("proof-of-work",  GetDifficulty()));
    diff.push_back(Pair("proof-of-stake", GetDifficulty(GetLastBlockIndex(pindexBest, true))));
    obj.push_back(Pair("difficulty",    GetDifficulty(GetLastBlockIndex(pindexBest, true))));

   obj.push_back(Pair("testnet",       TestNet()));
#ifdef ENABLE_WALLET
    if (pwalletMain) {
       obj.push_back(Pair("keypoololdest", (int64_t)pwalletMain->GetOldestKeyPoolTime()));
       obj.push_back(Pair("keypoolsize",   (int)pwalletMain->GetKeyPoolSize()));
    }
    
	obj.push_back(Pair("paytxfee",      ValueFromAmount(nTransactionFee)));
	obj.push_back(Pair("mininput",      ValueFromAmount(nMinimumInputValue)));
   if (pwalletMain && pwalletMain->IsCrypted())
       obj.push_back(Pair("unlocked_until", (int64_t)nWalletUnlockTime));
#endif

	obj.push_back(Pair("errors",        GetWarnings("statusbar")));

LogPrintf("RGP Debug getinfo exit \n");
    
    return obj;
}

UniValue validateaddress(const UniValue& params, bool fHelp)
{

#ifdef ENABLE_WALLET
    // LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
    //LOCK2(cs_main, &pwalletMain->cs_wallet);
#else
    LOCK(cs_main);
#endif

    CBitcoinAddress address(params[0].get_str());
    bool isValid = address.IsValid();

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("isvalid", isValid));
    
    if (isValid) {
        CTxDestination dest = address.Get();
        string currentAddress = address.ToString();
        ret.push_back(Pair("address", currentAddress));
        CScript scriptPubKey = GetScriptForDestination(dest);
        ret.push_back(Pair("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end())));

#ifdef ENABLE_WALLET
        isminetype mine = pwalletMain ? IsMine(*pwalletMain, dest) : ISMINE_NO;
        ret.push_back(Pair("ismine", bool(mine & ISMINE_SPENDABLE)));
        ret.push_back(Pair("iswatchonly", bool(mine & ISMINE_WATCH_ONLY)));
        
        //UniValue detail = boost::apply_visitor(DescribeAddressVisitor(mine), dest);
        //ret.pushKVs(detail);
 //       if (pwalletMain && pwalletMain->mapAddressBook.count(dest))
 //           ret.push_back(Pair("account", pwalletMain->mapAddressBook[dest].name));
            
        if (pwalletMain && pwalletMain->mapAddressBook.count(dest))
            ret.push_back(Pair("account", pwalletMain->mapAddressBook[dest]));            
            
#endif
    }
    return ret;
}


UniValue validatepubkey(const UniValue& params, bool fHelp)
{
    if (fHelp || !params.size() || params.size() > 2)
        throw runtime_error(
            "validatepubkey <SocietyGpubkey>\n"
            "Return information about <SocietyGpubkey>.");

    std::vector<unsigned char> vchPubKey = ParseHex(params[0].get_str());
    CPubKey pubKey(vchPubKey);

    bool isValid = pubKey.IsValid();
    bool isCompressed = pubKey.IsCompressed();
    CKeyID keyID = pubKey.GetID();

    CSocietyGcoinAddress address;
    address.Set(keyID);

    //Object ret;
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("isvalid", isValid));
    if (isValid)
    {
        CTxDestination dest = address.Get();
        string currentAddress = address.ToString();
        ret.push_back(Pair("address", currentAddress));
        ret.push_back(Pair("iscompressed", isCompressed));
#ifdef ENABLE_WALLET
        isminetype mine = pwalletMain ? IsMine(*pwalletMain, dest) : ISMINE_NO;
        ret.push_back(Pair("ismine", (mine & ISMINE_SPENDABLE) ? true : false));
        if (mine != ISMINE_NO) {
            ret.push_back(Pair("iswatchonly", (mine & ISMINE_WATCH_ONLY) ? true: false));
            //UniValue detail = boost::apply_visitor(DescribeAddressVisitor(mine), dest);
           
            //ret.insert(ret.end(), detail.begin(), detail.end());
        }
        if (pwalletMain && pwalletMain->mapAddressBook.count(dest))
            ret.push_back(Pair("account", pwalletMain->mapAddressBook[dest]));
#endif
    }
    return ret;
}

UniValue verifymessage(const UniValue& params, bool fHelp )
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "verifymessage <SocietyGaddress> <signature> <message>\n"
            "Verify a signed message");

    string strAddress  = params[0].get_str();
    string strSign     = params[1].get_str();
    string strMessage  = params[2].get_str();

    CSocietyGcoinAddress addr(strAddress);
    if (!addr.IsValid())
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");

    CKeyID keyID;
    if (!addr.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");

    bool fInvalid = false;
    vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;

    return (pubkey.GetID() == keyID);
}

/*
    Used for updating/reading spork settings on the network
*/

UniValue spork(const UniValue& params, bool fHelp )
{
    if(params.size() == 1 && params[0].get_str() == "show"){
        std::map<int, CSporkMessage>::iterator it = mapSporksActive.begin();

        //Object ret;
        UniValue ret(UniValue::VOBJ);
        while(it != mapSporksActive.end()) {
            ret.push_back(Pair(sporkManager.GetSporkNameByID(it->second.nSporkID), it->second.nValue));
            it++;
        }
        return ret;
    } else if (params.size() == 2){
        int nSporkID = sporkManager.GetSporkIDByName(params[0].get_str());
        if(nSporkID == -1){
            return "Invalid spork name";
        }

        // SPORK VALUE
        int64_t nValue = params[1].get_int();

        //broadcast new spork
        if(sporkManager.UpdateSpork(nSporkID, nValue)){
            return "success";
        } else {
            return "failure";
        }

    }

    
}

