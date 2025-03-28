// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Copyright (c) 2018-2023 Bank Society Gold developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/* --------------------------------------------
   -- JIRA BSG-10 OpenSSL 1.1.1g implemented --
   -------------------------------------------- */

#include "rpcprotocol.h"

#include <openssl/err.h>
#include <openssl/crypto.h>


#include "util.h"

#include <stdint.h>


#include "tinyformat.h"
//#include "utilstrencodings.h"

#include "version.h"

#include <fstream>


#include <boost/algorithm/string.hpp>
//#include <boost/asio.hpp>
//#include <boost/asio/ssl.hpp>
//#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
//#include <boost/foreach.hpp>args
//#include <boost/iostreams/concepts.hpp>
//#include <boost/iostreams/stream.hpp>
//#include <boost/lexical_cast.hpp>
//#include <boost/shared_ptr.hpp>
//#include "json/json_spirit_writer_template.h"

using namespace std;


//using namespace json_spirit;

// Number of bytes to allocate and read at most at once in post data
const size_t POST_READ_SIZE = 256 * 1024;


/**
 * JSON-RPC protocol.  BSG speaks version 1.0 for maximum compatibility,
 * but uses JSON-RPC 1.1/2.0 standards for parts of the 1.0 standard that were
 * unspecified (HTTP errors and contents of 'error').
 *
 * 1.0 spec: http://json-rpc.org/wiki/specification
 * 1.2 spec: http://jsonrpc.org/historical/json-rpc-over-http.html
 * http://www.codeproject.com/KB/recipes/JSON_Spirit.aspx
 */


UniValue JSONRPCRequestObj(const std::string& strMethod, const UniValue& params, const UniValue& id)
{
    UniValue request(UniValue::VOBJ);
    request.pushKV("method", strMethod);
    request.pushKV("params", params);
    request.pushKV("id", id);
    return request;
}

string JSONRPCRequest(const string& strMethod, const UniValue& params, const UniValue& id)
{

printf("RGP RPCProtocol.cpp JSONRPCRequest %s \n", &strMethod[0] );

    UniValue request(UniValue::VOBJ);
    request.push_back( Pair ("method", strMethod));
    request.push_back( Pair ("params", params));
    request.push_back( Pair ("id", id));
    return request.write() + "\n";
}



UniValue JSONRPCReplyObj(const UniValue& result, const UniValue& error, const UniValue& id)
{
    UniValue reply(UniValue::VOBJ);
    
    if (!error.isNull())
    {
        reply.push_back(Pair("result", NullUniValue));
    }
    else
    {
        reply.push_back(Pair("result", result));
    }
    
    reply.push_back(Pair("error", error));
    reply.push_back(Pair("id", id));

    return reply;
}

string JSONRPCReply(const UniValue& result, const UniValue& error, const UniValue& id)
{

    UniValue reply = JSONRPCReplyObj(result, error, id);
      
    return reply.write() + "\n";
}

UniValue JSONRPCError(int code, const string& message)
{
    UniValue error(UniValue::VOBJ);
    error.push_back(Pair("code", code));
    error.push_back(Pair("message", message));
    return error;
}

/** Username used when cookie authentication is in use (arbitrary, only for
 * recognizability in debugging/logging purposes)
 */
static const std::string COOKIEAUTH_USER = "__cookie__";
/** Default name for auth cookie file */
static const std::string COOKIEAUTH_FILE = ".cookie";

boost::filesystem::path GetAuthCookieFile()
{
    boost::filesystem::path path(GetArg("rpccookiefile", COOKIEAUTH_FILE));
    if (!path.is_complete()) path = GetDataDir() / path;
    return path;
}

bool GenerateAuthCookie(std::string *cookie_out)
{
    unsigned char rand_pwd[32];
    GetRandBytes(rand_pwd, 32);
    std::string cookie = COOKIEAUTH_USER + ":" + EncodeBase64(&rand_pwd[0],32);

    /** the umask determines what permissions are used to create this file -
     * these are set to 077 in init.cpp unless overridden with -sysperms.
     */
    std::ofstream file;
    boost::filesystem::path filepath = GetAuthCookieFile();
    file.open(filepath.string().c_str());
    if (!file.is_open()) {
        LogPrintf("Unable to open cookie authentication file %s for writing\n", filepath.string());
        return false;
    }
    file << cookie;
    file.close();
    LogPrintf("Generated RPC authentication cookie %s\n", filepath.string());

    if (cookie_out)
        *cookie_out = cookie;
    return true;
}

bool GetAuthCookie(std::string *cookie_out)
{
    std::ifstream file;
    std::string cookie;
    boost::filesystem::path filepath = GetAuthCookieFile();
    file.open(filepath.string().c_str());
    if (!file.is_open())
        return false;
    std::getline(file, cookie);
    file.close();

    if (cookie_out)
        *cookie_out = cookie;
    return true;
}

void DeleteAuthCookie()
{
    try {
        boost::filesystem::remove(GetAuthCookieFile());
    } catch (const boost::filesystem::filesystem_error& e) {
        LogPrintf("%s: Unable to remove random auth cookie file: %s\n", __func__, e.what());
    }
}

/* RGP from Raptoreum */
std::vector<UniValue> JSONRPCProcessBatchReply(const UniValue &in, size_t num)
{
    if (!in.isArray()) {
        throw std::runtime_error("Batch must be an array");
    }
    std::vector<UniValue> batch(num);
    for (size_t i=0; i<in.size(); ++i) {
        const UniValue &rec = in[i];
        if (!rec.isObject()) {
            throw std::runtime_error("Batch member must be object");
        }
        size_t id = rec["id"].get_int();
        if (id >= num) {
            throw std::runtime_error("Batch member id larger than size");
        }
        batch[id] = rec;
    }
    return batch;
}





