// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Copyright (c) 2018 Profit Hunters Coin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <set>
#include "rpcclient.h"

#include "rpcprotocol.h"
#include "util.h"
#include "ui_interface.h"
#include "chainparams.h" // for Params().RPCPort()

#include <stdint.h>

#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>
#include "json/json_spirit_writer_template.h"

using namespace std;
using namespace boost;
using namespace boost::asio;
using namespace json_spirit;

class CRPCConvertParam
{
public:
    std::string methodName; //! method whose params want conversion
    int paramIdx;           //! 0-based idx of param to convert
    std::string paramName;  //!< parameter name
};


// ***TODO*** RGP Needs Optimisation
static const CRPCConvertParam vRPCConvertParams[] =
    {
        {"stop", 0, "stop"},
        {"setmocktime", 0, "setmocktime"},
        {"getaddednodeinfo", 0, "getaddednodeinfo" },
        {"setgenerate", 0, "setgenerate"},
        {"setgenerate", 1, "setgenerate"},
        {"getnetworkhashps", 0, ""},
        {"getnetworkhashps", 1, ""},
        {"sendtoaddress", 1, ""},
        {"sendtoaddressix", 1, ""},
        {"burncoins", 0, ""},
        {"settxfee", 0, ""},
        {"getreceivedbyaddress", 1, ""},
        {"getreceivedbyaccount", 1, ""},
        {"listreceivedbyaddress", 0, ""},
        {"listreceivedbyaddress", 1, ""},
        {"listreceivedbyaddress", 2, ""},
        {"listreceivedbyaccount", 0, ""},
        {"listreceivedbyaccount", 1, ""},
        {"listreceivedbyaccount", 2, ""},
        {"getbalance", 1, ""},
        {"getbalance", 2, ""},
        {"getblockhash", 0, ""},
        {"move", 2, ""},
        {"move", 3, ""},
        {"sendfrom", 2, ""},
        {"sendfrom", 3, ""},
        {"listtransactions", 1, ""},
        {"listtransactions", 2, ""},
        {"listtransactions", 3, ""},
        {"listaccounts", 0, ""},
        {"listaccounts", 1, ""},
        {"walletpassphrase", 1, ""},
        {"walletpassphrase", 2, ""},
        {"getblocktemplate", 0, ""},
        {"listsinceblock", 1, ""},
        {"listsinceblock", 2, ""},
        {"sendmany", 1, ""},
        {"sendmany", 2, ""},
        {"addmultisigaddress", 0, ""},
        {"addmultisigaddress", 1, ""},
        {"createmultsig", 0, ""},
        {"createmultisig", 1, ""},
        {"listunspent", 0, ""},
        {"listunspent", 1, ""},
        {"listunspent", 2, ""},
        {"listunspent", 3, ""},
        {"getblock", 1, ""},
        {"getblockheader", 1, ""},
        {"gettransaction", 1, ""},
        {"getrawtransaction", 1, ""},
        {"createrawtransaction", 0, ""},
        {"createrawtransaction", 1, ""},
        {"createrawtransaction", 2, ""},
        {"signrawtransaction", 1, ""},
        {"signrawtransaction", 2, ""},
        {"sendrawtransaction", 1, ""},
        {"sendrawtransaction", 2, ""},
        {"gettxout", 1, ""},
        {"gettxout", 2, ""},
        {"lockunspent", 0, ""},
        {"lockunspent", 1, ""},
        {"importprivkey", 2, ""},
        {"importaddress", 2, ""},
        {"verifychain", 0, ""},
        {"verifychain", 1, ""},
        {"keypoolrefill", 0, ""},
        {"getrawmempool", 0, ""},
        {"estimatefee", 0, ""},
        {"estimatepriority", 0, ""},
        {"prioritisetransaction", 1, ""},
        {"prioritisetransaction", 2, ""},
        {"setban", 2, ""},
        {"setban", 3, ""},
        {"spork", 1, ""},
        {"mnbudget", 3, ""},
        {"mnbudget", 4, ""},
        {"mnbudget", 6, ""},
        {"mnbudget", 8, ""},
        {"preparebudget", 2, ""},
        {"preparebudget", 3, ""},
        {"preparebudget", 5, ""},
        {"submitbudget", 2, ""},
        {"submitbudget", 3, ""},
        {"submitbudget", 5, ""},
        {"submitbudget", 7, ""},
        {"mnvoteraw", 1, ""},
        {"mnvoteraw", 4, ""},
        {"reservebalance", 0, ""},
        {"reservebalance", 1, ""},
        {"setstakesplitthreshold", 0, ""},
        {"autocombinerewards", 0, ""},
        {"autocombinerewards", 1, ""},
        {"getzerocoinbalance", 0, ""},
        {"listmintedzerocoins", 0, ""},
        {"listspentzerocoins", 0, ""},
        {"listzerocoinamounts", 0, ""},
        {"mintzerocoin", 0, ""},
        {"mintzerocoin", 1, ""},
        {"spendzerocoin", 0, ""},
        {"spendzerocoin", 1, ""},
        {"spendzerocoin", 2, ""},
        {"spendzerocoin", 3, ""},
        {"spendrawzerocoin", 2, ""},
        {"importzerocoins", 0, ""},
        {"exportzerocoins", 0, ""},
        {"exportzerocoins", 1, ""},
        {"resetmintzerocoin", 0, ""},
        {"getspentzerocoinamount", 1, ""},
        {"generatemintlist", 0, ""},
        {"generatemintlist", 1, ""},
        {"searchdzyce", 0, ""},
        {"searchdzyce", 1, ""},
        {"searchdzyce", 2, ""},
        {"getaccumulatorvalues", 0, ""},
        {"getserials", 0, ""},
        {"getserials", 1, ""},
        {"getserials", 2, ""},
        {"getfeeinfo", 0, ""},   
        {"getreceivedbyaddress", 1, ""},
        {"getreceivedbyaccount", 1, ""},
        {"getblock", 1, ""},
        {"getblockbynumber", 0, ""},
        {"getblockbynumber", 1, ""},
        {"getinfo", 0, "getinfo" },
        {"sendalert", 2, ""},
        {"sendalert", 3, ""},
        {"sendalert", 4, ""},
        {"sendalert", 5, ""},
        {"sendalert", 6, ""},
        {"sendmany", 1, ""},
        {"sendmany", 2, ""},
        {"reservebalance", 0, ""},
        {"reservebalance", 1, ""},
        {"keypoolrefill", 0, ""},
    	{ "importprivkey", 2, ""},
    	{ "importaddress", 2, ""},
    	{ "checkkernel", 0, ""},
    	{ "checkkernel", 1, ""},
    	{ "setban", 2, ""},
    	{ "setban", 3, ""},
    	{ "sendtostealthaddress", 1, ""},
    	{ "firewallenabled", 1, ""},
    	{ "firewallstatus", 0, ""},
    	{ "firewallclearblacklist", 1, ""},
    	{ "firewallclearbanlist", 1, ""},
    	{ "firewalltraffictolerance", 1, ""},
    	{ "firewalltrafficzone", 1, ""},
    	{ "firewalldebug", 1, ""},
    	{ "firewalldebugexam", 1, ""},
    	{ "firewalldebugbans", 1, ""},
    	{ "firewalldebugblacklist", 1, ""},
    	{ "firewalldebugdisconnect", 1, ""},
    	{ "firewalldebugbandwidthabuse", 1, ""},
    	{ "firewalldebugnofalsepositivebandwidthabuse", 1, ""},
    	{ "firewalldebuginvalidwallet", 1, ""},
    	{ "firewalldebugfloodingwallet", 1, ""},
    	{ "firewalldetectbandwidthabuse", 1, ""},
    	{ "firewallblacklistbandwidthabuse", 1, ""},
    	{ "firewallbanbandwidthabuse", 1, ""},
    	{ "firewallnofalsepositivebandwidthabuse", 1, ""},
    	{ "firewallbantimebandwidthabuse", 1, ""},
    	{ "firewallbandwidthabusemaxcheck", 1, ""},
    	{ "firewallbandwidthabuseminattack", 1, ""},
    	{ "firewallbandwidthabusemaxattack", 1, ""},
    	{ "firewalldetectinvalidwallet", 1, ""},
    	{ "firewallblacklistinvalidwallet", 1, ""},
    	{ "firewallbaninvalidwallet", 1, ""},
    	{ "firewallbantimeinvalidwallet", 1, ""},
    	{ "firewallinvalidwalletminprotocol", 1, ""},
    	{ "firewallinvalidwalletmaxcheck", 1, ""},
    	{ "firewalldetectforkedwallet", 1, ""},
    	{ "firewallblacklistforkedwallet", 1, ""},
    	{ "firewallbanforkedwallet", 1, ""},
    	{ "firewallbantimeforkedwallet", 1, ""},
    	{ "firewalldetectfloodingwallet", 1, ""},
    	{ "firewallblacklistfloodingwallet", 1, ""},
    	{ "firewallbanfloodingwallet", 1, ""},
    	{ "firewallbantimefloodingwallet", 1, ""},
    	{ "firewallfloodingwalletminbytes", 1, ""},
    	{ "firewallfloodingwalletmaxbytes", 1, ""},
    	{ "firewallfloodingwalletattackpatternadd", 1, ""},
    	{ "firewallfloodingwalletattackpatternremove", 1, ""},
    	{ "firewallfloodingwalletmintrafficavg", 1, ""},
    	{ "firewallfloodingwalletmaxtrafficavg", 1, ""},
    	{ "firewallfloodingwalletmincheck", 1, ""},
    	{ "firewallfloodingwalletmaxcheck", 1, ""},
	
   };



class CRPCConvertTable
{
private:
    std::set<std::pair<std::string, int>> members;
    std::set<std::pair<std::string, std::string>> membersByName;

public:
    CRPCConvertTable();

    bool convert(const std::string& method, int idx) 
    {
    
        return (members.count(std::make_pair(method, idx)) > 0);
        
    }

    bool convert(const std::string& method, const std::string& name) 
    {

        return (membersByName.count(std::make_pair(method, name)) > 0);

    }
    
};

CRPCConvertTable::CRPCConvertTable()
{
    const unsigned int n_elem =
        (sizeof(vRPCConvertParams) / sizeof(vRPCConvertParams[0]));
        

    for (unsigned int i = 0; i < n_elem; i++) {
        members.insert(std::make_pair(vRPCConvertParams[i].methodName,
                                      vRPCConvertParams[i].paramIdx));
        membersByName.insert(std::make_pair(vRPCConvertParams[i].methodName,
                                            vRPCConvertParams[i].paramName));
    }
}

static CRPCConvertTable rpcCvtTable;

/** Non-RFC4627 JSON parser, accepts internal values (such as numbers, true, false, null)
 * as well as objects and arrays.
 */
UniValue ParseNonRFCJSONValue(const std::string& strVal)
{

//RGP bsg_cli evhttp_request_new() resonse was not null 
    UniValue jVal;
    if (!jVal.read(std::string("[")+strVal+std::string("]")) ||
        !jVal.isArray() || jVal.size()!=1)
        throw runtime_error(string("Error parsing JSON:")+strVal);
    return jVal[0];
}

/** Convert strings to command-specific RPC representation */
UniValue RPCConvertValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{

    UniValue params(UniValue::VARR);

    for (unsigned int idx = 0; idx < strParams.size(); idx++) 
    {
        const std::string& strVal = strParams[idx];

        if (!rpcCvtTable.convert(strMethod, idx)) 
        {
            // insert string value directly
            params.push_back(strVal);
        } 
        else
        {
            // parse string as JSON, insert bool/number/object/etc. value
            params.push_back(ParseNonRFCJSONValue(strVal));
        }
    }

    return params;
}


UniValue RPCConvertNamedValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{
    UniValue params(UniValue::VOBJ);

    for (const std::string &s: strParams) {
        size_t pos = s.find('=');
        if (pos == std::string::npos) {
            throw(std::runtime_error("No '=' in named argument '"+s+"', this needs to be present for every argument (even if it is empty)"));
        }

        std::string name = s.substr(0, pos);
        std::string value = s.substr(pos+1);

        if (!rpcCvtTable.convert(strMethod, name)) 
        {
            // insert string value directly
            params.pushKV(name, value);
        } 
        else
         {
            // parse string as JSON, insert bool/number/object/etc. value
            params.pushKV(name, ParseNonRFCJSONValue(value));
        }
    }

    return params;
}


