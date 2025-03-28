// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2018 Profit Hunters Coin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "rpcserver.h"
#include "chainparams.h"
#include "main.h"
#include "db.h"
#include "txdb.h"
#include "init.h"
#include "miner.h"
#include "kernel.h"

#include <boost/assign/list_of.hpp>

#include <string>     // std::string, std::stoi

#include <univalue.h>
//using namespace json_spirit;
using namespace std;
using namespace boost::assign;


// Key used by getwork/getblocktemplate miners.
// Allocated in InitRPCMining, free'd in ShutdownRPCMining
static CReserveKey* pMiningKey = NULL;
extern double GetPoSKernelPS();

void InitRPCMining()
{
    if (!pwalletMain)
    {
        return;
    }

LogPrintf("RGP Debug InitRPCMining \n");

    // getwork/getblocktemplate mining rewards paid here:
    pMiningKey = new CReserveKey(pwalletMain);
}


void ShutdownRPCMining()
{
    if (!pMiningKey)
    {
        return;
    }

    delete pMiningKey; pMiningKey = NULL;
}


UniValue getsubsidy(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
    {
        throw runtime_error("getsubsidy [nTarget]\n"
                            "Returns proof-of-work subsidy value for the specified value of target.");
    }

    return (int64_t)GetProofOfStakeReward(pindexBest->pprev, 0, 0);
}


UniValue getstakesubsidy(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
    {
        throw runtime_error("getstakesubsidy <hex string>\n"
                            "Returns proof-of-stake subsidy value for the specified coinstake.");
    }

    //RPCTypeCheck(params, list_of(str_type));
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

    vector<unsigned char> txData(ParseHex(params[0].get_str()));
    
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    CTransaction tx;
    
    try
    {
        ssData >> tx;
    }
    catch (std::exception &e)
    {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    uint64_t nCoinAge;
    CTxDB txdb("r");

    if (!tx.GetCoinAge(txdb, pindexBest, nCoinAge))
    {
        throw JSONRPCError(RPC_MISC_ERROR, "GetCoinAge failed");
    }

    return (uint64_t)GetProofOfStakeReward(pindexBest->pprev, nCoinAge, 0);
}


UniValue getgenerate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getgenerate\n"
            "Returns true or false.");

    if (!pMiningKey)
        return false;

    return GetBoolArg("gen", false);
}


UniValue setgenerate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "setgenerate <generate> [genproclimit] <debugtoconsole>\n"
            "<generate> is true or false to turn generation on or off.\n"
            "[genproclimit] CPU threads to use for mining\n"
            "<debugtoconsole> optional to display live debug to unix/dos console (default: false)"
            "Generation is limited to [genproclimit] processors, -1 is unlimited.");

    bool fGenerate = true;
    bool fDebugConsoleOutputMining = false;
    
    if (params.size() > 0)
    {
        if (params[0].get_str() == "true")
        {
            fGenerate = true;
        }
        else
        {
            fGenerate = false;
        }
    }
    
    if (params.size() > 1)
    {
        int nGenProcLimit = std::stoi(params[1].get_str());

        mapArgs["genproclimit"] = itostr(nGenProcLimit);

        if (nGenProcLimit == 0)
        {
            fGenerate = false;
        }
    }

    if (params.size() > 2)
    {
        if (params[2].get_str() == "true")
        {
            fDebugConsoleOutputMining = true;
        }
    }

    mapArgs["gen"] = (fGenerate ? "1" : "0");

    assert(pwalletMain != NULL);
    
    GeneratePoWcoins(fGenerate, pwalletMain, fDebugConsoleOutputMining);

    return UniValue::VNULL;
}

/*
UniValue gethashespersec(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "gethashespersec\n"
            "Returns a recent hashes per second performance measurement while generating.");

    if (GetTimeMillis() - nHPSTimerStart > 8000)
        return (boost::int64_t)0;
    return (boost::int64_t)dHashesPerSec;
}
*/

UniValue getmininginfo(const UniValue& params, bool fHelp)
{
int64_t Time_to_Last_block, Time_in_since_Last_Block, last_recorded_block_time, time_to_sleep, after_stake_success_timeout;
int64_t Last_known_block_time;

    if (fHelp || params.size() != 0)
    {
        throw runtime_error(
            "getmininginfo\n"
            "Returns an object containing mining-related information.");
    }

    uint64_t nWeight = 0;
    
    //Last_known_block_time = pindexBest->GetBlockTime();  // RGP, wait if necessary for next block, then start mining

   //LogPrintf("Debug getmininginfo time %d \n", GetTime() - Last_known_block_time );

   //if ( GetTime() - Last_known_block_time > 300 )
   //{
   // LogPrintf("Debug getmininginfo MINE \n" );
   //    /* mine */ 
  // } 
  // else 
  // {
  //     return -1;
  // }


    if (pwalletMain)
    {
        nWeight = pwalletMain->GetStakeWeight();
    }

    UniValue obj(UniValue::VOBJ);
    UniValue diff(UniValue::VOBJ);
    UniValue weight(UniValue::VOBJ);

    //Object obj, diff, weight;
    obj.push_back(Pair("blocks",                (int)nBestHeight));
    obj.push_back(Pair("currentblocksize",      (uint64_t)nLastBlockSize));
    obj.push_back(Pair("currentblocktx",        (uint64_t)nLastBlockTx));

    diff.push_back(Pair("proof-of-work",        GetDifficulty()));
    diff.push_back(Pair("proof-of-stake",       GetDifficulty(GetLastBlockIndex(pindexBest, true))));
    diff.push_back(Pair("search-interval",      (int)nLastCoinStakeSearchInterval));
    obj.push_back(Pair("difficulty",            GetDifficulty(GetLastBlockIndex(pindexBest, true))));

    obj.push_back(Pair("generate",              GetBoolArg("gen", false)));
    obj.push_back(Pair("genproclimit",          (int)GetArg("genproclimit", -1)));
    //obj.push_back(Pair("hashespersec",        gethashespersec(params, false)));

    obj.push_back(Pair("blockvalue",            (int64_t)GetProofOfStakeReward(pindexBest->pprev, 0, 0)));
    obj.push_back(Pair("netmhashps",            GetPoWMHashPS()));
    obj.push_back(Pair("netstakeweight",        GetPoSKernelPS()));
    obj.push_back(Pair("errors",                GetWarnings("statusbar")));
    obj.push_back(Pair("pooledtx",              (uint64_t)mempool.size()));

    weight.push_back(Pair("minimum",            (uint64_t)nWeight));
    weight.push_back(Pair("maximum",            (uint64_t)0));
    weight.push_back(Pair("combined",           (uint64_t)nWeight));
    obj.push_back(Pair("stakeweight",           weight));

    obj.push_back(Pair("testnet",               TestNet()));
    
    return obj;
}

/* ------------------------
   -- RGP - JIRA BSG-181 --
   -----------------------------------------------------------------------------------
   -- The function GetPoSKernelPS() returns double, but the code was using uint64_t --
   -- for nNetworkWeight.                                                           --
   ----------------------------------------------------------------------------------- */


UniValue getstakinginfo(const UniValue& params, bool fHelp)
{
double nNetworkWeight;

    if (fHelp || params.size() != 0)
    {
        throw runtime_error("getstakinginfo\n"
                            "Returns an object containing staking-related information.");
    }

    LogPrintf("*** RGP getstakinginfo start \n");

    uint64_t nWeight = 0;
    uint64_t nExpectedTime = 0;
    
    if (pwalletMain)
    {
        nWeight = pwalletMain->GetStakeWeight();

        LogPrintf("*** RGP getstakinginfo stake weight %d \n", nWeight );

    }

    nNetworkWeight = GetPoSKernelPS();
    
    LogPrintf("*** RGP getstakinginfo GetPoSKernelPS netweight %f \n", nNetworkWeight );

    bool staking = nLastCoinStakeSearchInterval && nWeight;
    
    nExpectedTime = staking ? (TARGET_SPACING * nNetworkWeight / (double) nWeight) : 0;

    UniValue obj(UniValue::VOBJ);
    //Object obj;

    obj.push_back(Pair("enabled",           GetBoolArg("staking", true)));
    obj.push_back(Pair("staking",           staking));
    obj.push_back(Pair("errors",            GetWarnings("statusbar")));

    obj.push_back(Pair("currentblocksize",  (uint64_t)nLastBlockSize));
    obj.push_back(Pair("currentblocktx",    (uint64_t)nLastBlockTx));
    obj.push_back(Pair("pooledtx",          (uint64_t)mempool.size()));

    obj.push_back(Pair("difficulty",        GetDifficulty(GetLastBlockIndex(pindexBest, true))));
    obj.push_back(Pair("search-interval",   (int)nLastCoinStakeSearchInterval));

    obj.push_back(Pair("weight",            (uint64_t)nWeight));
    obj.push_back(Pair("netstakeweight",    (uint64_t)nNetworkWeight));

    obj.push_back(Pair("expectedtime",      nExpectedTime));

    return obj;
}


UniValue checkkernel(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
    {
        throw runtime_error("checkkernel [{\"txid\":txid,\"vout\":n},...] [createblocktemplate=false]\n"
                            "Check if one of given inputs is a kernel input at the moment.\n"
        );
    }

    //RPCTypeCheck(params, list_of(array_type)(bool_type));
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VBOOL));

    UniValue inputs = params[0].get_array();
//    Array inputs = params[0].get_array();
    
    bool fCreateBlockTemplate = params.size() > 1 ? params[1].get_bool() : false;

    if (vNodes.empty())
    {
        throw JSONRPCError(-9, "SocietyG is not connected!");
    }

    if (IsInitialBlockDownload())
    {
        throw JSONRPCError(-10, "SocietyG is downloading blocks...");
    }

    COutPoint kernel;
    CBlockIndex* pindexPrev = pindexBest;
    
    unsigned int nBits = GetNextTargetRequired(pindexPrev, true);
    
    int64_t nTime = GetAdjustedTime();
    
    nTime &= ~STAKE_TIMESTAMP_MASK;

    //UniValue inputs = params[0].get_array();
    
    for (unsigned int idx = 0; idx < inputs.size(); idx++)
    //BOOST_FOREACH(UniValue& input, inputs)
    {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();
        //const Object& o = input.get_obj();

        const UniValue& txid_v = find_value(o, "txid");
        //const UniValue& txid_v = find_value(o, "txid");
        
        //if (txid_v.type() != str_type)
        if ( txid_v.type() != UniValue::VSTR )
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing txid key");
        }
        
        string txid = txid_v.get_str();
        
        if (!IsHex(txid))
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");
        }
        
	const UniValue& vout_v = find_value(o, "vout");
        //const UniValue& vout_v = find_value(o, "vout");
        
        if ( txid_v.type() != UniValue::VSTR )
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        }
        
        int nOutput = vout_v.get_int();
        
        if (nOutput < 0)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");
        }

        COutPoint cInput(uint256(txid), nOutput);
        
        if (CheckKernel(pindexPrev, nBits, nTime, cInput))
        {
            kernel = cInput;

            break;
        }
    }

    UniValue result(UniValue::VOBJ);
    //Object result;
    result.push_back(Pair("found", !kernel.IsNull()));

    if (kernel.IsNull())
    {
        return result;
    }

    UniValue oKernel(UniValue::VOBJ);
    //Object oKernel;
    oKernel.push_back(Pair("txid", kernel.hash.GetHex()));
    oKernel.push_back(Pair("vout", (int64_t)kernel.n));
    oKernel.push_back(Pair("time", nTime));
    result.push_back(Pair("kernel", oKernel));

    if (!fCreateBlockTemplate)
    {
        return result;
    }

    int64_t nFees;


    /* RGP, replaced auto_ptr with unique_ptr */
    /* auto_ptr<CBlock> pblock(CreateNewBlock(*pMiningKey, true, &nFees)); */
    unique_ptr<CBlock> pblock(CreateNewBlock(*pMiningKey, true, &nFees));

    pblock->nTime = pblock->vtx[0].nTime = nTime;

    CDataStream ss(SER_DISK, PROTOCOL_VERSION);
    ss << *pblock;

    result.push_back(Pair("blocktemplate", HexStr(ss.begin(), ss.end())));
    result.push_back(Pair("blocktemplatefees", nFees));

    CPubKey pubkey;
    if (!pMiningKey->GetReservedKey(pubkey))
    {
        throw JSONRPCError(RPC_MISC_ERROR, "GetReservedKey failed");
    }

    result.push_back(Pair("blocktemplatesignkey", HexStr(pubkey)));

    return result;
}


UniValue getworkex(const UniValue& params, bool fHelp)
{
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VBOOL));

    
    LogPrintf("*** RGP rpcmining getworkex start \n");

    if (fHelp || params.size() > 2)
    {
        throw runtime_error("getworkex [data, coinbase]\n"
                            "If [data, coinbase] is not specified, returns extended work data.\n"
        );
    }

    if (vNodes.empty())
    {
        throw JSONRPCError(-9, "SocietyG is not connected!");
    }

    if (IsInitialBlockDownload())
    {
        throw JSONRPCError(-10, "SocietyG is downloading blocks...");
    }

    if (pindexBest->nHeight >= Params().LastPOWBlock())
    {
        throw JSONRPCError(RPC_MISC_ERROR, "No more PoW blocks");
    }

    typedef map<uint256, pair<CBlock*, CScript> > mapNewBlock_t;
    static mapNewBlock_t mapNewBlock;
    static vector<CBlock*> vNewBlock;

    if (params.size() == 0)
    {
        // Update block
        static unsigned int nTransactionsUpdatedLast;
        static CBlockIndex* pindexPrev;
        static int64_t nStart;
        static CBlock* pblock;
        
        if (pindexPrev != pindexBest || (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60))
        {
            if (pindexPrev != pindexBest)
            {
                // Deallocate old blocks since they're obsolete now
                mapNewBlock.clear();
                
                BOOST_FOREACH(CBlock* pblock, vNewBlock)
                {
                    delete pblock;
                }
                
                vNewBlock.clear();
            }

            nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
            pindexPrev = pindexBest;
            nStart = GetTime();

            // Create new block
            pblock = CreateNewBlock(*pMiningKey);
            if (!pblock)
            {
                throw JSONRPCError(-7, "Out of memory");
            }

            vNewBlock.push_back(pblock);
        }

        // Update nTime
        pblock->nTime = max(pindexPrev->GetPastTimeLimit()+1, GetAdjustedTime());
        pblock->nNonce = 0;

        // Update nExtraNonce
        static unsigned int nExtraNonce = 0;
        IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

        // Save
        mapNewBlock[pblock->hashMerkleRoot] = make_pair(pblock, pblock->vtx[0].vin[0].scriptSig);

        LogPrintf("*** RGP rpcmining after mapNewBlock\n");

        // Prebuild hash buffers
        char pmidstate[32];
        char pdata[128];
        char phash1[64];
        FormatHashBuffers(pblock, pmidstate, pdata, phash1);

        LogPrintf("*** RGP rpcmining after FormatHashBuffer\n");

        uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();

        CTransaction coinbaseTx = pblock->vtx[0];
        std::vector<uint256> merkle = pblock->GetMerkleBranch(0);

        LogPrintf("*** RGP rpcmining after FormatHashBuffer debug 001\n");

        UniValue result(UniValue::VOBJ);
        //Object result;
        result.push_back(Pair("data",     HexStr(BEGIN(pdata), END(pdata))));
        result.push_back(Pair("target",   HexStr(BEGIN(hashTarget), END(hashTarget))));

        LogPrintf("*** RGP rpcmining after FormatHashBuffer debug 002\n");

        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << coinbaseTx;
        result.push_back(Pair("coinbase", HexStr(ssTx.begin(), ssTx.end())));


        UniValue merkle_arr;

        LogPrintf("*** RGP rpcmining after FormatHashBuffer debug 003\n");

        BOOST_FOREACH(uint256 merkleh, merkle)
        {
            merkle_arr.push_back(HexStr(BEGIN(merkleh), END(merkleh)));
        }

        LogPrintf("*** RGP rpcmining after FormatHashBuffer debug 004\n");

        result.push_back(Pair("merkle", merkle_arr));

        LogPrintf("*** RGP rpcmining after FormatHashBuffer debug 005\n");

        return result;
    }
    else
    {
        // Parse parameters
        vector<unsigned char> vchData = ParseHex(params[0].get_str());
        vector<unsigned char> coinbase;

        if(params.size() == 2)
        {
            coinbase = ParseHex(params[1].get_str());
        }

        if (vchData.size() != 128)
        {
            throw JSONRPCError(-8, "Invalid parameter");
        }

        CBlock* pdata = (CBlock*)&vchData[0];

        // Byte reverse
        for (int i = 0; i < 128/4; i++)
        {
            ((unsigned int*)pdata)[i] = ByteReverse(((unsigned int*)pdata)[i]);
        }

        // Get saved block
        if (!mapNewBlock.count(pdata->hashMerkleRoot))
        {
            return false;
        }

        CBlock* pblock = mapNewBlock[pdata->hashMerkleRoot].first;

        pblock->nTime = pdata->nTime;
        pblock->nNonce = pdata->nNonce;

        if(coinbase.size() == 0)
        {
            pblock->vtx[0].vin[0].scriptSig = mapNewBlock[pdata->hashMerkleRoot].second;
        }
        else
        {
            CDataStream(coinbase, SER_NETWORK, PROTOCOL_VERSION) >> pblock->vtx[0]; // FIXME - HACK!
        }

        pblock->hashMerkleRoot = pblock->BuildMerkleTree();

        assert(pwalletMain != NULL);

        return ProcessBlockFound(pblock, *pwalletMain, *pMiningKey);
    }
}

/* ----------------------------------------------------------------------------- 
   -- RGP, getwork is a JSON-RPC method sent over HTTP transport.             
   --                                                                         --
   --      getwork accepts one optional parameter, this must be provided by a --
   --      prior request.                                                     --
   --                                                                         --
   --      getwork without arguments provides the block header for a miner    --
   --      to find the solution.                                              --
   ----------------------------------------------------------------------------- */

UniValue getwork(const UniValue& params, bool fHelp)
{
bool process_block_found_status;
typedef map<uint256, pair<CBlock*, CScript> > mapNewBlock_t;

    LogPrintf("RGP DEBUG getwork 001 \n");

    if (fHelp || params.size() > 1)
    {

        throw runtime_error("getwork [data]\n"
                            "If [data] is not specified, returns formatted hash data to work on:\n"
                            "  \"midstate\" : precomputed hash state after hashing the first half of the data (DEPRECATED)\n" // deprecated
                            "  \"data\" : block data\n"
                            "  \"hash1\" : formatted hash buffer for second hash (DEPRECATED)\n" // deprecated
                            "  \"target\" : little endian hash target\n"
                            "If [data] is specified, tries to solve the block and returns true if it was successful.");
    }
    else
        LogPrintf("RG DEBUG Param is ZERO \n");

    if (vNodes.empty())
    {
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "SocietyG is not connected!");
    }

    if (IsInitialBlockDownload())
    {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "SocietyG is downloading blocks...");
    }

    if (pindexBest->nHeight >= Params().LastPOWBlock())
    {

        throw JSONRPCError(RPC_MISC_ERROR, "No more PoW blocks");
    }

    

    LogPrintf("RGP DEBUG getwork 004 \n");

    static mapNewBlock_t mapNewBlock;   // FIXME: thread safety
    static vector<CBlock*> vNewBlock;
    bool status;
    
    if (params.size() == 0)
    {
   LogPrintf("RGP DEBUG getwork 005 parameter size is 0 \n");
static CBlockIndex* pindexPrev;
CBlockIndex* pindexTest = pindexBest; /* RGP, use to wait for pindexBest changing */
 //       do 
 //       {
 //          MilliSleep ( 5000 );
 //          LogPrintf("RGP GetWork delating 5 seconds for index to change \n");
 //       } while ( pindexTest == pindexBest );


        // Update block
        static unsigned int nTransactionsUpdatedLast;
        //static CBlockIndex* pindexPrev;
        static int64_t nStart, fees;
        static CBlock* pblock;

        




        if (pindexPrev != pindexBest || (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60))
        {
   LogPrintf("RGP DEBUG getwork 006 \n");
           if (pindexPrev != pindexBest)
           {

                // Deallocate old blocks since they're obsolete now
                mapNewBlock.clear();
                   LogPrintf("RGP DEBUG getwork 007 \n");
                BOOST_FOREACH(CBlock* pblock, vNewBlock)
                {
                   delete pblock;
                }

               vNewBlock.clear();
           }

            // Clear pindexPrev so future getworks make a new block, despite any failures from here on
            pindexPrev = NULL;

            // Store the pindexBest used before CreateNewBlock, to avoid races
            nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
            CBlockIndex* pindexPrevNew = pindexBest;
            
            nStart = GetTime();

            LogPrintf("RGP Debug getwork 006a before CreatNewBlock \n");

            // Create new block
            /* RGP, replaced auto_ptr with unique_ptr */
            /* auto_ptr<CBlock> pblocktemplate(CreateNewBlockWithKey(reservekey, pwallet)); */
            
            //unique_ptr<CBlock> pblocktemplate(CreateNewBlockWithKey(reservekey, pwallet));

            
            pblock = CreateNewBlock( *pMiningKey );
LogPrintf("RGP Debug after CreateNewBlock \n");

            if (!pblock)
            {
LogPrintf("RGP Debug after CreateNewBlock pointer failed \n");
                throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");
            }
            
            vNewBlock.push_back(pblock);

            // Need to update only after we know CreateNewBlock succeeded
            pindexPrev = pindexPrevNew;
        }

   LogPrintf("RGP DEBUG getwork 008 \n");
        // Update nTime
        pblock->UpdateTime(pindexPrev);
        pblock->nNonce = 0;

        // Update nExtraNonce
        static unsigned int nExtraNonce = 0;
        IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

        // Save
        mapNewBlock[pblock->hashMerkleRoot] = make_pair(pblock, pblock->vtx[0].vin[0].scriptSig);

        // Pre-build hash buffers
        char pmidstate[32];
        char pdata[128];
        char phash1[64];
        FormatHashBuffers(pblock, pmidstate, pdata, phash1);

   LogPrintf("RGP DEBUG getwork 009 \n");

        uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();

        UniValue result(UniValue::VOBJ);
        //Object result;
        
        result.push_back(Pair("midstate", HexStr(BEGIN(pmidstate), END(pmidstate)))); // deprecated
        result.push_back(Pair("data",     HexStr(BEGIN(pdata), END(pdata))));
        result.push_back(Pair("hash1",    HexStr(BEGIN(phash1), END(phash1)))); // deprecated
        result.push_back(Pair("target",   HexStr(BEGIN(hashTarget), END(hashTarget))));
  LogPrintf("RGP DEBUG getwork 0010 \n");

        
        /* Result is type UniValue with parameters of midstate, data, hash1 and target 
           This is returned to the miner that used RPC                                   */

        return result;
    }
    else
    {
 LogPrintf("RGP DEBUG getwork 005a parameter size is more than 0 \n");
        // Parse parameters

LogPrintf("RGP DEBUG getwork 005b \n");

        vector<unsigned char> vchData = ParseHex(params[0].get_str());

LogPrintf("RGP DEBUG getwork 005c \n");

LogPrintf("RGP DEBUG getwork 0011 \n");
        if (vchData.size() != 128)
        {

            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
        }
        
        CBlock* pdata = (CBlock*)&vchData[0];

        // Byte reverse
        for (int i = 0; i < 128/4; i++)
        {
            ((unsigned int*)pdata)[i] = ByteReverse(((unsigned int*)pdata)[i]);
        }

        // Get saved block
        if (!mapNewBlock.count(pdata->hashMerkleRoot))
        {

            return false;
        }
        
        CBlock* pblock = mapNewBlock[pdata->hashMerkleRoot].first;

        pblock->nTime = pdata->nTime;
        pblock->nNonce = pdata->nNonce;
        pblock->vtx[0].vin[0].scriptSig = mapNewBlock[pdata->hashMerkleRoot].second;
        pblock->hashMerkleRoot = pblock->BuildMerkleTree();

        assert(pwalletMain != NULL);
        
   LogPrintf("RGP DEBUG getwork 0020 \n");


        process_block_found_status = ProcessBlockFound(pblock, *pwalletMain, *pMiningKey);


        return process_block_found_status;
    }
}


UniValue getblocktemplate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
    {
        throw runtime_error("getblocktemplate [params]\n"
                            "Returns data needed to construct a block to work on:\n"
                            "  \"version\" : block version\n"
                            "  \"previousblockhash\" : hash of current highest block\n"
                            "  \"transactions\" : contents of non-coinbase transactions that should be included in the next block\n"
                            "  \"coinbaseaux\" : data that should be included in coinbase\n"
                            "  \"coinbasevalue\" : maximum allowable input to coinbase transaction, including the generation award and transaction fees\n"
                            "  \"target\" : hash target\n"
                            "  \"mintime\" : minimum timestamp appropriate for next block\n"
                            "  \"curtime\" : current timestamp\n"
                            "  \"mutable\" : list of ways the block template may be changed\n"
                            "  \"noncerange\" : range of valid nonces\n"
                            "  \"sigoplimit\" : limit of sigops in blocks\n"
                            "  \"sizelimit\" : limit of block size\n"
                            "  \"bits\" : compressed target of next block\n"
                            "  \"height\" : height of the next block\n"
                            "  \"payee\" : \"xxx\",                (string) required payee for the next block\n"
                            "  \"payee_amount\" : n,               (numeric) required amount to pay\n"
                            "  \"votes\" : [\n                     (array) show vote candidates\n"
                            "        { ... }                       (json object) vote candidate\n"
                            "        ,...\n"
                            "  ],\n"
                            "  \"masternode_payments\" : true|false,         (boolean) true, if masternode payments are enabled"
                            "  \"enforce_masternode_payments\" : true|false  (boolean) true, if masternode payments are enforced"
                            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.");
    }

    std::string strMode = "template";

    LogPrintf("RGP Debug GetBlockTemplate parameter count %d \n", params.size() );

    if (params.size() > 0)
    {
    
        const UniValue& oparam = params[0].get_obj();
        const UniValue& modeval = find_value(oparam, "mode");
        
        // const Object& oparam = params[0].get_obj();
        //const Value& modeval = find_value(oparam, "mode");
        
        if (modeval.type() == UniValue::VSTR)
        {
            strMode = modeval.get_str();
LogPrintf("RGP Debug GetBlockTemplate strmode %s \n", strMode );
        }
        else if (modeval.type() == UniValue::VNULL)
        {
            /* Do nothing */
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
        }
    }

    if (strMode != "template")
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
    }

    if (vNodes.empty())
    {
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "SocietyG is not connected!");
    }

    if (IsInitialBlockDownload())
    {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "SocietyG is downloading blocks...");
    }

    if (pindexBest->nHeight >= Params().LastPOWBlock())
    {
        throw JSONRPCError(RPC_MISC_ERROR, "No more PoW blocks");
    }

LogPrintf("RGP Debug GetBlockTemplate Update Block \n");

    // Update block
    static unsigned int nTransactionsUpdatedLast;
    static CBlockIndex* pindexPrev;
    static int64_t nStart;
    static CBlock* pblock;
    
    if (pindexPrev != pindexBest || (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 5))
    {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = NULL;

        // Store the pindexBest used before CreateNewBlock, to avoid races
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex* pindexPrevNew = pindexBest;
        nStart = GetTime();

        // Create new block
        if(pblock)
        {
            delete pblock;
            
            pblock = NULL;
        }

        pblock = CreateNewBlock(*pMiningKey);
        
        if (!pblock)
        {
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");
        }

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew;
    }

    // Update nTime
    pblock->UpdateTime(pindexPrev);
    pblock->nNonce = 0;

    UniValue transactions(UniValue::VARR);
    //Array transactions;
    map<uint256, int64_t> setTxIndex;
    
    int i = 0;
    
    CTxDB txdb("r");
    
    BOOST_FOREACH (CTransaction& tx, pblock->vtx)
    {
        uint256 txHash = tx.GetHash();
        setTxIndex[txHash] = i++;

        if (tx.IsCoinBase() || tx.IsCoinStake())
        {
            continue;
        }

	UniValue entry(UniValue::VOBJ);
        //Object entry;

        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << tx;
        entry.push_back(Pair("data", HexStr(ssTx.begin(), ssTx.end())));

        entry.push_back(Pair("hash", txHash.GetHex()));

        MapPrevTx mapInputs;
        map<uint256, CTxIndex> mapUnused;
        
        bool fInvalid = false;
        
        if (tx.FetchInputs(txdb, mapUnused, false, false, mapInputs, fInvalid))
        {
            entry.push_back(Pair("fee", (int64_t)(tx.GetValueIn(mapInputs) - tx.GetValueOut())));

            UniValue deps(UniValue::VARR);
            //Array deps;
            
            BOOST_FOREACH (MapPrevTx::value_type& inp, mapInputs)
            {
                if (setTxIndex.count(inp.first))
                {
                    deps.push_back(setTxIndex[inp.first]);
                }
            }

            entry.push_back(Pair("depends", deps));

            int64_t nSigOps = GetLegacySigOpCount(tx);
            nSigOps += GetP2SHSigOpCount(tx, mapInputs);
            entry.push_back(Pair("sigops", nSigOps));
        }

        transactions.push_back(entry);
    }

    UniValue aux(UniValue::VOBJ);
    //Object aux;
    aux.push_back(Pair("flags", HexStr(COINBASE_FLAGS.begin(), COINBASE_FLAGS.end())));

    uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();


    static UniValue aMutable;
    if (aMutable.empty())
    {
        aMutable.push_back("time");
        aMutable.push_back("transactions");
        aMutable.push_back("prevblock");
    }

    UniValue result(UniValue::VOBJ);
    //Object result;

    result.push_back(Pair("version",                pblock->nVersion));
    result.push_back(Pair("previousblockhash",      pblock->hashPrevBlock.GetHex()));
    result.push_back(Pair("transactions",           transactions));
    result.push_back(Pair("coinbaseaux",            aux));
    result.push_back(Pair("coinbasevalue",          (int64_t)pblock->vtx[0].vout[0].nValue));
    result.push_back(Pair("target",                 hashTarget.GetHex()));
    result.push_back(Pair("mintime",                (int64_t)pindexPrev->GetPastTimeLimit()+1));
    result.push_back(Pair("mutable",                aMutable));
    result.push_back(Pair("noncerange",             "00000000ffffffff"));
    result.push_back(Pair("sigoplimit",             (int64_t)MAX_BLOCK_SIGOPS));
    result.push_back(Pair("sizelimit",              (int64_t)MAX_BLOCK_SIZE));
    result.push_back(Pair("curtime",                (int64_t)pblock->nTime));
    result.push_back(Pair("bits",                   strprintf("%08x", pblock->nBits)));
    result.push_back(Pair("height",                 (int64_t)(pindexPrev->nHeight+1)));

LogPrintf("RGP Debug GetBlockTemplate Completed \n");

    return result;
}


UniValue submitblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
    {
        throw runtime_error("submitblock <hex data> [optional-params-obj]\n"
                            "[optional-params-obj] parameter is currently ignored.\n"
                            "Attempts to submit new block to network.\n"
                            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.");
    }

    vector<unsigned char> blockData(ParseHex(params[0].get_str()));

    CDataStream ssBlock(blockData, SER_NETWORK, PROTOCOL_VERSION);

    CBlock block;

    try
    {
        ssBlock >> block;
    }
    catch (std::exception &e)
    {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
    }

    bool fAccepted = ProcessBlock(NULL, &block);

    if (!fAccepted)
    {
        return "rejected";
    }

    return UniValue::VNULL;
}

