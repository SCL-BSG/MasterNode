// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _BITCOINRPC_SERVER_H_
#define _BITCOINRPC_SERVER_H_ 1

#include "uint256.h"
#include "rpcprotocol.h"

#include <list>
#include <map>
#include <boost/function.hpp>
#include <univalue.h>

class CRPCCommand;

namespace RPCServer
{
    void OnStarted(boost::function<void ()> slot);
    void OnStopped(boost::function<void ()> slot);
    void OnPreCommand(boost::function<void (const CRPCCommand&)> slot);
    void OnPostCommand(boost::function<void (const CRPCCommand&)> slot);
}

class CBlockIndex;
class CNetAddr;


class JSONRequest
{
public:
    UniValue id;
    std::string strMethod;
    UniValue params;

    JSONRequest() { id = NullUniValue; }
    void parse(const UniValue& valRequest);
};

/** Query whether RPC is running */
bool IsRPCRunning();

/**
 * Set the RPC warmup status.  When this is done, all RPC calls will error out
 * immediately with RPC_IN_WARMUP.
 */
void SetRPCWarmupStatus(const std::string& newStatus);
/* Mark warmup as done.  RPC calls will be processed from now on.  */
void SetRPCWarmupFinished();

/* returns the current warmup state.  */
bool RPCIsInWarmup(std::string* statusOut);

/**
 * Type-check arguments; throws JSONRPCError if wrong type given. Does not check that
 * the right number of arguments are passed, just that any passed are the correct type.
 * Use like:  RPCTypeCheck(params, boost::assign::list_of(str_type)(int_type)(obj_type));
 */
void RPCTypeCheck(const UniValue& params,
                  const std::list<UniValue::VType>& typesExpected, bool fAllowNull=false);

/**
 * Check for expected keys/value types in an Object.
 * Use like: RPCTypeCheckObj(object, boost::assign::map_list_of("name", str_type)("value", int_type));
 */
void RPCTypeCheckObj(const UniValue& o,
                  const std::map<std::string, UniValue::VType>& typesExpected, bool fAllowNull=false);

/** Opaque base class for timers returned by NewTimerFunc.
 * This provides no methods at the moment, but makes sure that delete
 * cleans up the whole state.
 */
class RPCTimerBase
{
public:
    virtual ~RPCTimerBase() {}
};

/**
* RPC timer "driver".
 */
class RPCTimerInterface
{
public:
    virtual ~RPCTimerInterface() {}
    /** Implementation name */
    virtual const char *Name() = 0;
    /** Factory function for timers.
     * RPC will call the function to create a timer that will call func in *millis* milliseconds.
     * @note As the RPC mechanism is backend-neutral, it can use different implementations of timers.
     * This is needed to cope with the case in which there is no HTTP server, but
     * only GUI RPC console, and to break the dependency of pcserver on httprpc.
     */
    virtual RPCTimerBase* NewTimer(boost::function<void(void)>& func, int64_t millis) = 0;
};

/** Set factory function for timers */
void RPCSetTimerInterface(RPCTimerInterface *iface);
/** Set factory function for timers, but only if unset */
void RPCSetTimerInterfaceIfUnset(RPCTimerInterface *iface);
/** Unset factory function for timers */
void RPCUnsetTimerInterface(RPCTimerInterface *iface);

/**
 * Run func nSeconds from now.
 * Overrides previous timer <name> (if any).
 */
void RPCRunLater(const std::string& name, boost::function<void(void)> func, int64_t nSeconds);

typedef UniValue(*rpcfn_type)(const UniValue& params, bool fHelp);

class CRPCCommand
{
public:
    std::string category;
    std::string name;
    rpcfn_type actor;
    bool okSafeMode;
    bool threadSafe;
    bool reqWallet;
};

/**
 * Myce RPC command dispatcher.
 */
class CRPCTable
{
private:
    std::map<std::string, const CRPCCommand*> mapCommands;

public:
    CRPCTable();
    const CRPCCommand* operator[](const std::string& name) const;
    std::string help(std::string name) const;

    /**
     * Execute a method.
     * @param method   Method to execute
     * @param params   UniValue Array of arguments (JSON objects)
     * @returns Result of the call.
     * @throws an exception (UniValue) when an error happens.
     */
    UniValue execute(const std::string &method, const UniValue &params) const;

    /**
    * Returns a list of registered commands
    * @returns List of registered commands.
    */
    std::vector<std::string> listCommands() const;
};

extern const CRPCTable tableRPC;


// Myce
//extern std::string HelpRequiringPassphrase();
extern std::string HelpExampleCli(std::string methodname, std::string args);
extern std::string HelpExampleRpc(std::string methodname, std::string args);
// Myce
/**
 * Utilities: convert hex-encoded Values
 * (throws error if not hex).
 */
//
// Utilities: convert hex-encoded Values
// (throws error if not hex).
//

extern uint256 ParseHashV(const UniValue& v, std::string strName);
extern uint256 ParseHashO(const UniValue& o, std::string strKey);
extern std::vector<unsigned char> ParseHexV(const UniValue& v, std::string strName);
extern std::vector<unsigned char> ParseHexO(const UniValue& o, std::string strKey);
extern int ParseInt(const UniValue& o, std::string strKey);
extern bool ParseBool(const UniValue& o, std::string strKey);



// RGP added to reference external in main.cpp
extern double GetDifficulty(const CBlockIndex* blockindex = NULL);
extern double GetPoWMHashPS();


extern UniValue getconnectioncount(const UniValue& params, bool fHelp); // in rpcnet.cpp
extern UniValue getpeerinfo(const UniValue& params, bool fHelp);
extern UniValue ping(const UniValue& params, bool fHelp);
extern UniValue addnode(const UniValue& params, bool fHelp);
extern UniValue disconnectnode(const UniValue& params, bool fHelp);
extern UniValue getaddednodeinfo(const UniValue& params, bool fHelp);
extern UniValue getnettotals(const UniValue& params, bool fHelp);
extern UniValue setban(const UniValue& params, bool fHelp);
extern UniValue listbanned(const UniValue& params, bool fHelp);
extern UniValue clearbanned(const UniValue& params, bool fHelp);




/* Firewall General Session Settings */
extern UniValue firewallstatus(const UniValue& params, bool fHelp);
extern UniValue firewallenabled(const UniValue& params, bool fHelp);
extern UniValue firewallclearblacklist(const UniValue& params, bool fHelp);
extern UniValue firewallclearbanlist(const UniValue& params, bool fHelp);
extern UniValue firewalladdtowhitelist(const UniValue& params, bool fHelp);
extern UniValue firewalladdtoblacklist(const UniValue& params, bool fHelp);

// *** Firewall Debug (Live Output) ***
extern UniValue firewalldebug(const UniValue& params, bool fHelp);
extern UniValue firewalldebugexam(const UniValue& params, bool fHelp);
extern UniValue firewalldebugbans(const UniValue& params, bool fHeRPCNotifyBlockChangelp);
extern UniValue firewalldebugblacklist(const UniValue& params, bool fHelp);
extern UniValue firewalldebugdisconnect(const UniValue& params, bool fHelp);
extern UniValue firewalldebugbandwidthabuse(const UniValue& params, bool fHelp);
extern UniValue firewalldebugnofalsepositivebandwidthabuse(const UniValue& params, bool fHelp);
extern UniValue firewalldebuginvalidwallet(const UniValue& params, bool fHelp);
extern UniValue firewalldebugforkedwallet(const UniValue& params, bool fHelp);
extern UniValue firewalldebugfloodingwallet(const UniValue& params, bool fHelp);

// * Firewall Settings (Exam) *
extern UniValue firewalltraffictolerance(const UniValue& params, bool fHelp);
extern UniValue firewalltrafficzone(const UniValue& params, bool fHelp);

/* Firewall BandwidthAbuse Session Settings */
extern UniValue firewalldetectbandwidthabuse(const UniValue& params, bool fHelp);
extern UniValue firewallblacklistbandwidthabuse(const UniValue& params, bool fHelp);
extern UniValue firewallbanbandwidthabuse(const UniValue& params, bool fHelp);
extern UniValue firewallnofalsepositivebandwidthabuse(const UniValue& params, bool fHelp);
extern UniValue firewallbantimebandwidthabuse(const UniValue& params, bool fHelp);
extern UniValue firewallbandwidthabusemaxcheck(const UniValue& params, bool fHelp);
extern UniValue firewallbandwidthabuseminattack(const UniValue& params, bool fHelp);
extern UniValue firewallbandwidthabusemaxattack(const UniValue& params, bool fHelp);

/* Firewall Invalid Wallet Session Settings */
extern UniValue firewalldetectinvalidwallet(const UniValue& params, bool fHelp);
extern UniValue firewallblacklistinvalidwallet(const UniValue& params, bool fHelp);
extern UniValue firewallbaninvalidwallet(const UniValue& params, bool fHelp);
extern UniValue firewallbantimeinvalidwallet(const UniValue& params, bool fHelp);
extern UniValue firewallinvalidwalletminprotocol(const UniValue& params, bool fHelp);
extern UniValue firewallinvalidwalletmaxcheck(const UniValue& params, bool fHelp);

/* Firewall Forked Wallet Session Settings */
extern UniValue firewalldetectforkedwallet(const UniValue& params, bool fHelp);
extern UniValue firewallblacklistforkedwallet(const UniValue& params, bool fHelp);
extern UniValue firewallbanforkedwallet(const UniValue& params, bool fHelp);
extern UniValue firewallbantimeforkedwallet(const UniValue& params, bool fHelp);
extern UniValue firewallforkedwalletnodeheight(const UniValue& params, bool fHelp);

/* Firewall Flooding Wallet Session Settings */
extern UniValue firewalldetectfloodingwallet(const UniValue& params, bool fHelp);
extern UniValue firewallblacklistfloodingwallet(const UniValue& params, bool fHelp);
extern UniValue firewallbanfloodingwallet(const UniValue& params, bool fHelp);
extern UniValue firewallbantimefloodingwallet(const UniValue& params, bool fHelp);
extern UniValue firewallfloodingwalletminbytes(const UniValue& params, bool fHelp);
extern UniValue firewallfloodingwalletmaxbytes(const UniValue& params, bool fHelp);
extern UniValue firewallfloodingwalletattackpatternadd(const UniValue& params, bool fHelp);
extern UniValue firewallfloodingwalletattackpatternremove(const UniValue& params, bool fHelp);
extern UniValue firewallfloodingwalletmintrafficavg(const UniValue& params, bool fHelp);
extern UniValue firewallfloodingwalletmaxtrafficavg(const UniValue& params, bool fHelp);
extern UniValue firewallfloodingwalletmincheck(const UniValue& params, bool fHelp);
extern UniValue firewallfloodingwalletmaxcheck(const UniValue& params, bool fHelp);


extern UniValue addnode(const UniValue& params, bool fHelp);
extern UniValue getaddednodeinfo(const UniValue& params, bool fHelp);
extern UniValue getnettotals(const UniValue& params, bool fHelp);

extern UniValue dumpwallet(const UniValue& params, bool fHelp);
extern UniValue importwallet(const UniValue& params, bool fHelp);
extern UniValue importaddress(const UniValue& params, bool fHelp);
extern UniValue dumpprivkey(const UniValue& params, bool fHelp); // in rpcdump.cpp
extern UniValue importprivkey(const UniValue& params, bool fHelp);

extern UniValue sendalert(const UniValue& params, bool fHelp);

extern UniValue getsubsidy(const UniValue& params, bool fHelp);
extern UniValue getstakesubsidy(const UniValue& params, bool fHelp);
extern UniValue getmininginfo(const UniValue& params, bool fHelp);
extern UniValue getstakinginfo(const UniValue& params, bool fHelp);
extern UniValue checkkernel(const UniValue& params, bool fHelp);
extern UniValue getwork(const UniValue& params, bool fHelp);
extern UniValue getworkex(const UniValue& params, bool fHelp);
extern UniValue getblocktemplate(const UniValue& params, bool fHelp);
extern UniValue submitblock(const UniValue& params, bool fHelp);

extern UniValue getnewaddress(const UniValue& params, bool fHelp); // in rpcwallet.cpp
extern UniValue getaccountaddress(const UniValue& params, bool fHelp);
extern UniValue setaccount(const UniValue& params, bool fHelp);
extern UniValue getaccount(const UniValue& params, bool fHelp);
extern UniValue getaddressesbyaccount(const UniValue& params, bool fHelp);
extern UniValue sendtoaddress(const UniValue& params, bool fHelp);
extern UniValue signmessage(const UniValue& params, bool fHelp);
extern UniValue verifymessage(const UniValue& params, bool fHelp);
extern UniValue getreceivedbyaddress(const UniValue& params, bool fHelp);
extern UniValue getreceivedbyaccount(const UniValue& params, bool fHelp);
extern UniValue getbalance(const UniValue& params, bool fHelp);
extern UniValue movecmd(const UniValue& params, bool fHelp);
extern UniValue sendfrom(const UniValue& params, bool fHelp);
extern UniValue sendmany(const UniValue& params, bool fHelp);
extern UniValue addmultisigaddress(const UniValue& params, bool fHelp);
extern UniValue addredeemscript(const UniValue& params, bool fHelp);
extern UniValue listreceivedbyaddress(const UniValue& params, bool fHelp);
extern UniValue listreceivedbyaccount(const UniValue& params, bool fHelp);
extern UniValue listtransactions(const UniValue& params, bool fHelp);
extern UniValue listaddressgroupings(const UniValue& params, bool fHelp);
extern UniValue listaccounts(const UniValue& params, bool fHelp);
extern UniValue listsinceblock(const UniValue& params, bool fHelp);
extern UniValue gettransaction(const UniValue& params, bool fHelp);
extern UniValue backupwallet(const UniValue& params, bool fHelp);
extern UniValue keypoolrefill(const UniValue& params, bool fHelp);
extern UniValue walletpassphrase(const UniValue& params, bool fHelp);
extern UniValue walletpassphrasechange(const UniValue& params, bool fHelp);
extern UniValue walletlock(const UniValue& params, bool fHelp);
extern UniValue encryptwallet(const UniValue& params, bool fHelp);
extern UniValue validateaddress(const UniValue& params, bool fHelp);
extern UniValue getinfo(const UniValue& params, bool fHelp);
extern UniValue reservebalance(const UniValue& params, bool fHelp);
extern UniValue addmultisigaddress(const UniValue& params, bool fHelp);
extern UniValue createmultisig(const UniValue& params, bool fHelp);
extern UniValue checkwallet(const UniValue& params, bool fHelp);
extern UniValue repairwallet(const UniValue& params, bool fHelp);
extern UniValue resendtx(const UniValue& params, bool fHelp);
extern UniValue makekeypair(const UniValue& params, bool fHelp);
extern UniValue validatepubkey(const UniValue& params, bool fHelp);
extern UniValue getnewpubkey(const UniValue& params, bool fHelp);

extern UniValue getrawtransaction(const UniValue& params, bool fHelp); // in rcprawtransaction.cpp
extern UniValue searchrawtransactions(const UniValue& params, bool fHelp);

extern UniValue listunspent(const UniValue& params, bool fHelp);
extern UniValue createrawtransaction(const UniValue& params, bool fHelp);
extern UniValue decoderawtransaction(const UniValue& params, bool fHelp);
extern UniValue decodescript(const UniValue& params, bool fHelp);
extern UniValue signrawtransaction(const UniValue& params, bool fHelp);
extern UniValue sendrawtransaction(const UniValue& params, bool fHelp);

extern UniValue getbestblockhash(const UniValue& params, bool fHelp); // in rpcblockchain.cpp
extern UniValue getblockcount(const UniValue& params, bool fHelp); // in rpcblockchain.cpp
extern UniValue getdifficulty(const UniValue& params, bool fHelp);
extern UniValue settxfee(const UniValue& params, bool fHelp);
extern UniValue getrawmempool(const UniValue& params, bool fHelp);
extern UniValue getblockhash(const UniValue& params, bool fHelp);
extern UniValue getblock(const UniValue& params, bool fHelp);
extern UniValue getblockbynumber(const UniValue& params, bool fHelp);
extern UniValue getcheckpoint(const UniValue& params, bool fHelp);
extern UniValue getblockheader(const UniValue& params, bool fHelp);
extern UniValue getchaintips(const UniValue& params, bool fHelp);
extern UniValue getfeeinfo(const UniValue& params, bool fHelp);
extern UniValue getmempoolinfo(const UniValue& params, bool fHelp);
extern UniValue gettxoutsetinfo(const UniValue& params, bool fHelp);
extern UniValue gettxout(const UniValue& params, bool fHelp);
extern UniValue verifychain(const UniValue& params, bool fHelp);
extern UniValue getchaintips(const UniValue& params, bool fHelp);
extern UniValue invalidateblock(const UniValue& params, bool fHelp);
extern UniValue reconsiderblock(const UniValue& params, bool fHelp);
extern UniValue getaccumulatorvalues(const UniValue& params, bool fHelp);
extern UniValue findserial(const UniValue& params, bool fHelp); // in rpcblockchain.cpp
extern UniValue getserials(const UniValue& params, bool fHelp);
extern UniValue getnetworkhashps(const UniValue& params, bool fHelp);
extern UniValue prioritisetransaction(const UniValue& params, bool fHelp);

extern UniValue getgenerate(const UniValue& params, bool fHelp); // in rpcmining.cpp
extern UniValue setgenerate(const UniValue& params, bool fHelp);
extern UniValue gethashespersec(const UniValue& params, bool fHelp);
extern UniValue prioritisetransaction(const UniValue& params, bool fHelp);
extern UniValue getblocktemplate(const UniValue& params, bool fHelp);
extern UniValue submitblock(const UniValue& params, bool fHelp);
extern UniValue estimatefee(const UniValue& params, bool fHelp);
extern UniValue estimatepriority(const UniValue& params, bool fHelp);


extern UniValue setmocktime(const UniValue& params, bool fHelp);
extern UniValue getrawchangeaddress(const UniValue& params, bool fHelp);
extern UniValue setstakesplitthreshold(const UniValue& params, bool fHelp);
extern UniValue getstakesplitthreshold(const UniValue& params, bool fHelp);

extern UniValue getunconfirmedbalance(const UniValue& params, bool fHelp);

extern UniValue getpoolinfo(const UniValue& params, bool fHelp); // in rpcmasternode.cpp
extern UniValue masternode(const UniValue& params, bool fHelp);
extern UniValue listmasternodes(const UniValue& params, bool fHelp);
extern UniValue getmasternodecount(const UniValue& params, bool fHelp);
extern UniValue createmasternodebroadcast(const UniValue& params, bool fHelp);
extern UniValue decodemasternodebroadcast(const UniValue& params, bool fHelp);
extern UniValue relaymasternodebroadcast(const UniValue& params, bool fHelp);
extern UniValue masternodeconnect(const UniValue& params, bool fHelp);
extern UniValue masternodecurrent(const UniValue& params, bool fHelp);
extern UniValue masternodedebug(const UniValue& params, bool fHelp);
extern UniValue startmasternode(const UniValue& params, bool fHelp);
extern UniValue createmasternodekey(const UniValue& params, bool fHelp);
extern UniValue getmasternodeoutputs(const UniValue& params, bool fHelp);
extern UniValue listmasternodeconf(const UniValue& params, bool fHelp);
extern UniValue getmasternodestatus(const UniValue& params, bool fHelp);
extern UniValue getmasternodewinners(const UniValue& params, bool fHelp);
extern UniValue getmasternodescores(const UniValue& params, bool fHelp);

extern UniValue mnbudget(const UniValue& params, bool fHelp); // in rpcmasternode-budget.cpp
extern UniValue preparebudget(const UniValue& params, bool fHelp);
extern UniValue submitbudget(const UniValue& params, bool fHelp);
extern UniValue mnbudgetvote(const UniValue& params, bool fHelp);
extern UniValue getbudgetvotes(const UniValue& params, bool fHelp);
extern UniValue getnextsuperblock(const UniValue& params, bool fHelp);
extern UniValue getbudgetprojection(const UniValue& params, bool fHelp);
extern UniValue getbudgetinfo(const UniValue& params, bool fHelp);
extern UniValue mnbudgetrawvote(const UniValue& params, bool fHelp);
extern UniValue mnfinalbudget(const UniValue& params, bool fHelp);
extern UniValue checkbudgets(const UniValue& params, bool fHelp);
extern UniValue mnsync(const UniValue& params, bool fHelp);
extern UniValue autocombinerewards(const UniValue& params, bool fHelp);

extern UniValue getwalletinfo(const UniValue& params, bool fHelp);
extern UniValue listunspent(const UniValue& params, bool fHelp);
extern UniValue lockunspent(const UniValue& params, bool fHelp);
extern UniValue listlockunspent(const UniValue& params, bool fHelp);
extern UniValue createrawtransaction(const UniValue& params, bool fHelp);
extern UniValue decoderawtransaction(const UniValue& params, bool fHelp);
extern UniValue decodescript(const UniValue& params, bool fHelp);
extern UniValue signrawtransaction(const UniValue& params, bool fHelp);
extern UniValue sendrawtransaction(const UniValue& params, bool fHelp);
extern UniValue getstakingstatus(const UniValue& params, bool fHelp);
extern UniValue multisend(const UniValue& params, bool fHelp);
extern UniValue burncoins(const UniValue& params, bool fHelp);
extern UniValue sendtoaddressix(const UniValue& params, bool fHelp);
extern UniValue getzerocoinbalance(const UniValue& params, bool fHelp);
extern UniValue listmintedzerocoins(const UniValue& params, bool fHelp);
extern UniValue listspentzerocoins(const UniValue& params, bool fHelp);
extern UniValue listzerocoinamounts(const UniValue& params, bool fHelp);
extern UniValue mintzerocoin(const UniValue& params, bool fHelp);
extern UniValue spendzerocoin(const UniValue& params, bool fHelp);
extern UniValue spendrawzerocoin(const UniValue& params, bool fHelp);
extern UniValue resetmintzerocoin(const UniValue& params, bool fHelp);
extern UniValue resetspentzerocoin(const UniValue& params, bool fHelp);
extern UniValue getarchivedzerocoin(const UniValue& params, bool fHelp);
extern UniValue importzerocoins(const UniValue& params, bool fHelp);
extern UniValue exportzerocoins(const UniValue& params, bool fHelp);
extern UniValue reconsiderzerocoins(const UniValue& params, bool fHelp);
extern UniValue getspentzerocoinamount(const UniValue& params, bool fHelp);
extern UniValue setzyceseed(const UniValue& params, bool fHelp);
extern UniValue getzyceseed(const UniValue& params, bool fHelp);
extern UniValue generatemintlist(const UniValue& params, bool fHelp);
extern UniValue searchdzyce(const UniValue& params, bool fHelp);
extern UniValue dzycestate(const UniValue& params, bool fHelp);



/* ---------------------
   -- RGP JIRA BSG-51 --
   -----------------------------------------------------
   -- added getnetworkinfo to list of server commands --
   ----------------------------------------------------- */

extern UniValue getnetworkinfo(const UniValue& params, bool fHelp);
extern UniValue getblockchaininfo(const UniValue& params, bool fHelp);
extern UniValue estimatefee(const UniValue& params, bool fHelp);
extern UniValue estimatepriority(const UniValue& params, bool fHelp);

extern UniValue getnewstealthaddress(const UniValue& params, bool fHelp);
extern UniValue liststealthaddresses(const UniValue& params, bool fHelp);
extern UniValue importstealthaddress(const UniValue& params, bool fHelp);
extern UniValue sendtostealthaddress(const UniValue& params, bool fHelp);
extern UniValue scanforalltxns(const UniValue& params, bool fHelp);
extern UniValue scanforstealthtxns(const UniValue& params, bool fHelp);

extern UniValue darksend(const UniValue& params, bool fHelp);
extern UniValue spork(const UniValue& params, bool fHelp);
extern UniValue masternode(const UniValue& params, bool fHelp);
extern UniValue masternodelist(const UniValue& params, bool fHelp);

extern UniValue smsgenable(const UniValue& params, bool fHelp);
extern UniValue smsgdisable(const UniValue& params, bool fHelp);
extern UniValue smsglocalkeys(const UniValue& params, bool fHelp);
extern UniValue smsgoptions(const UniValue& params, bool fHelp);
extern UniValue smsgscanchain(const UniValue& params, bool fHelp);
extern UniValue smsgscanbuckets(const UniValue& params, bool fHelp);
extern UniValue smsgaddkey(const UniValue& params, bool fHelp);
extern UniValue smsggetpubkey(const UniValue& params, bool fHelp);
extern UniValue smsgsend(const UniValue& params, bool fHelp);
extern UniValue smsgsendanon(const UniValue& params, bool fHelp);
extern UniValue smsginbox(const UniValue& params, bool fHelp);
extern UniValue smsgoutbox(const UniValue& params, bool fHelp);
extern UniValue smsgbuckets(const UniValue& params, bool fHelp);


bool StartRPC();
void InterruptRPC();
void StopRPC();

void InitRPCMining();
void ShutdownRPCMining();

std::string JSONRPCExecBatch(const UniValue& vReq);
void RPCNotifyBlockChange(const uint256 nHeight);

#endif  // _BITCOINRPC_SERVER_H_

