
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MASTERNODEMAN_H
#define MASTERNODEMAN_H

#include "bignum.h"
#include "sync.h"
#include "net.h"
#include "key.h"
#include "core.h"
#include "util.h"
#include "script.h"
#include "base58.h"
#include "main.h"
#include "masternode.h"

#define MASTERNODES_DUMP_SECONDS               (15*60)
#define MASTERNODES_DSEG_SECONDS               (3*60*60)

using namespace std;

class CMasternodeMan;

extern CMasternodeMan mnodeman;

// map to hold all MNs
extern std::vector<CMasternode> vMasternodes;
    // who's asked for the masternode list and the last time
extern std::map<CNetAddr, int64_t> mAskedUsForMasternodeList;
    // who we asked for the masternode list and the last time
extern  std::map<CNetAddr, int64_t> mWeAskedForMasternodeList;
    // which masternodes we've asked for
extern   std::map<COutPoint, int64_t> mWeAskedForMasternodeListEntry;


extern map < unsigned int, CMasternode > Masternode_Information;
extern map < uint256, CTransaction > MN_CTransaction_Map; 

extern void Misbehaving(NodeId nodeid, int howmuch);

void DumpMasternodes();

/** Access to the MN database (mncache.dat) */
class CMasternodeDB
{
private:
    boost::filesystem::path pathMN;
    std::string strMagicMessage;
public:
    enum ReadResult {
        Ok,
       FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CMasternodeDB();
    bool Write(const CMasternodeMan &mnodemanToSave);
    ReadResult Read(CMasternodeMan& mnodemanToLoad);
};

class CMasternodeMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // map to hold all MNs
    //std::vector<CMasternode> vMasternodes;
    // who's asked for the masternode list and the last time
    //std::map<CNetAddr, int64_t> mAskedUsForMasternodeList;
    // who we asked for the masternode list and the last time
    //std::map<CNetAddr, int64_t> mWeAskedForMasternodeList;
    // which masternodes we've asked for
    //std::map<COutPoint, int64_t> mWeAskedForMasternodeListEntry;

public:
    // keep track of dsq count to prevent masternodes from gaming darksend queue
    int64_t nDsqCount;

    


    IMPLEMENT_SERIALIZE
    (
        // serialized format:
        // * version byte (currently 0)
        // * masternodes vector
        {
                LOCK(cs);
                unsigned char nVersion = 0;
                READWRITE(nVersion);
                READWRITE(vMasternodes);
                READWRITE(mAskedUsForMasternodeList);
                READWRITE(mWeAskedForMasternodeList);
                READWRITE(mWeAskedForMasternodeListEntry);
                READWRITE(nDsqCount);
        }
    )

    CMasternodeMan();
    CMasternodeMan(CMasternodeMan& other);

    // Add an entry
    bool Add(CMasternode &mn);

    // Check all masternodes
    void Check();

    /// Ask (source) node for mnb
    void AskForMN(CNode *pnode, CTxIn &vin);

    // Check all masternodes and remove inactive
    void CheckAndRemove();

    // Clear masternode vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    int CountMasternodesAboveProtocol(int protocolVersion);

    void DsegUpdate(CNode* pnode);

    // Find an entry
    CMasternode* Find(const CTxIn& vin);
    CMasternode* Find(const CPubKey& pubKeyMasternode);

    bool FindNull(const CTxIn& vin);

    //Find an entry thta do not match every entry provided vector
    CMasternode* FindOldestNotInVec(const std::vector<CTxIn> &vVins, int nMinimumAge);

    // Find a random entry
    CMasternode* FindRandom();

    /// Find a random entry
    CMasternode* FindRandomNotInVec(std::vector<CTxIn> &vecToExclude, int protocolVersion = -1);

    // Get the current winner for this block
    CMasternode* GetCurrentMasterNode(int mod=1, int64_t nBlockHeight=0, int minProtocol=0);

    std::vector<CMasternode> GetFullMasternodeVector()
    {
        LogPrintf("RGP GetFullMasternodeVector checking \n");


        Check();

LogPrintf("RGP GetFullMasternodeVector returning %d \n", vMasternodes.size() );

        return vMasternodes;
    }

    std::vector<pair<int, CMasternode> > GetMasternodeRanks(int64_t nBlockHeight, int minProtocol=0);
    int GetMasternodeRank(const CTxIn &vin, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);
    CMasternode* GetMasternodeByRank(int nRank, int64_t nBlockHeight, int minProtocol=0, bool fOnlyActive=true);

    void ProcessMasternodeConnections();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    // Return the number of (unique) masternodes
    int size() { return vMasternodes.size(); }

    std::string ToString() const;

    //
    // Relay Masternode Messages
    //

    void RelayOldMasternodeEntry(const CTxIn vin, const CService addr, const std::vector<unsigned char> vchSig, const int64_t nNow, const CPubKey pubkey, const CPubKey pubkey2, const int count, const int current, const int64_t lastUpdated, const int protocolVersion);

    void RelayMasternodeEntry(const CTxIn vin, const CService addr, const std::vector<unsigned char> vchSig, const int64_t nNow, const CPubKey pubkey, const CPubKey pubkey2, const int count, const int current, const int64_t lastUpdated, const int protocolVersion,
                              CScript rewardAddress, int rewardPercentage );

    void RelayMasternodeEntryPing(const CTxIn vin, const std::vector<unsigned char> vchSig, const int64_t nNow, const bool stop);

    void Remove(CTxIn vin);

};

#endif
