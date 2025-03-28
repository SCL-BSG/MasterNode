#include "uint256.h"
#include "sync.h"
#include "net.h"
#include "key.h"
#include "util.h"
#include "base58.h"
#include "main.h"
#include "protocol.h"
#include "instantx.h"
#include "activemasternode.h"
#include "masternodeman.h"
#include "darksend.h"
#include "spork.h"
#include "txdb.h"
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace boost;

std::map<uint256, CTransaction> mapTxLockReq;
std::map<uint256, CTransaction> mapTxLockReqRejected;
std::map<uint256, CConsensusVote> mapTxLockVote;
std::map<uint256, CTransactionLock> mapTxLocks;
std::map<COutPoint, uint256> mapLockedInputs;
std::map<uint256, int64_t> mapUnknownVotes; //track votes with no tx for DOS
int nCompleteTXLocks;

//txlock - Locks transaction
//
//step 1.) Broadcast intention to lock transaction inputs, "txlreg", CTransaction
//step 2.) Top 10 masternodes, open connect to top 1 masternode. Send "txvote", CTransaction, Signature, Approve
//step 3.) Top 1 masternode, waits for 10 messages. Upon success, sends "txlock'

void ProcessMessageInstantX(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if(fLiteMode) return; //disable all darksend/masternode related functionality
    if(!IsSporkActive(SPORK_2_INSTANTX)) return;
    if(!darkSendPool.IsBlockchainSynced()) return;


    //LogPrintf("*** RGP ProcessMessageInstantX Start MASTERNODE node address %s \n", pfrom->addr.ToString() );

    if (strCommand == "txlreq")
    {
        LogPrintf("ProcessMessageInstantX::txlreq command \n");

        CDataStream vMsg(vRecv);
        CTransaction tx;
        vRecv >> tx;

        CInv inv(MSG_TXLOCK_REQUEST, tx.GetHash());
        pfrom->AddInventoryKnown(inv);

        if(mapTxLockReq.count(tx.GetHash()) || mapTxLockReqRejected.count(tx.GetHash())){
            return;
        }

        if(!IsIXTXValid(tx))
        {
            LogPrintf("ProcessMessageInstantX::txlreq Not a valid TX \n");
            return;
        }

        BOOST_FOREACH(const CTxOut o, tx.vout)
        {
            if(!o.scriptPubKey.IsNormalPaymentScript() && !o.scriptPubKey.IsUnspendable())
            {
                LogPrintf("ProcessMessageInstantX::txlreq - Invalid Script %s\n", tx.ToString().c_str());
                return;
            }
            MilliSleep(1); /* RGP Optimise */
        }

        int nBlockHeight = CreateNewLock(tx);

        bool fMissingInputs = false;
        CValidationState state;
        CBlockIndex* pindex;
        CBlock block;
        CTxDB txdb("r");

        bool fAccepted = false;
        {
            LOCK(cs_main);
            fAccepted = AcceptToMemoryPool(mempool, tx, true, &fMissingInputs);
        }

        if (fAccepted)
        {
            LogPrintf("*** RGP ProcessMessageInstantX::txlreq Before RelayInventory \n");

            RelayInventory(inv);

            LogPrintf("*** RGP ProcessMessageInstantX::txlreq Before DoConsensusVote \n");

            DoConsensusVote(tx, nBlockHeight);

            mapTxLockReq.insert(make_pair(tx.GetHash(), tx));

            LogPrintf("ProcessMessageInstantX::txlreq - Transaction Lock Request: %s %s : accepted %s\n",
                pfrom->addr.ToString().c_str(), pfrom->cleanSubVer.c_str(),
                tx.GetHash().ToString().c_str()
            );

            return;

        }
        else
        {

            LogPrintf("*** RGP ProcessMessageInstantX::txlreq NOT Accepted \n");

            mapTxLockReqRejected.insert(make_pair(tx.GetHash(), tx));

            // can we get the conflicting transaction as proof?

            LogPrintf("ProcessMessageInstantX::txlreq - Transaction Lock Request: %s %s : rejected %s\n",
                pfrom->addr.ToString().c_str(), pfrom->cleanSubVer.c_str(),
                tx.GetHash().ToString().c_str()
            );

            BOOST_FOREACH(const CTxIn& in, tx.vin){
                if(!mapLockedInputs.count(in.prevout)){
                    mapLockedInputs.insert(make_pair(in.prevout, tx.GetHash()));
                }
                MilliSleep(1); /* Optimize */
            }

            // resolve conflicts
            std::map<uint256, CTransactionLock>::iterator i = mapTxLocks.find(tx.GetHash());
            if (i != mapTxLocks.end()){
                //we only care if we have a complete tx lock
                if((*i).second.CountSignatures() >= INSTANTX_SIGNATURES_REQUIRED){
                    if(!CheckForConflictingLocks(tx)){
                        LogPrintf("ProcessMessageInstantX::txlreq - Found Existing Complete IX Lock\n");

                        //reprocess the last 15 blocks
                        block.DisconnectBlock(txdb, pindex);
                        tx.DisconnectInputs(txdb);
                    }
                }
            }

            return;
        }
    }
    else if (strCommand == "txlvote") //InstantX Lock Consensus Votes
    {
        CConsensusVote ctx;
        vRecv >> ctx;

        LogPrintf("*** RGP ProcessMessageInstantX::txlvote Before RelayInventory \n");

        CInv inv(MSG_TXLOCK_VOTE, ctx.GetHash());
        pfrom->AddInventoryKnown(inv);

        if(mapTxLockVote.count(ctx.GetHash())){
            return;
        }

        mapTxLockVote.insert(make_pair(ctx.GetHash(), ctx));

        if(ProcessConsensusVote(pfrom, ctx)){
            //Spam/Dos protection
            /*
                Masternodes will sometimes propagate votes before the transaction is known to the client.
                This tracks those messages and allows it at the same rate of the rest of the network, if
                a peer violates it, it will simply be ignored
            */
            if(!mapTxLockReq.count(ctx.txHash) && !mapTxLockReqRejected.count(ctx.txHash)){
                if(!mapUnknownVotes.count(ctx.vinMasternode.prevout.hash)){
                    mapUnknownVotes[ctx.vinMasternode.prevout.hash] = GetTime()+(60*10);
                }

                if(mapUnknownVotes[ctx.vinMasternode.prevout.hash] > GetTime() &&
                    mapUnknownVotes[ctx.vinMasternode.prevout.hash] - GetAverageVoteTime() > 60*10){
                        LogPrintf("ProcessMessageInstantX::txlreq - masternode is spamming transaction votes: %s %s\n",
                            ctx.vinMasternode.ToString().c_str(),
                            ctx.txHash.ToString().c_str()
                        );
                        return;
                } else {
                    mapUnknownVotes[ctx.vinMasternode.prevout.hash] = GetTime()+(60*10);
                }
            }

            RelayInventory(inv);
        }

        return;
    }
    MilliSleep(5); /* RGP Optimise */
    
     //LogPrintf("ProcessMessageInstantX END \n");

}

bool IsIXTXValid(const CTransaction& txCollateral)
{
    LogPrintf("*** RGP ProcessMessageInstantX::IsIXTXValid  \n");

    if(txCollateral.vout.size() < 1) return false;
    if(txCollateral.nLockTime != 0) return false;

    int64_t nValueIn = 0;
    int64_t nValueOut = 0;
    bool missingTx = false;

    BOOST_FOREACH(const CTxOut o, txCollateral.vout)
    {
        nValueOut += o.nValue;
        MilliSleep(1); /* RGP Optimise */
    }

    BOOST_FOREACH(const CTxIn i, txCollateral.vin)
    {
        CTransaction tx2;
        uint256 hash;
        if(GetTransaction(i.prevout.hash, tx2, hash)){
            if(tx2.vout.size() > i.prevout.n) {
                nValueIn += tx2.vout[i.prevout.n].nValue;
            }
        } else{
            missingTx = true;
        }
        MilliSleep(1); /* RGP Optimise */
    }

    if(nValueOut > GetSporkValue(SPORK_5_MAX_VALUE)*COIN){
        LogPrint("instantx", "IsIXTXValid - Transaction value too high - %s\n", txCollateral.ToString().c_str());
        return false;
    }

    if(missingTx){
        LogPrint("instantx", "IsIXTXValid - Unknown inputs in IX transaction - %s\n", txCollateral.ToString().c_str());
        /*
            This happens sometimes for an unknown reason, so we'll return that it's a valid transaction.
            If someone submits an invalid transaction it will be rejected by the network anyway and this isn't
            very common, but we don't want to block IX just because the client can't figure out the fee.
        */
        return true;
    }

    if(nValueIn-nValueOut < COIN*0.01) {
        LogPrint("instantx", "IsIXTXValid - did not include enough fees in transaction %d\n%s\n", nValueOut-nValueIn, txCollateral.ToString().c_str());
        return false;
    }

    LogPrintf("*** RGP ProcessMessageInstantX::IsIXTXValid Good ending \n");
    
    MilliSleep(5); /* RGP Optimise */
    return true;
}

int64_t CreateNewLock(CTransaction tx)
{
    LogPrintf("*** RGP ProcessMessageInstantX::CreateNewLock  \n");

    int64_t nTxAge = 0;
    BOOST_REVERSE_FOREACH(CTxIn i, tx.vin){
        nTxAge = GetInputAge(i);
        if(nTxAge < 9)
        {
            LogPrintf("CreateNewLock - Transaction not found / too new: %d / %s\n", nTxAge, tx.GetHash().ToString().c_str());
            return 0;
        }
        MilliSleep(1); /* RGP Optimise */
    }

    /*
        Use a blockheight newer than the input.
        This prevents attackers from using transaction mallibility to predict which masternodes
        they'll use.
    */
    int nBlockHeight = (pindexBest->nHeight - nTxAge)+4;

    if (!mapTxLocks.count(tx.GetHash())){
        LogPrintf("CreateNewLock - New Transaction Lock %s !\n", tx.GetHash().ToString().c_str());

        CTransactionLock newLock;
        newLock.nBlockHeight = nBlockHeight;
        newLock.nExpiration = GetTime()+(20*60); //locks expire after 20 minutes (20 confirmations)
        newLock.nTimeout = GetTime()+(60*5);
        newLock.txHash = tx.GetHash();
        mapTxLocks.insert(make_pair(tx.GetHash(), newLock));
    } else {
        mapTxLocks[tx.GetHash()].nBlockHeight = nBlockHeight;
        LogPrint("instantx", "CreateNewLock - Transaction Lock Exists %s !\n", tx.GetHash().ToString().c_str());
    }

    return nBlockHeight;
}

// check if we need to vote on this transaction
void DoConsensusVote(CTransaction& tx, int64_t nBlockHeight)
{

    LogPrintf("*** RGP ProcessMessageInstantX::DoConsensusVote  \n");

    if(!fMasterNode) return;

    int n = mnodeman.GetMasternodeRank(activeMasternode.vin, nBlockHeight, MIN_INSTANTX_PROTO_VERSION);

    if(n == -1)
    {
        LogPrint("instantx", "InstantX::DoConsensusVote - Unknown Masternode\n");
        return;
    }

    if(n > INSTANTX_SIGNATURES_TOTAL)
    {
        LogPrint("instantx", "InstantX::DoConsensusVote - Masternode not in the top %d (%d)\n", INSTANTX_SIGNATURES_TOTAL, n);
        return;
    }
    /*
        nBlockHeight calculated from the transaction is the authoritive source
    */

    LogPrint("instantx", "InstantX::DoConsensusVote - In the top %d (%d)\n", INSTANTX_SIGNATURES_TOTAL, n);

    CConsensusVote ctx;
    ctx.vinMasternode = activeMasternode.vin;
    ctx.txHash = tx.GetHash();
    ctx.nBlockHeight = nBlockHeight;
    if(!ctx.Sign()){
        LogPrintf("InstantX::DoConsensusVote - Failed to sign consensus vote\n");
        return;
    }
    if(!ctx.SignatureValid()) {
        LogPrintf("InstantX::DoConsensusVote - Signature invalid\n");
        return;
    }

    mapTxLockVote[ctx.GetHash()] = ctx;

    CInv inv(MSG_TXLOCK_VOTE, ctx.GetHash());

    RelayInventory(inv);
    
    MilliSleep(5); /* RGP Optimise */
}

//received a consensus vote
bool ProcessConsensusVote(CNode* pnode, CConsensusVote& ctx)
{

    LogPrintf("*** RGP ProcessMessageInstantX::ProcessConsensusVote  \n");

    int n = mnodeman.GetMasternodeRank(ctx.vinMasternode, ctx.nBlockHeight, MIN_INSTANTX_PROTO_VERSION);

    CMasternode* pmn = mnodeman.Find(ctx.vinMasternode);
    if(pmn != NULL)
    {
        LogPrint("instantx", "InstantX::ProcessConsensusVote - Masternode ADDR %s %d\n", pmn->addr.ToString().c_str(), n);
    }

    if(n == -1)
    {
        //can be caused by past versions trying to vote with an invalid protocol
        LogPrint("instantx", "InstantX::ProcessConsensusVote - Unknown Masternode\n");
        mnodeman.AskForMN(pnode, ctx.vinMasternode);
        return false;
    }

    if(n > INSTANTX_SIGNATURES_TOTAL)
    {
        LogPrint("instantx", "InstantX::ProcessConsensusVote - Masternode not in the top %d (%d) - %s\n", INSTANTX_SIGNATURES_TOTAL, n, ctx.GetHash().ToString().c_str());
        return false;
    }

    if(!ctx.SignatureValid()) {
        LogPrintf("InstantX::ProcessConsensusVote - Signature invalid\n");
        //don't ban, it could just be a non-synced masternode
        mnodeman.AskForMN(pnode, ctx.vinMasternode);
        return false;
    }

    if (!mapTxLocks.count(ctx.txHash)){
        LogPrintf("InstantX::ProcessConsensusVote - New Transaction Lock %s !\n", ctx.txHash.ToString().c_str());

        CTransactionLock newLock;
        newLock.nBlockHeight = 0;
        newLock.nExpiration = GetTime()+(20*60);
        newLock.nTimeout = GetTime()+(60*5);
        newLock.txHash = ctx.txHash;
        mapTxLocks.insert(make_pair(ctx.txHash, newLock));
    } else {
        LogPrint("instantx", "InstantX::ProcessConsensusVote - Transaction Lock Exists %s !\n", ctx.txHash.ToString().c_str());
    }

    CBlockIndex* pindex;
    CBlock block;
    CTxDB txdb("r");
    //compile consessus vote
    std::map<uint256, CTransactionLock>::iterator i = mapTxLocks.find(ctx.txHash);
    if (i != mapTxLocks.end()){
        (*i).second.AddSignature(ctx);

#ifdef ENABLE_WALLET
        if(pwalletMain){
            //when we get back signatures, we'll count them as requests. Otherwise the client will think it didn't propagate.
            if(pwalletMain->mapRequestCount.count(ctx.txHash))
                pwalletMain->mapRequestCount[ctx.txHash]++;
        }
#endif

        LogPrint("instantx", "InstantX::ProcessConsensusVote - Transaction Lock Votes %d - %s !\n", (*i).second.CountSignatures(), ctx.GetHash().ToString().c_str());

        if((*i).second.CountSignatures() >= INSTANTX_SIGNATURES_REQUIRED){
            LogPrint("instantx", "InstantX::ProcessConsensusVote - Transaction Lock Is Complete %s !\n", (*i).second.GetHash().ToString().c_str());

            CTransaction& tx = mapTxLockReq[ctx.txHash];
            if(!CheckForConflictingLocks(tx)){

#ifdef ENABLE_WALLET
                if(pwalletMain){
                    if(pwalletMain->UpdatedTransaction((*i).second.txHash)){
                        nCompleteTXLocks++;
                    }
                }
#endif

                if(mapTxLockReq.count(ctx.txHash)){
                    BOOST_FOREACH(const CTxIn& in, tx.vin){
                        if(!mapLockedInputs.count(in.prevout)){
                            mapLockedInputs.insert(make_pair(in.prevout, ctx.txHash));
                        }
                    }
                }

                // resolve conflicts

                //if this tx lock was rejected, we need to remove the conflicting blocks
                if(mapTxLockReqRejected.count((*i).second.txHash)){
                    //reprocess the last 15 blocks
                    block.DisconnectBlock(txdb, pindex);
                    tx.DisconnectInputs(txdb);
                }
            }
        }
        return true;
    }

    MilliSleep(5); /* RGP Optimise */
    return false;
}

bool CheckForConflictingLocks(CTransaction& tx)
{

    LogPrintf("*** RGP ProcessMessageInstantX::CheckForConflictingLocks  \n");


    /*
        It's possible (very unlikely though) to get 2 conflicting transaction locks approved by the network.
        In that case, they will cancel each other out.

        Blocks could have been rejected during this time, which is OK. After they cancel out, the client will
        rescan the blocks and find they're acceptable and then take the chain with the most work.
    */
    BOOST_FOREACH(const CTxIn& in, tx.vin)
    {
        if(mapLockedInputs.count(in.prevout))
        {
            if(mapLockedInputs[in.prevout] != tx.GetHash())
            {
                LogPrintf("InstantX::CheckForConflictingLocks - found two complete conflicting locks - removing both. %s %s", tx.GetHash().ToString().c_str(), mapLockedInputs[in.prevout].ToString().c_str());
                if(mapTxLocks.count(tx.GetHash())) mapTxLocks[tx.GetHash()].nExpiration = GetTime();
                if(mapTxLocks.count(mapLockedInputs[in.prevout])) mapTxLocks[mapLockedInputs[in.prevout]].nExpiration = GetTime();
                return true;
            }
        }
        MilliSleep(1); /* RGP Optimise */
    }

    return false;
}

int64_t GetAverageVoteTime()
{
    std::map<uint256, int64_t>::iterator it = mapUnknownVotes.begin();
    int64_t total = 0;
    int64_t count = 0;

    while(it != mapUnknownVotes.end()) 
    {
        total+= it->second;
        count++;
        it++;
        MilliSleep(1); /* RGP Optimise */
    }

    return total / count;
}

void CleanTransactionLocksList()
{

    //LogPrintf("*** RGP ProcessMessageInstantX::CleanTransactionLocksList  \n");


    if(pindexBest == NULL) return;

    std::map<uint256, CTransactionLock>::iterator it = mapTxLocks.begin();

    while(it != mapTxLocks.end()) 
    {
        if(GetTime() > it->second.nExpiration){ //keep them for an hour
            LogPrintf("Removing old transaction lock %s\n", it->second.txHash.ToString().c_str());

            if(mapTxLockReq.count(it->second.txHash)){
                CTransaction& tx = mapTxLockReq[it->second.txHash];

                BOOST_FOREACH(const CTxIn& in, tx.vin)
                {
                    mapLockedInputs.erase(in.prevout);
                    MilliSleep(1); /* RGP Optimise */
                }

                mapTxLockReq.erase(it->second.txHash);
                mapTxLockReqRejected.erase(it->second.txHash);

                BOOST_FOREACH(CConsensusVote& v, it->second.vecConsensusVotes)
                {
                    mapTxLockVote.erase(v.GetHash());
                    MilliSleep(1); /* RGP Optimise */
                }
            }

            mapTxLocks.erase(it++);
        } else {
            it++;
        }
        MilliSleep(1); /* RGP Optimise */
    }

}

uint256 CConsensusVote::GetHash() const
{

    //LogPrintf("*** RGP ProcessMessageInstantX::CConsensusVote::GetHash  \n");


    return vinMasternode.prevout.hash + vinMasternode.prevout.n + txHash;
}


bool CConsensusVote::SignatureValid()
{
    //LogPrintf("*** RGP ProcessMessageInstantX::CConsensusVote::SignatureValid  \n");


    std::string errorMessage;
    std::string strMessage = txHash.ToString().c_str() + boost::lexical_cast<std::string>(nBlockHeight);
    //LogPrintf("verify strMessage %s \n", strMessage.c_str());

    CMasternode* pmn = mnodeman.Find(vinMasternode);

    if(pmn == NULL)
    {
        LogPrintf("InstantX::CConsensusVote::SignatureValid() - Unknown Masternode\n");
        return false;
    }

    if(!darkSendSigner.VerifyMessage(pmn->pubkey2, vchMasterNodeSignature, strMessage, errorMessage)) {
        LogPrintf("InstantX::CConsensusVote::SignatureValid() - Verify message failed\n");
        return false;
    }

    MilliSleep(5); /* RGP Optimise */

    return true;
}

bool CConsensusVote::Sign()
{

    //LogPrintf("*** RGP ProcessMessageInstantX::CConsensusVote::Sign  \n");


    std::string errorMessage;

    CKey key2;
    CPubKey pubkey2;
    std::string strMessage = txHash.ToString().c_str() + boost::lexical_cast<std::string>(nBlockHeight);
    //LogPrintf("signing strMessage %s \n", strMessage.c_str());
    //LogPrintf("signing privkey %s \n", strMasterNodePrivKey.c_str());

    if(!darkSendSigner.SetKey(strMasterNodePrivKey, errorMessage, key2, pubkey2))
    {
        LogPrintf("CConsensusVote::Sign() - ERROR: Invalid masternodeprivkey: '%s'\n", errorMessage.c_str());
        return false;
    }

    if(!darkSendSigner.SignMessage(strMessage, errorMessage, vchMasterNodeSignature, key2)) {
        LogPrintf("CConsensusVote::Sign() - Sign message failed");
        return false;
    }

    if(!darkSendSigner.VerifyMessage(pubkey2, vchMasterNodeSignature, strMessage, errorMessage)) {
        LogPrintf("CConsensusVote::Sign() - Verify message failed");
        return false;
    }

    return true;
}


bool CTransactionLock::SignaturesValid()
{

    //LogPrintf("*** RGP ProcessMessageInstantX::CConsensusVote::SignaturesValid \n");


    BOOST_FOREACH(CConsensusVote vote, vecConsensusVotes)
    {
        int n = mnodeman.GetMasternodeRank(vote.vinMasternode, vote.nBlockHeight, MIN_INSTANTX_PROTO_VERSION);

        if(n == -1)
        {
            LogPrintf("CTransactionLock::SignaturesValid() - Unknown Masternode\n");
            return false;
        }

        if(n > INSTANTX_SIGNATURES_TOTAL)
        {
            LogPrintf("CTransactionLock::SignaturesValid() - Masternode not in the top %s\n", INSTANTX_SIGNATURES_TOTAL);
            return false;
        }

        if(!vote.SignatureValid()){
            LogPrintf("CTransactionLock::SignaturesValid() - Signature not valid\n");
            return false;
        }
        MilliSleep(1); /* RGP Optimise */
    }

    return true;
}

void CTransactionLock::AddSignature(CConsensusVote& cv)
{
    //LogPrintf("*** RGP ProcessMessageInstantX:: CTransactionLock::AddSignature \n");

    vecConsensusVotes.push_back(cv);
}

int CTransactionLock::CountSignatures()
{
    //LogPrintf("*** RGP ProcessMessageInstantX:: CTransactionLock::CountSignatures \n");

    /*
        Only count signatures where the BlockHeight matches the transaction's blockheight.
        The votes have no proof it's the correct blockheight
    */

    if(nBlockHeight == 0) return -1;

    int n = 0;
    BOOST_FOREACH(CConsensusVote v, vecConsensusVotes)
    {
        if(v.nBlockHeight == nBlockHeight){
            n++;
        }
        MilliSleep(1); /* RGP Optimise */
    }
    return n;
}

