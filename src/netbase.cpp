// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "netbase.h"

#include "hash.h"
#include "sync.h"
#include "uint256.h"
#include "util.h"

#ifndef WIN32
#include <fcntl.h>
#endif

#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/predicate.hpp> // for startswith() and endswith()

#if !defined(HAVE_MSG_NOSIGNAL) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif


/**
 * Seed insecure_rand using the random pool.
 * @param Deterministic Use a deterministic seed
 */
// void seed_insecure_rand(bool fDeterministic = false);

/**
 * MWC RNG of George Marsaglia
 * This is intended to be fast. It has a period of 2^59.3, though the
 * least significant 16 bits only have a period of about 2^30.1.
 *
 * @return random value
 */
//uint32_t insecure_rand_Rz;
//uint32_t insecure_rand_Rw;
static inline uint32_t insecure_rand(void)
{
    insecure_rand_Rz = 36969 * (insecure_rand_Rz & 65535) + (insecure_rand_Rz >> 16);
    insecure_rand_Rw = 18000 * (insecure_rand_Rw & 65535) + (insecure_rand_Rw >> 16);
    return (insecure_rand_Rw << 16) + insecure_rand_Rz;
}

using namespace std;

// Settings
// Settings
static proxyType proxyInfo[NET_MAX];
static proxyType nameProxy;
static CCriticalSection cs_proxyInfos;
static const int SOCKS5_RECV_TIMEOUT = 20 * 1000;

static proxyType_OLD proxyInfo_OLD[NET_MAX];
static proxyType_OLD nameproxyInfo_OLD;
static CCriticalSection cs_proxyInfos_OLD;
int nConnectTimeout = 5000;
bool fNameLookup = false;

static const unsigned char pchIPv4[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };

enum Network ParseNetwork(std::string net) {
    boost::to_lower(net);
    if (net == "ipv4") return NET_IPV4;
    if (net == "ipv6") return NET_IPV6;
    if (net == "tor")  return NET_TOR;
    if (net == "i2p")  return NET_I2P;
    return NET_UNROUTABLE;
}
/* ---------------------
   -- RGP JIRA BSG-51 --
   ----------------------------------------------------------------------
   -- added GetNetworkName() implementation to list of server commands --
   -- required by GetNetworksInfo() in rpsnet.cpp                      --
   ---------------------------------------------------------------------- */

std::string GetNetworkName(enum Network net)
{
    switch (net) {
    case NET_IPV4:
        return "ipv4";
    case NET_IPV6:
        return "ipv6";
    case NET_TOR:
        return "onion";
    default:
        return "";
    }
}



bool CloseSocket(SOCKET& hSocket)
{
    if (hSocket == INVALID_SOCKET)
        return false;
#ifdef WIN32
    int ret = closesocket(hSocket);
#else
    int ret = close(hSocket);
#endif
    hSocket = INVALID_SOCKET;
    return ret != SOCKET_ERROR;
}



bool SetSocketNoDelay(const SOCKET& hSocket)
{
    int set = 1;
    int rc = setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&set, sizeof(int));
    return rc == 0;
}

bool SetSocketNonBlocking(const SOCKET& hSocket, bool fNonBlocking)
{
    if (fNonBlocking) {
#ifdef WIN32
        u_long nOne = 1;
        if (ioctlsocket(hSocket, FIONBIO, &nOne) == SOCKET_ERROR) {
#else
        int fFlags = fcntl(hSocket, F_GETFL, 0);
        if (fcntl(hSocket, F_SETFL, fFlags | O_NONBLOCK) == SOCKET_ERROR) {
#endif
            return false;
        }
    } else {
#ifdef WIN32
        u_long nZero = 0;
        if (ioctlsocket(hSocket, FIONBIO, &nZero) == SOCKET_ERROR) {
#else
        int fFlags = fcntl(hSocket, F_GETFL, 0);
        if (fcntl(hSocket, F_SETFL, fFlags & ~O_NONBLOCK) == SOCKET_ERROR) {
#endif
            return false;
        }
    }

    return true;
}



void SplitHostPort(std::string in, int &portOut, std::string &hostOut) {
    size_t colon = in.find_last_of(':');
    // if a : is found, and it either follows a [...], or no other : is in the string, treat it as port separator
    bool fHaveColon = colon != in.npos;
    bool fBracketed = fHaveColon && (in[0]=='[' && in[colon-1]==']'); // if there is a colon, and in[0]=='[', colon is not 0, so in[colon-1] is safe
    bool fMultiColon = fHaveColon && (in.find_last_of(':',colon-1) != in.npos);
    if (fHaveColon && (colon==0 || fBracketed || !fMultiColon)) {
        char *endp = NULL;
        int n = strtol(in.c_str() + colon + 1, &endp, 10);
        if (endp && *endp == 0 && n >= 0) {
            in = in.substr(0, colon);
            if (n > 0 && n < 0x10000)
                portOut = n;
        }
    }
    if (in.size()>0 && in[0] == '[' && in[in.size()-1] == ']')
        hostOut = in.substr(1, in.size()-2);
    else
        hostOut = in;
}

bool static LookupIntern(const char *pszName, std::vector<CNetAddr>& vIP, unsigned int nMaxSolutions, bool fAllowLookup)
{
    vIP.clear();

    {
        CNetAddr addr;
        if (addr.SetSpecial(std::string(pszName))) {
            vIP.push_back(addr);
            return true;
        }
    }

    struct addrinfo aiHint;
    memset(&aiHint, 0, sizeof(struct addrinfo));

    aiHint.ai_socktype = SOCK_STREAM;
    aiHint.ai_protocol = IPPROTO_TCP;
    aiHint.ai_family = AF_UNSPEC;
#ifdef WIN32
    aiHint.ai_flags = fAllowLookup ? 0 : AI_NUMERICHOST;
#else
    aiHint.ai_flags = fAllowLookup ? AI_ADDRCONFIG : AI_NUMERICHOST;
#endif
    struct addrinfo *aiRes = NULL;
    int nErr = getaddrinfo(pszName, NULL, &aiHint, &aiRes);
    if (nErr)
        return false;

    struct addrinfo *aiTrav = aiRes;
    while (aiTrav != NULL && (nMaxSolutions == 0 || vIP.size() < nMaxSolutions))
    {
        if (aiTrav->ai_family == AF_INET)
        {
            assert(aiTrav->ai_addrlen >= sizeof(sockaddr_in));
            vIP.push_back(CNetAddr(((struct sockaddr_in*)(aiTrav->ai_addr))->sin_addr));
        }

        if (aiTrav->ai_family == AF_INET6)
        {
            assert(aiTrav->ai_addrlen >= sizeof(sockaddr_in6));
            vIP.push_back(CNetAddr(((struct sockaddr_in6*)(aiTrav->ai_addr))->sin6_addr));
        }

        aiTrav = aiTrav->ai_next;
    }

    freeaddrinfo(aiRes);

    return (vIP.size() > 0);
}

bool LookupHost(const char *pszName, std::vector<CNetAddr>& vIP, unsigned int nMaxSolutions, bool fAllowLookup)
{
    std::string strHost(pszName);
    if (strHost.empty())
        return false;
    if (boost::algorithm::starts_with(strHost, "[") && boost::algorithm::ends_with(strHost, "]"))
    {
        strHost = strHost.substr(1, strHost.size() - 2);
    }

    return LookupIntern(strHost.c_str(), vIP, nMaxSolutions, fAllowLookup);
}

bool LookupHostNumeric(const char *pszName, std::vector<CNetAddr>& vIP, unsigned int nMaxSolutions)
{
    return LookupHost(pszName, vIP, nMaxSolutions, false);
}

bool Lookup(const char *pszName, std::vector<CService>& vAddr, int portDefault, bool fAllowLookup, unsigned int nMaxSolutions)
{
    if (pszName[0] == 0)
        return false;
    int port = portDefault;
    std::string hostname = "";
    SplitHostPort(std::string(pszName), port, hostname);

    std::vector<CNetAddr> vIP;
    bool fRet = LookupIntern(hostname.c_str(), vIP, nMaxSolutions, fAllowLookup);
    if (!fRet)
        return false;
    vAddr.resize(vIP.size());
    for (unsigned int i = 0; i < vIP.size(); i++)
        vAddr[i] = CService(vIP[i], port);
    return true;
}

bool Lookup(const char *pszName, CService& addr, int portDefault, bool fAllowLookup)
{
    std::vector<CService> vService;
    bool fRet = Lookup(pszName, vService, portDefault, fAllowLookup, 1);
    if (!fRet)
        return false;
    addr = vService[0];
    return true;
}

bool LookupNumeric(const char *pszName, CService& addr, int portDefault)
{
    return Lookup(pszName, addr, portDefault, false);
}

struct timeval MillisToTimeval(int64_t nTimeout)
{
    struct timeval timeout;
    timeout.tv_sec  = nTimeout / 1000;
    timeout.tv_usec = (nTimeout % 1000) * 1000;
    return timeout;
}


bool static Socks4(const CService &addrDest, SOCKET& hSocket)
{
    LogPrintf("SOCKS4 connecting %s\n", addrDest.ToString());
    if (!addrDest.IsIPv4())
    {
        closesocket(hSocket);
        return error("Proxy destination is not IPv4");
    }
    char pszSocks4IP[] = "\4\1\0\0\0\0\0\0user";
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (!addrDest.GetSockAddr((struct sockaddr*)&addr, &len) || addr.sin_family != AF_INET)
    {
        closesocket(hSocket);
        return error("Cannot get proxy destination address");
    }
    memcpy(pszSocks4IP + 2, &addr.sin_port, 2);
    memcpy(pszSocks4IP + 4, &addr.sin_addr, 4);
    char* pszSocks4 = pszSocks4IP;
    int nSize = sizeof(pszSocks4IP);

    int ret = send(hSocket, pszSocks4, nSize, MSG_NOSIGNAL);
    if (ret != nSize)
    {
        closesocket(hSocket);
        return error("Error sending to proxy");
    }
    char pchRet[8];
    if (recv(hSocket, pchRet, 8, 0) != 8)
    {
        closesocket(hSocket);
        return error("Error reading proxy response");
    }
    if (pchRet[1] != 0x5a)
    {
        closesocket(hSocket);
        if (pchRet[1] != 0x5b)
            LogPrintf("ERROR: Proxy returned error %d\n", pchRet[1]);
        return false;
    }
    LogPrintf("SOCKS4 connected %s\n", addrDest.ToString());
    return true;
}

/**
 * Read bytes from socket. This will either read the full number of bytes requested
 * or return False on error or timeout.
 * This function can be interrupted by boost thread interrupt.
 *
 * @param data Buffer to receive into
 * @param len  Length of data to receive
 * @param timeout  Timeout in milliseconds for receive operation
 *
 * @note This function requires that hSocket is in non-blocking mode.
 */
bool static InterruptibleRecv(char* data, size_t len, int timeout, SOCKET& hSocket)
{
    int64_t curTime = GetTimeMillis();
    int64_t endTime = curTime + timeout;
    // Maximum time to wait in one select call. It will take up until this time (in millis)
    // to break off in case of an interruption.
    const int64_t maxWait = 1000;
    while (len > 0 && curTime < endTime) {
        ssize_t ret = recv(hSocket, data, len, 0); // Optimistically try the recv first
        if (ret > 0) {
            len -= ret;
            data += ret;
        } else if (ret == 0) { // Unexpected disconnection
            return false;
        } else { // Other error or blocking
            int nErr = WSAGetLastError();
            if (nErr == WSAEINPROGRESS || nErr == WSAEWOULDBLOCK || nErr == WSAEINVAL) {
                if (!IsSelectableSocket(hSocket)) {
                    return false;
                }
                struct timeval tval = MillisToTimeval(std::min(endTime - curTime, maxWait));
                fd_set fdset;
                FD_ZERO(&fdset);
                FD_SET(hSocket, &fdset);
                int nRet = select(hSocket + 1, &fdset, NULL, NULL, &tval);
                if (nRet == SOCKET_ERROR) {
                    return false;
                }
            } else {
                return false;
            }
        }
        boost::this_thread::interruption_point();
        curTime = GetTimeMillis();
    }
    return len == 0;
}



struct ProxyCredentials
{
    std::string username;
    std::string password;
};

/** Connect using SOCKS5 (as described in RFC1928) */
bool static Socks5(string strDest, int port, const ProxyCredentials *auth, SOCKET& hSocket)
{
    LogPrintf("SOCKS5 connecting %s\n", strDest);
    if (strDest.size() > 255) {
        CloseSocket(hSocket);
        return error("Hostname too long");
    }
    // Accepted authentication methods
    std::vector<uint8_t> vSocks5Init;
    vSocks5Init.push_back(0x05);
    if (auth) {
        vSocks5Init.push_back(0x02); // # METHODS
        vSocks5Init.push_back(0x00); // X'00' NO AUTHENTICATION REQUIRED
        vSocks5Init.push_back(0x02); // X'02' USERNAME/PASSWORD (RFC1929)
    } else {
        vSocks5Init.push_back(0x01); // # METHODS
        vSocks5Init.push_back(0x00); // X'00' NO AUTHENTICATION REQUIRED
    }
    ssize_t ret = send(hSocket, (const char*)begin_ptr(vSocks5Init), vSocks5Init.size(), MSG_NOSIGNAL);
    if (ret != (ssize_t)vSocks5Init.size()) {
        CloseSocket(hSocket);
        return error("Error sending to proxy");
    }
    char pchRet1[2];
    if (!InterruptibleRecv(pchRet1, 2, SOCKS5_RECV_TIMEOUT, hSocket)) {
        CloseSocket(hSocket);
        return error("Error reading proxy response");
    }
    if (pchRet1[0] != 0x05) {
        CloseSocket(hSocket);
        return error("Proxy failed to initialize");
    }
    if (pchRet1[1] == 0x02 && auth) {
        // Perform username/password authentication (as described in RFC1929)
        std::vector<uint8_t> vAuth;
        vAuth.push_back(0x01);
        if (auth->username.size() > 255 || auth->password.size() > 255)
            return error("Proxy username or password too long");
        vAuth.push_back(auth->username.size());
        vAuth.insert(vAuth.end(), auth->username.begin(), auth->username.end());
        vAuth.push_back(auth->password.size());
        vAuth.insert(vAuth.end(), auth->password.begin(), auth->password.end());
        ret = send(hSocket, (const char*)begin_ptr(vAuth), vAuth.size(), MSG_NOSIGNAL);
        if (ret != (ssize_t)vAuth.size()) {
            CloseSocket(hSocket);
            return error("Error sending authentication to proxy");
        }
        LogPrint("proxy", "SOCKS5 sending proxy authentication %s:%s\n", auth->username, auth->password);
        char pchRetA[2];
        if (!InterruptibleRecv(pchRetA, 2, SOCKS5_RECV_TIMEOUT, hSocket)) {
            CloseSocket(hSocket);
            return error("Error reading proxy authentication response");
        }
        if (pchRetA[0] != 0x01 || pchRetA[1] != 0x00) {
            CloseSocket(hSocket);
            return error("Proxy authentication unsuccesful");
        }
    } else if (pchRet1[1] == 0x00) {
        // Perform no authentication
    } else {
        CloseSocket(hSocket);
        return error("Proxy requested wrong authentication method %02x", pchRet1[1]);
    }
    std::vector<uint8_t> vSocks5;
    vSocks5.push_back(0x05); // VER protocol version
    vSocks5.push_back(0x01); // CMD CONNECT
    vSocks5.push_back(0x00); // RSV Reserved
    vSocks5.push_back(0x03); // ATYP DOMAINNAME
    vSocks5.push_back(strDest.size()); // Length<=255 is checked at beginning of function
    vSocks5.insert(vSocks5.end(), strDest.begin(), strDest.end());
    vSocks5.push_back((port >> 8) & 0xFF);
    vSocks5.push_back((port >> 0) & 0xFF);
    ret = send(hSocket, (const char*)begin_ptr(vSocks5), vSocks5.size(), MSG_NOSIGNAL);
    if (ret != (ssize_t)vSocks5.size()) {
        CloseSocket(hSocket);
        return error("Error sending to proxy");
    }
    char pchRet2[4];
    if (!InterruptibleRecv(pchRet2, 4, SOCKS5_RECV_TIMEOUT, hSocket)) {
        CloseSocket(hSocket);
        return error("Error reading proxy response");
    }
    if (pchRet2[0] != 0x05) {
        CloseSocket(hSocket);
        return error("Proxy failed to accept request");
    }
    if (pchRet2[1] != 0x00) {
        CloseSocket(hSocket);
        switch (pchRet2[1]) {
        case 0x01:
            return error("Proxy error: general failure");
        case 0x02:
            return error("Proxy error: connection not allowed");
        case 0x03:
            return error("Proxy error: network unreachable");
        case 0x04:
            return error("Proxy error: host unreachable");
        case 0x05:
            return error("Proxy error: connection refused");
        case 0x06:
            return error("Proxy error: TTL expired");
        case 0x07:
            return error("Proxy error: protocol error");
        case 0x08:
            return error("Proxy error: address type not supported");
        default:
            return error("Proxy error: unknown");
        }
    }
    if (pchRet2[2] != 0x00) {
        CloseSocket(hSocket);
        return error("Error: malformed proxy response");
    }
    char pchRet3[256];
    switch (pchRet2[3]) {
    case 0x01:
        ret = InterruptibleRecv(pchRet3, 4, SOCKS5_RECV_TIMEOUT, hSocket);
        break;
    case 0x04:
        ret = InterruptibleRecv(pchRet3, 16, SOCKS5_RECV_TIMEOUT, hSocket);
        break;
    case 0x03: {
        ret = InterruptibleRecv(pchRet3, 1, SOCKS5_RECV_TIMEOUT, hSocket);
        if (!ret) {
            CloseSocket(hSocket);
            return error("Error reading from proxy");
        }
        int nRecv = pchRet3[0];
        ret = InterruptibleRecv(pchRet3, nRecv, SOCKS5_RECV_TIMEOUT, hSocket);
        break;
    }
    default:
        CloseSocket(hSocket);
        return error("Error: malformed proxy response");
    }
    if (!ret) {
        CloseSocket(hSocket);
        return error("Error reading from proxy");
    }
    if (!InterruptibleRecv(pchRet3, 2, SOCKS5_RECV_TIMEOUT, hSocket)) {
        CloseSocket(hSocket);
        return error("Error reading from proxy");
    }
    LogPrintf("SOCKS5 connected %s\n", strDest);
    return true;
}



//bool static Socks5(string strDest, int port, SOCKET& hSocket)
//{
//    LogPrintf("SOCKS5 connecting %s\n", strDest);
//    if (strDest.size() > 255)
//    {
//        closesocket(hSocket);
//        return error("Hostname too long");
//    }
//    char pszSocks5Init[] = "\5\1\0";
//    ssize_t nSize = sizeof(pszSocks5Init) - 1;

//    ssize_t ret = send(hSocket, pszSocks5Init, nSize, MSG_NOSIGNAL);
//    if (ret != nSize)
//    {
//        closesocket(hSocket);
//        return error("Error sending to proxy");
//    }
//    char pchRet1[2];
//    if (recv(hSocket, pchRet1, 2, 0) != 2)
//    {
//        closesocket(hSocket);
//        return error("Error reading proxy response");
//    }
//    if (pchRet1[0] != 0x05 || pchRet1[1] != 0x00)
//    {
//       closesocket(hSocket);
//        return error("Proxy failed to initialize");
//    }
//    string strSocks5("\5\1");
//    strSocks5 += '\000'; strSocks5 += '\003';
//    strSocks5 += static_cast<char>(std::min((int)strDest.size(), 255));
//    strSocks5 += strDest;
//    strSocks5 += static_cast<char>((port >> 8) & 0xFF);
//    strSocks5 += static_cast<char>((port >> 0) & 0xFF);
//    ret = send(hSocket, strSocks5.c_str(), strSocks5.size(), MSG_NOSIGNAL);
//    if (ret != (ssize_t)strSocks5.size())
//    {
//        closesocket(hSocket);
//        return error("Error sending to proxy");
//    }
//    char pchRet2[4];
//    if (recv(hSocket, pchRet2, 4, 0) != 4)
//    {
//        closesocket(hSocket);
//        return error("Error reading proxy response");
//    }
//    if (pchRet2[0] != 0x05)
//    {
//        closesocket(hSocket);
//        return error("Proxy failed to accept request");
//    }
//    if (pchRet2[1] != 0x00)
//    {
//        closesocket(hSocket);
//        switch (pchRet2[1])
//        {
//            case 0x01: return error("Proxy error: general failure");
//            case 0x02: return error("Proxy error: connection not allowed");
//            case 0x03: return error("Proxy error: network unreachable");
//            case 0x04: return error("Proxy error: host unreachable");
//            case 0x05: return error("Proxy error: connection refused");
//            case 0x06: return error("Proxy error: TTL expired");
//            case 0x07: return error("Proxy error: protocol error");
//            case 0x08: return error("Proxy error: address type not supported");
//            default:   return error("Proxy error: unknown");
//        }
//    }
//    if (pchRet2[2] != 0x00)
//    {
//        closesocket(hSocket);
//        return error("Error: malformed proxy response");
//    }
//    char pchRet3[256];
//    switch (pchRet2[3])
//    {
//        case 0x01: ret = recv(hSocket, pchRet3, 4, 0) != 4; break;
//        case 0x04: ret = recv(hSocket, pchRet3, 16, 0) != 16; break;
//        case 0x03:
//        {
//            ret = recv(hSocket, pchRet3, 1, 0) != 1;
//            if (ret) {
//                closesocket(hSocket);
//                return error("Error reading from proxy");
//            }
//            int nRecv = pchRet3[0];
//            ret = recv(hSocket, pchRet3, nRecv, 0) != nRecv;
//            break;
//        }
//        default: closesocket(hSocket); return error("Error: malformed proxy response");
//    }
//    if (ret)
//    {
//        closesocket(hSocket);
//        return error("Error reading from proxy");
//    }
//    if (recv(hSocket, pchRet3, 2, 0) != 2)
//    {
//        closesocket(hSocket);
//        return error("Error reading from proxy");
//    }
//    LogPrintf("SOCKS5 connected %s\n", strDest);
//    return true;
//}

bool static ConnectSocketDirectly(const CService &addrConnect, SOCKET& hSocketRet, int nTimeout)
{
    hSocketRet = INVALID_SOCKET;
    struct sockaddr_storage sockaddr;
    int set;

    //LogPrintf("*** RGP ConnectSocketDirectly Debug 1 timeout %d \n", nTimeout);


    socklen_t len = sizeof(sockaddr);
    if (!addrConnect.GetSockAddr((struct sockaddr*)&sockaddr, &len))
    {
        LogPrintf("Cannot connect to %s: unsupported network\n", addrConnect.ToString());
        return false;
    }

    //LogPrintf("*** RGP ConnectSocketDirectly Debug 2 timeout %d \n", nTimeout);

    SOCKET hSocket = socket(((struct sockaddr*)&sockaddr)->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (hSocket == INVALID_SOCKET)
    {
        LogPrintf("*** RGP ConnectSocketDirectly Debug 3 INVALID SOCKET timeout %d \n", nTimeout);
        return false;
    }


    // LogPrintf("*** RGP ConnectSocketDirectly Debug 4 timeout %d \n", nTimeout);

    set = 1;
#ifdef SO_NOSIGPIPE
    // Different way of disabling SIGPIPE on BSD
    setsockopt(hSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&set, sizeof(int));
#endif

     //Disable Nagle's algorithm
#ifdef WIN32
    setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&set, sizeof(int));
#else
    setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (void*)&set, sizeof(int));
#endif

#ifdef WIN32
    u_long fNonblock = 1;
    if (ioctlsocket(hSocket, FIONBIO, &fNonblock) == SOCKET_ERROR)
#else
    int fFlags = fcntl(hSocket, F_GETFL, 0);
    if (fcntl(hSocket, F_SETFL, fFlags | O_NONBLOCK) == -1)
#endif
    {
        closesocket(hSocket);
        return false;
    }


//#ifdef SO_NOSIGPIPE
//    set = 1;
    // Different way of disabling SIGPIPE on BSD
//    setsockopt(hSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&set, sizeof(int));
//#endif

    //LogPrintf("*** RGP ConnectSocketDirectly Debug 5 timeout %d \n", nTimeout);

     //Disable Nagle's algorithm
//    set = 1;
//#ifdef WIN32
    //setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&set, sizeof(int));
//#else
    // LInux RGP
    //setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (void*)&set, sizeof(int));
//#endif

    //LogPrintf("*** RGP ConnectSocketDirectly Debug 6 timeout %d \n", nTimeout);

    //Disable Nagle's algorithm
    //SetSocketNoDelay(hSocket);

    //LogPrintf("*** RGP ConnectSocketDirectly Debug 7 timeout %d \n", nTimeout);

    // Set to non-blocking
    //if (!SetSocketNonBlocking(hSocket, true))
    //{

    //    LogPrintf("*** RGP ConnectSocketDirectly Debug 8 SetSocketNonBlocking timeout %d \n", nTimeout);
    //    CloseSocket(hSocket);
   //     return error("ConnectSocketDirectly: Setting socket to non-blocking failed, error %s\n", NetworkErrorString(WSAGetLastError()));
   // }

//#ifdef WIN32
//    u_long fNonblock = 1;
//    if (ioctlsocket(hSocket, FIONBIO, &fNonblock) == SOCKET_ERROR)
//#else

//    int fFlags = fcntl(hSocket, F_GETFL, 0);
   // if (fcntl(hSocket, F_SETFL, fFlags | O_NONBLOCK) == -1)
//#endif
  //  {
  //      closesocket(hSocket);
  //      return false;
  //  }

    if (connect(hSocket, (struct sockaddr*)&sockaddr, len) == SOCKET_ERROR)
    {

        int nErr = WSAGetLastError();
        // WSAEINVAL is here because some legacy version of winsock uses it
        //if (WSAGetLastError() == WSAEINPROGRESS || WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINVAL)
        if (nErr == WSAEINPROGRESS || nErr == WSAEWOULDBLOCK || nErr == WSAEINVAL)
        {
            //struct timeval timeout;
            //timeout.tv_sec  = nTimeout / 1000;
            //timeout.tv_usec = (nTimeout % 1000) * 1000;

            //fd_set fdset;
            //FD_ZERO(&fdset);
            //FD_SET(hSocket, &fdset);
            //int nRet = select(hSocket + 1, NULL, &fdset, NULL, &timeout);

            /* RGP */
            int TIMEOUT = 20000;

            struct timeval timeout = MillisToTimeval ( TIMEOUT ); //(nTimeout);
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(hSocket, &fdset);



            int nRet = select(hSocket + 1, nullptr, &fdset, nullptr, &timeout);

            if (nRet == 0)
            {

                //LogPrintf("net, connection to %s timeout\n", addrConnect.ToString());
                closesocket(hSocket);
                return false;
            }
            if (nRet == SOCKET_ERROR)
            {

                //LogPrintf("select() for %s failed: %i\n", addrConnect.ToString(), WSAGetLastError());
                closesocket(hSocket);
                return false;
            }
            socklen_t nRetSize = sizeof(nRet);
#ifdef WIN32
            if (getsockopt(hSocket, SOL_SOCKET, SO_ERROR, (char*)(&nRet), &nRetSize) == SOCKET_ERROR)
#else
            if (getsockopt(hSocket, SOL_SOCKET, SO_ERROR, &nRet, &nRetSize) == SOCKET_ERROR)
#endif
            {
                //LogPrintf("*** RGP ConnectSocketDirectly Debug 11c timeout %d \n", nTimeout);


                //LogPrintf("getsockopt() for %s failed: %i\n", addrConnect.ToString(), WSAGetLastError());
                closesocket(hSocket);
                return false;
            }
            if (nRet != 0)
            {
                //LogPrintf("*** RGP ConnectSocketDirectly Debug 11d timeout %d \n", nTimeout);



                //LogPrintf("connect() to %s failed after select(): %s \n", addrConnect.ToString(), strerror(nRet));
                closesocket(hSocket);
                return false;
            }
        }
#ifdef WIN32
        else
            if (WSAGetLastError() != WSAEISCONN)
#else
        else
#endif
        {


            // LogPrintf("*** RGP ConnectSocketDirectly Debug 5 connect failed timeout %d \n", nTimeout);
            //LogPrintf("connect() to %s failed: %i \n", addrConnect.ToString(), WSAGetLastError());
            closesocket(hSocket);
            return false;
        }



    }

    // this isn't even strictly necessary
    // CNode::ConnectNode immediately turns the socket back to non-blocking
    // but we'll turn it back to blocking just in case
#ifdef WIN32
    fNonblock = 0;
    if (ioctlsocket(hSocket, FIONBIO, &fNonblock) == SOCKET_ERROR)
#else
    fFlags = fcntl(hSocket, F_GETFL, 0);
    if (fcntl(hSocket, F_SETFL, fFlags & ~O_NONBLOCK) == SOCKET_ERROR)
#endif
    {
        LogPrintf("*** RGP ConnectSocketDirectly Debug 23 timeout %d \n", nTimeout);

        closesocket(hSocket);
        return false;
    }

    hSocketRet = hSocket;
    return true;
}

//bool SetProxy(enum Network net, CService addrProxy, int nSocksVersion) {
//    assert(net >= 0 && net < NET_MAX);
//    if (nSocksVersion != 0 && nSocksVersion != 4 && nSocksVersion != 5)
//        return false;
//    if (nSocksVersion != 0 && !addrProxy.IsValid())
//        return false;
//    LOCK(cs_proxyInfos);
//    proxyInfo[net] = std::make_pair(addrProxy, nSocksVersion);
//    return true;
//}

/* ---------------------
   -- RGP JIRA BSG-51 --
   --------------------- */

bool SetProxy(enum Network net, const proxyType &addrProxy)
{
    assert(net >= 0 && net < NET_MAX);
    if (!addrProxy.IsValid())
        return false;
    LOCK(cs_proxyInfos);
    proxyInfo[net] = addrProxy;
    return true;
}

bool GetProxy(enum Network net, proxyType& proxyInfoOut)
{
    assert(net >= 0 && net < NET_MAX);
    LOCK(cs_proxyInfos);
    if (!proxyInfo[net].IsValid())
        return false;
    proxyInfoOut = proxyInfo[net];
    return true;
}


bool GetProxy_OLD(enum Network net, proxyType_OLD &proxyInfoOut)
{
    //LogPrintf("*** RGP GetProxy_OLD() \n");

    switch ( net )
    {
        case NET_UNROUTABLE : LogPrintf("*** RGP NET_UNROUTABLE \n");
                              LogPrintf("*** RGP GetProxy ERROR net is ZERO \n");
                              // assert(net >= 0 && net < NET_MAX);
                              return false;
                              break;
        case NET_IPV4       : //LogPrintf("*** RGP NET_IPV4 \n");
                              break;
        case NET_TOR        : LogPrintf("*** RGP NET_TOR \n");
                              break;
        case NET_I2P        : LogPrintf("*** RGP NET_I2P \n");
                              break;

        case NET_IPV6       : //LogPrintf("*** RGP NET_IPV6 \n");
                              break;

        case NET_MAX        : LogPrintf("*** RGP NET_MAX \n");
                              LogPrintf("*** RGP GetProxy ERROR net NET_MAX reached \n");
                              return false;
                              //assert(net >= 0 && net < NET_MAX);
                              break;

        default             : LogPrintf("*** RGP not defined \n");

    }

    if ( net >= 0 && net < NET_MAX )
    {
        LogPrintf("*** RGP GetProxy ERROR net is ZERO or NET_MAX reached \n");
        assert(net >= 0 && net < NET_MAX);
    }

    //LOCK(cs_s);

    if (!proxyInfo_OLD[net].second)
    {
        return false;
    }
    proxyInfoOut = proxyInfo_OLD[net];
    return true;
}

bool SetNameProxy(const proxyType &addrProxy)
{
    if (!addrProxy.IsValid())
        return false;
    LOCK(cs_proxyInfos);
    nameProxy = addrProxy;
    return true;
}

bool GetNameProxy(proxyType &nameProxyOut)
{
    LOCK(cs_proxyInfos);
    if (!nameProxy.IsValid())
        return false;
    nameProxyOut = nameProxy;
    return true;
}

bool HaveNameProxy()
{
    LOCK(cs_proxyInfos);
    return nameProxy.IsValid();
}

bool IsProxy(const CNetAddr& addr)
{
    LOCK(cs_proxyInfos);
    for (int i = 0; i < NET_MAX; i++) {
        if (addr == (CNetAddr)proxyInfo[i].proxy)
            return true;
    }
    return false;
}

static bool ConnectThroughProxy(const proxyType &proxy, const std::string strDest, int port, SOCKET& hSocketRet, int nTimeout, bool *outProxyConnectionFailed)
{
    SOCKET hSocket = INVALID_SOCKET;
    // first connect to proxy server
    if (!ConnectSocketDirectly(proxy.proxy, hSocket, nTimeout)) {
        if (outProxyConnectionFailed)
            *outProxyConnectionFailed = true;
        return false;
    }
    // do socks negotiation
    if (proxy.randomize_credentials) {
        ProxyCredentials random_auth;
        random_auth.username = strprintf("%i", insecure_rand());
        random_auth.password = strprintf("%i", insecure_rand());
        if (!Socks5(strDest, (unsigned short)port, &random_auth, hSocket))
            return false;
    } else {
        if (!Socks5(strDest, (unsigned short)port, 0, hSocket))
            return false;
    }

    hSocketRet = hSocket;
    return true;
}


//bool SetNameProxy(CService addrProxy, int nSocksVersion) {
//    if (nSocksVersion != 0 && nSocksVersion != 5)
//        return false;
//    if (nSocksVersion != 0 && !addrProxy.IsValid())
//        return false;
//    LOCK(cs_proxyInfos);
//    nameproxyInfo = std::make_pair(addrProxy, nSocksVersion);
//    return true;
//}

//bool GetNameProxy(proxyType &nameproxyInfoOut) {
//    LOCK(cs_proxyInfos);
//    if (!nameproxyInfo.second)
//        return false;
//    nameproxyInfoOut = nameproxyInfo;
//    return true;
//}

//bool HaveNameProxy() {
//    LOCK(cs_proxyInfos);
//    return nameproxyInfo.second != 0;
//}

//bool IsProxy(const CNetAddr &addr) {
//    LOCK(cs_proxyInfos);
//    for (int i = 0; i < NET_MAX; i++) {
//        if (proxyInfo[i].second && (addr == (CNetAddr)proxyInfo[i].first))
//            return true;
//    }
//    return false;
//}

bool ConnectSocket(const CService &addrDest, SOCKET& hSocketRet, int nTimeout, bool *outProxyConnectionFailed)
{
    proxyType proxy;
    if (outProxyConnectionFailed)
        *outProxyConnectionFailed = false;

    if (GetProxy(addrDest.GetNetwork(), proxy))
        return ConnectThroughProxy(proxy, addrDest.ToStringIP(), addrDest.GetPort(), hSocketRet, nTimeout, outProxyConnectionFailed);
    else // no proxy needed (none set for target network)
        return ConnectSocketDirectly(addrDest, hSocketRet, nTimeout);
}


//bool ConnectSocket(const CService &addrDest, SOCKET& hSocketRet, int nTimeout)
//{
//    proxyType proxy;
//    enum Network network_type;
//    bool status;

//    network_type = addrDest.GetNetwork();
//    switch ( network_type )
//    {
//        case NET_UNROUTABLE : LogPrintf("*** RGP NET_UNROUTABLE \n");
//                              break;
//        case NET_IPV4       : //LogPrintf("*** RGP NET_IPV4 \n");
//                              break;
//        case NET_TOR        : LogPrintf("*** RGP NET_TOR \n");
//                              break;
//        case NET_I2P        : LogPrintf("*** RGP NET_I2P \n");
//                              break;
//
//        case NET_IPV6       : //LogPrintf("*** RGP NET_IPV6 \n");
//                              break;
//
//        case NET_MAX        : LogPrintf("*** RGP NET_MAX \n");
//                              break;
//
//        default             : LogPrintf("*** RGP not defined \n");
//
//    }

    // no proxy needed
//    if (!GetProxy(addrDest.GetNetwork(), proxy))
//    {
//
//        status = ConnectSocketDirectly(addrDest, hSocketRet, nTimeout);
//
//        return status;
//    }

    //   if ( !ConnectSocketDirectly(addrDest, hSocketRet, nTimeout))
//    {
 //        LogPrintf("*** RGP ConnectSocket Debug 4 \n");
 //        return false;
//    }

//    SOCKET hSocket = INVALID_SOCKET;

    // first connect to proxy server
//    if (!ConnectSocketDirectly(proxy.first, hSocket, nTimeout))
//    {
//        return false;
//    }
    // do socks negotiation
//    switch (proxy.second) {
//    case 4:
//        if (!Socks4(addrDest, hSocket))
//            return false;
//        break;
//    case 5:
//        if (!Socks5(addrDest.ToStringIP(), addrDest.GetPort(), hSocket))
//            return false;
//        break;
//    default:
//        closesocket(hSocket);
//        return false;
//    }

//    hSocketRet = hSocket;
//    return true;
//}

bool ConnectSocketByName(CService& addr, SOCKET& hSocketRet, const char* pszDest, int portDefault, int nTimeout, bool* outProxyConnectionFailed)
{
    string strDest;
    int port = portDefault;

    LogPrintf("RGP ConnectSocketByName start with %s timeout %d  \n", pszDest, nTimeout );

    if (outProxyConnectionFailed)
        *outProxyConnectionFailed = false;

    SplitHostPort(string(pszDest), port, strDest);

    proxyType nameProxy;
    GetNameProxy(nameProxy);

    LogPrintf("RGP ConnectSocketByName Debug 001  \n");

    CService addrResolved(CNetAddr(strDest, fNameLookup && !HaveNameProxy()), port);
    if (addrResolved.IsValid()) 
    {
        addr = addrResolved;
        return ConnectSocket(addr, hSocketRet, nTimeout);
    }

 LogPrintf("RGP ConnectSocketByName Debug 002  \n");

    addr = CService("0.0.0.0:0");

    if (!HaveNameProxy())
        return false;

     LogPrintf("RGP ConnectSocketByName Debug 003  \n");

    return ConnectThroughProxy(nameProxy, strDest, port, hSocketRet, nTimeout, outProxyConnectionFailed);
}


void CNetAddr::Init()
{
    memset(ip, 0, sizeof(ip));
}

void CNetAddr::SetIP(const CNetAddr& ipIn)
{
    memcpy(ip, ipIn.ip, sizeof(ip));
}

void CNetAddr::SetRaw(Network network, const uint8_t *ip_in)
{
    switch(network)
    {
        case NET_IPV4:
            memcpy(ip, pchIPv4, 12);
            memcpy(ip+12, ip_in, 4);
            break;
        case NET_IPV6:
            memcpy(ip, ip_in, 16);
            break;
        default:
            assert(!"invalid network");
    }
}

static const unsigned char pchOnionCat[] = {0xFD,0x87,0xD8,0x7E,0xEB,0x43};
static const unsigned char pchGarliCat[] = {0xFD,0x60,0xDB,0x4D,0xDD,0xB5};

bool CNetAddr::SetSpecial(const std::string &strName)
{
    if (strName.size()>6 && strName.substr(strName.size() - 6, 6) == ".onion") {
        std::vector<unsigned char> vchAddr = DecodeBase32(strName.substr(0, strName.size() - 6).c_str());
        if (vchAddr.size() != 16-sizeof(pchOnionCat))
            return false;
        memcpy(ip, pchOnionCat, sizeof(pchOnionCat));
        for (unsigned int i=0; i<16-sizeof(pchOnionCat); i++)
            ip[i + sizeof(pchOnionCat)] = vchAddr[i];
        return true;
    }
    if (strName.size()>11 && strName.substr(strName.size() - 11, 11) == ".oc.b32.i2p") {
        std::vector<unsigned char> vchAddr = DecodeBase32(strName.substr(0, strName.size() - 11).c_str());
        if (vchAddr.size() != 16-sizeof(pchGarliCat))
            return false;
        memcpy(ip, pchOnionCat, sizeof(pchGarliCat));
        for (unsigned int i=0; i<16-sizeof(pchGarliCat); i++)
            ip[i + sizeof(pchGarliCat)] = vchAddr[i];
        return true;
    }
    return false;
}

CNetAddr::CNetAddr()
{
    Init();
}

CNetAddr::CNetAddr(const struct in_addr& ipv4Addr)
{
    SetRaw(NET_IPV4, (const uint8_t*)&ipv4Addr);
}

CNetAddr::CNetAddr(const struct in6_addr& ipv6Addr)
{
    SetRaw(NET_IPV6, (const uint8_t*)&ipv6Addr);
}

CNetAddr::CNetAddr(const char *pszIp, bool fAllowLookup)
{
    Init();
    std::vector<CNetAddr> vIP;
    if (LookupHost(pszIp, vIP, 1, fAllowLookup))
        *this = vIP[0];
}

CNetAddr::CNetAddr(const std::string &strIp, bool fAllowLookup)
{
    Init();
    std::vector<CNetAddr> vIP;
    if (LookupHost(strIp.c_str(), vIP, 1, fAllowLookup))
        *this = vIP[0];
}

unsigned int CNetAddr::GetByte(int n) const
{
    return ip[15-n];
}

bool CNetAddr::IsIPv4() const
{
    return (memcmp(ip, pchIPv4, sizeof(pchIPv4)) == 0);
}

bool CNetAddr::IsIPv6() const
{
    return (!IsIPv4() && !IsTor() && !IsI2P());
}

bool CNetAddr::IsRFC1918() const
{
    return IsIPv4() && (
        GetByte(3) == 10 ||
        (GetByte(3) == 192 && GetByte(2) == 168) ||
        (GetByte(3) == 172 && (GetByte(2) >= 16 && GetByte(2) <= 31)));
}

bool CNetAddr::IsRFC3927() const
{
    return IsIPv4() && (GetByte(3) == 169 && GetByte(2) == 254);
}

bool CNetAddr::IsRFC3849() const
{
    return GetByte(15) == 0x20 && GetByte(14) == 0x01 && GetByte(13) == 0x0D && GetByte(12) == 0xB8;
}

bool CNetAddr::IsRFC3964() const
{
    return (GetByte(15) == 0x20 && GetByte(14) == 0x02);
}

bool CNetAddr::IsRFC6052() const
{
    static const unsigned char pchRFC6052[] = {0,0x64,0xFF,0x9B,0,0,0,0,0,0,0,0};
    return (memcmp(ip, pchRFC6052, sizeof(pchRFC6052)) == 0);
}

bool CNetAddr::IsRFC4380() const
{
    return (GetByte(15) == 0x20 && GetByte(14) == 0x01 && GetByte(13) == 0 && GetByte(12) == 0);
}

bool CNetAddr::IsRFC4862() const
{
    static const unsigned char pchRFC4862[] = {0xFE,0x80,0,0,0,0,0,0};
    return (memcmp(ip, pchRFC4862, sizeof(pchRFC4862)) == 0);
}

bool CNetAddr::IsRFC4193() const
{
    return ((GetByte(15) & 0xFE) == 0xFC);
}

bool CNetAddr::IsRFC6145() const
{
    static const unsigned char pchRFC6145[] = {0,0,0,0,0,0,0,0,0xFF,0xFF,0,0};
    return (memcmp(ip, pchRFC6145, sizeof(pchRFC6145)) == 0);
}

bool CNetAddr::IsRFC4843() const
{
    return (GetByte(15) == 0x20 && GetByte(14) == 0x01 && GetByte(13) == 0x00 && (GetByte(12) & 0xF0) == 0x10);
}

bool CNetAddr::IsTor() const
{
    return (memcmp(ip, pchOnionCat, sizeof(pchOnionCat)) == 0);
}

bool CNetAddr::IsI2P() const
{
    return (memcmp(ip, pchGarliCat, sizeof(pchGarliCat)) == 0);
}

bool CNetAddr::IsLocal() const
{
    // IPv4 loopback
   if (IsIPv4() && (GetByte(3) == 127 || GetByte(3) == 0))
       return true;

   // IPv6 loopback (::1/128)
   static const unsigned char pchLocal[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
   if (memcmp(ip, pchLocal, 16) == 0)
       return true;

   return false;
}

bool CNetAddr::IsMulticast() const
{
    return    (IsIPv4() && (GetByte(3) & 0xF0) == 0xE0)
           || (GetByte(15) == 0xFF);
}

bool CNetAddr::IsValid() const
{
    // Cleanup 3-byte shifted addresses caused by garbage in size field
    // of addr messages from versions before 0.2.9 checksum.
    // Two consecutive addr messages look like this:
    // header20 vectorlen3 addr26 addr26 addr26 header20 vectorlen3 addr26 addr26 addr26...
    // so if the first length field is garbled, it reads the second batch
    // of addr misaligned by 3 bytes.
    if (memcmp(ip, pchIPv4+3, sizeof(pchIPv4)-3) == 0)
        return false;

    // unspecified IPv6 address (::/128)
    unsigned char ipNone[16] = {};
    if (memcmp(ip, ipNone, 16) == 0)
        return false;

    // documentation IPv6 address
    if (IsRFC3849())
        return false;

    if (IsIPv4())
    {
        // INADDR_NONE
        uint32_t ipNone = INADDR_NONE;
        if (memcmp(ip+12, &ipNone, 4) == 0)
            return false;

        // 0
        ipNone = 0;
        if (memcmp(ip+12, &ipNone, 4) == 0)
            return false;
    }

    return true;
}

bool CNetAddr::IsRoutable() const
{
    return IsValid() && !(IsRFC1918() || IsRFC3927() || IsRFC4862() || (IsRFC4193() && !IsTor() && !IsI2P()) || IsRFC4843() || IsLocal());
}

enum Network CNetAddr::GetNetwork() const
{
    if (!IsRoutable())
        return NET_UNROUTABLE;

    if (IsIPv4())
        return NET_IPV4;

    if (IsTor())
        return NET_TOR;

    if (IsI2P())
        return NET_I2P;

    return NET_IPV6;
}

std::string CNetAddr::ToStringIP() const
{
    if (IsTor())
        return EncodeBase32(&ip[6], 10) + ".onion";
    if (IsI2P())
        return EncodeBase32(&ip[6], 10) + ".oc.b32.i2p";
    CService serv(*this, 0);
    struct sockaddr_storage sockaddr;
    socklen_t socklen = sizeof(sockaddr);
    if (serv.GetSockAddr((struct sockaddr*)&sockaddr, &socklen)) {
        char name[1025] = "";
        if (!getnameinfo((const struct sockaddr*)&sockaddr, socklen, name, sizeof(name), NULL, 0, NI_NUMERICHOST))
            return std::string(name);
    }
    if (IsIPv4())
        return strprintf("%u.%u.%u.%u", GetByte(3), GetByte(2), GetByte(1), GetByte(0));
    else
        return strprintf("%x:%x:%x:%x:%x:%x:%x:%x",
                         GetByte(15) << 8 | GetByte(14), GetByte(13) << 8 | GetByte(12),
                         GetByte(11) << 8 | GetByte(10), GetByte(9) << 8 | GetByte(8),
                         GetByte(7) << 8 | GetByte(6), GetByte(5) << 8 | GetByte(4),
                         GetByte(3) << 8 | GetByte(2), GetByte(1) << 8 | GetByte(0));
}

std::string CNetAddr::ToString() const
{
    return ToStringIP();
}

bool operator==(const CNetAddr& a, const CNetAddr& b)
{
    return (memcmp(a.ip, b.ip, 16) == 0);
}

bool operator!=(const CNetAddr& a, const CNetAddr& b)
{
    return (memcmp(a.ip, b.ip, 16) != 0);
}

bool operator<(const CNetAddr& a, const CNetAddr& b)
{
    return (memcmp(a.ip, b.ip, 16) < 0);
}

bool CNetAddr::GetInAddr(struct in_addr* pipv4Addr) const
{
    if (!IsIPv4())
        return false;
    memcpy(pipv4Addr, ip+12, 4);
    return true;
}

bool CNetAddr::GetIn6Addr(struct in6_addr* pipv6Addr) const
{
    memcpy(pipv6Addr, ip, 16);
    return true;
}

// get canonical identifier of an address' group
// no two connections will be attempted to addresses with the same group
std::vector<unsigned char> CNetAddr::GetGroup() const
{
    std::vector<unsigned char> vchRet;
    int nClass = NET_IPV6;
    int nStartByte = 0;
    int nBits = 16;

    // all local addresses belong to the same group
    if (IsLocal())
    {
        nClass = 255;
        nBits = 0;
    }

    // all unroutable addresses belong to the same group
    if (!IsRoutable())
    {
        nClass = NET_UNROUTABLE;
        nBits = 0;
    }
    // for IPv4 addresses, '1' + the 16 higher-order bits of the IP
    // includes mapped IPv4, SIIT translated IPv4, and the well-known prefix
    else if (IsIPv4() || IsRFC6145() || IsRFC6052())
    {
        nClass = NET_IPV4;
        nStartByte = 12;
    }
    // for 6to4 tunnelled addresses, use the encapsulated IPv4 address
    else if (IsRFC3964())
    {
        nClass = NET_IPV4;
        nStartByte = 2;
    }
    // for Teredo-tunnelled IPv6 addresses, use the encapsulated IPv4 address
    else if (IsRFC4380())
    {
        vchRet.push_back(NET_IPV4);
        vchRet.push_back(GetByte(3) ^ 0xFF);
        vchRet.push_back(GetByte(2) ^ 0xFF);
        return vchRet;
    }
    else if (IsTor())
    {
        nClass = NET_TOR;
        nStartByte = 6;
        nBits = 4;
    }
    else if (IsI2P())
    {
        nClass = NET_I2P;
        nStartByte = 6;
        nBits = 4;
    }
    // for he.net, use /36 groups
    else if (GetByte(15) == 0x20 && GetByte(14) == 0x01 && GetByte(13) == 0x04 && GetByte(12) == 0x70)
        nBits = 36;
    // for the rest of the IPv6 network, use /32 groups
    else
        nBits = 32;

    vchRet.push_back(nClass);
    while (nBits >= 8)
    {
        vchRet.push_back(GetByte(15 - nStartByte));
        nStartByte++;
        nBits -= 8;
    }
    if (nBits > 0)
        vchRet.push_back(GetByte(15 - nStartByte) | ((1 << nBits) - 1));

    return vchRet;
}

uint64_t CNetAddr::GetHash() const
{
    uint256 hash = Hash(&ip[0], &ip[16]);
    uint64_t nRet;
    memcpy(&nRet, &hash, sizeof(nRet));
    return nRet;
}

void CNetAddr::print() const
{
    LogPrintf("CNetAddr(%s)\n", ToString());
}

// private extensions to enum Network, only returned by GetExtNetwork,
// and only used in GetReachabilityFrom
static const int NET_UNKNOWN = NET_MAX + 0;
static const int NET_TEREDO  = NET_MAX + 1;
int static GetExtNetwork(const CNetAddr *addr)
{
    if (addr == NULL)
        return NET_UNKNOWN;
    if (addr->IsRFC4380())
        return NET_TEREDO;
    return addr->GetNetwork();
}

/** Calculates a metric for how reachable (*this) is from a given partner */
int CNetAddr::GetReachabilityFrom(const CNetAddr *paddrPartner) const
{
    enum Reachability {
        REACH_UNREACHABLE,
        REACH_DEFAULT,
        REACH_TEREDO,
        REACH_IPV6_WEAK,
        REACH_IPV4,
        REACH_IPV6_STRONG,
        REACH_PRIVATE
    };

    if (!IsRoutable())
        return REACH_UNREACHABLE;

    int ourNet = GetExtNetwork(this);
    int theirNet = GetExtNetwork(paddrPartner);
    bool fTunnel = IsRFC3964() || IsRFC6052() || IsRFC6145();

    switch(theirNet) {
    case NET_IPV4:
        switch(ourNet) {
        default:       return REACH_DEFAULT;
        case NET_IPV4: return REACH_IPV4;
        }
    case NET_IPV6:
        switch(ourNet) {
        default:         return REACH_DEFAULT;
        case NET_TEREDO: return REACH_TEREDO;
        case NET_IPV4:   return REACH_IPV4;
        case NET_IPV6:   return fTunnel ? REACH_IPV6_WEAK : REACH_IPV6_STRONG; // only prefer giving our IPv6 address if it's not tunnelled
        }
    case NET_TOR:
        switch(ourNet) {
        default:         return REACH_DEFAULT;
        case NET_IPV4:   return REACH_IPV4; // Tor users can connect to IPv4 as well
        case NET_TOR:    return REACH_PRIVATE;
        }
    case NET_I2P:
        switch(ourNet) {
        default:         return REACH_DEFAULT;
        case NET_I2P:    return REACH_PRIVATE;
        }
    case NET_TEREDO:
        switch(ourNet) {
        default:          return REACH_DEFAULT;
        case NET_TEREDO:  return REACH_TEREDO;
        case NET_IPV6:    return REACH_IPV6_WEAK;
        case NET_IPV4:    return REACH_IPV4;
        }
    case NET_UNKNOWN:
    case NET_UNROUTABLE:
    default:
        switch(ourNet) {
        default:          return REACH_DEFAULT;
        case NET_TEREDO:  return REACH_TEREDO;
        case NET_IPV6:    return REACH_IPV6_WEAK;
        case NET_IPV4:    return REACH_IPV4;
        case NET_I2P:     return REACH_PRIVATE; // assume connections from unroutable addresses are
        case NET_TOR:     return REACH_PRIVATE; // either from Tor/I2P, or don't care about our address
        }
    }
}

void CService::Init()
{
    port = 0;
}

CService::CService()
{
    Init();
}

CService::CService(const CNetAddr& cip, unsigned short portIn) : CNetAddr(cip), port(portIn)
{
}

CService::CService(const struct in_addr& ipv4Addr, unsigned short portIn) : CNetAddr(ipv4Addr), port(portIn)
{
}

CService::CService(const struct in6_addr& ipv6Addr, unsigned short portIn) : CNetAddr(ipv6Addr), port(portIn)
{
}

CService::CService(const struct sockaddr_in& addr) : CNetAddr(addr.sin_addr), port(ntohs(addr.sin_port))
{
    assert(addr.sin_family == AF_INET);
}

CService::CService(const struct sockaddr_in6 &addr) : CNetAddr(addr.sin6_addr), port(ntohs(addr.sin6_port))
{
   assert(addr.sin6_family == AF_INET6);
}

bool CService::SetSockAddr(const struct sockaddr *paddr)
{
    switch (paddr->sa_family) {
    case AF_INET:
        *this = CService(*(const struct sockaddr_in*)paddr);
        return true;
    case AF_INET6:
        *this = CService(*(const struct sockaddr_in6*)paddr);
        return true;
    default:
        return false;
    }
}

CService::CService(const char *pszIpPort, bool fAllowLookup)
{
    Init();
    CService ip;
    if (Lookup(pszIpPort, ip, 0, fAllowLookup))
        *this = ip;
}

CService::CService(const char *pszIpPort, int portDefault, bool fAllowLookup)
{
    Init();
    CService ip;
    if (Lookup(pszIpPort, ip, portDefault, fAllowLookup))
        *this = ip;
}

CService::CService(const std::string &strIpPort, bool fAllowLookup)
{
    Init();
    CService ip;
    if (Lookup(strIpPort.c_str(), ip, 0, fAllowLookup))
        *this = ip;
}

CService::CService(const std::string &strIpPort, int portDefault, bool fAllowLookup)
{
    Init();
    CService ip;
    if (Lookup(strIpPort.c_str(), ip, portDefault, fAllowLookup))
        *this = ip;
}

unsigned short CService::GetPort() const
{
    return port;
}

bool operator==(const CService& a, const CService& b)
{
    return (CNetAddr)a == (CNetAddr)b && a.port == b.port;
}

bool operator!=(const CService& a, const CService& b)
{
    return (CNetAddr)a != (CNetAddr)b || a.port != b.port;
}

bool operator<(const CService& a, const CService& b)
{
    return (CNetAddr)a < (CNetAddr)b || ((CNetAddr)a == (CNetAddr)b && a.port < b.port);
}

bool CService::GetSockAddr(struct sockaddr* paddr, socklen_t *addrlen) const
{
    if (IsIPv4()) {
        if (*addrlen < (socklen_t)sizeof(struct sockaddr_in))
            return false;
        *addrlen = sizeof(struct sockaddr_in);
        struct sockaddr_in *paddrin = (struct sockaddr_in*)paddr;
        memset(paddrin, 0, *addrlen);
        if (!GetInAddr(&paddrin->sin_addr))
            return false;
        paddrin->sin_family = AF_INET;
        paddrin->sin_port = htons(port);
        return true;
    }
    if (IsIPv6()) {
        if (*addrlen < (socklen_t)sizeof(struct sockaddr_in6))
            return false;
        *addrlen = sizeof(struct sockaddr_in6);
        struct sockaddr_in6 *paddrin6 = (struct sockaddr_in6*)paddr;
        memset(paddrin6, 0, *addrlen);
        if (!GetIn6Addr(&paddrin6->sin6_addr))
            return false;
        paddrin6->sin6_family = AF_INET6;
        paddrin6->sin6_port = htons(port);
        return true;
    }
    return false;
}

std::vector<unsigned char> CService::GetKey() const
{
     std::vector<unsigned char> vKey;
     vKey.resize(18);
     memcpy(&vKey[0], ip, 16);
     vKey[16] = port / 0x100;
     vKey[17] = port & 0x0FF;
     return vKey;
}

std::string CService::ToStringPort() const
{
    return strprintf("%u", port);
}

std::string CService::ToStringIPPort() const
{
    if (IsIPv4() || IsTor() || IsI2P()) {
        return ToStringIP() + ":" + ToStringPort();
    } else {
        return "[" + ToStringIP() + "]:" + ToStringPort();
    }
}

std::string CService::ToString() const
{
    return ToStringIPPort();
}

void CService::print() const
{
    LogPrintf("CService(%s)\n", ToString());
}

void CService::SetPort(unsigned short portIn)
{
    port = portIn;
}

CSubNet::CSubNet():
    valid(false)
{
    memset(netmask, 0, sizeof(netmask));
}

CSubNet::CSubNet(const std::string &strSubnet, bool fAllowLookup)
{
    size_t slash = strSubnet.find_last_of('/');
    std::vector<CNetAddr> vIP;

    valid = true;
    // Default to /32 (IPv4) or /128 (IPv6), i.e. match single address
    memset(netmask, 255, sizeof(netmask));

    std::string strAddress = strSubnet.substr(0, slash);
    if (LookupHost(strAddress.c_str(), vIP, 1, fAllowLookup))
    {
        network = vIP[0];
        if (slash != strSubnet.npos)
        {
            std::string strNetmask = strSubnet.substr(slash + 1);
            int32_t n;
            // IPv4 addresses start at offset 12, and first 12 bytes must match, so just offset n
            int noffset = network.IsIPv4() ? (12 * 8) : 0;
            if (ParseInt32(strNetmask, &n)) // If valid number, assume /24 symtex
            {
                if(n >= 0 && n <= (128 - noffset)) // Only valid if in range of bits of address
                {
                    n += noffset;
                    // Clear bits [n..127]
                    for (; n < 128; ++n)
                        netmask[n>>3] &= ~(1<<(n&7));
                }
                else
                {
                    valid = false;
                }
            }
            else // If not a valid number, try full netmask syntax
            {
                if (LookupHost(strNetmask.c_str(), vIP, 1, false)) // Never allow lookup for netmask
                {
                    // Remember: GetByte returns bytes in reversed order
                    // Copy only the *last* four bytes in case of IPv4, the rest of the mask should stay 1's as
                    // we don't want pchIPv4 to be part of the mask.
                    int asize = network.IsIPv4() ? 4 : 16;
                    for(int x=0; x<asize; ++x)
                        netmask[15-x] = vIP[0].GetByte(x);
                }
                else
                {
                    valid = false;
                }
            }
        }
    }
    else
    {
        valid = false;
    }
}

bool CSubNet::Match(const CNetAddr &addr) const
{
    if (!valid || !addr.IsValid())
        return false;
    for(int x=0; x<16; ++x)
        if ((addr.GetByte(x) & netmask[15-x]) != network.GetByte(x))
            return false;
    return true;
}

static inline int NetmaskBits(uint8_t x)
{
    switch(x) {
    case 0x00: return 0; break;
    case 0x80: return 1; break;
    case 0xc0: return 2; break;
    case 0xe0: return 3; break;
    case 0xf0: return 4; break;
    case 0xf8: return 5; break;
    case 0xfc: return 6; break;
    case 0xfe: return 7; break;
    case 0xff: return 8; break;
    default: return -1; break;
    }
}

std::string CSubNet::ToString() const
{
    /* Parse binary 1{n}0{N-n} to see if mask can be represented as /n */
    int cidr = 0;
    bool valid_cidr = true;
    int n = network.IsIPv4() ? 12 : 0;
    for (; n < 16 && netmask[n] == 0xff; ++n)
        cidr += 8;
    if (n < 16) {
        int bits = NetmaskBits(netmask[n]);
        if (bits < 0)
            valid_cidr = false;
        else
            cidr += bits;
        ++n;
    }
    for (; n < 16 && valid_cidr; ++n)
        if (netmask[n] != 0x00)
            valid_cidr = false;

    /* Format output */
    std::string strNetmask;
    if (valid_cidr) {
        strNetmask = strprintf("%u", cidr);
    } else {
        if (network.IsIPv4())
            strNetmask = strprintf("%u.%u.%u.%u", netmask[12], netmask[13], netmask[14], netmask[15]);
        else
            strNetmask = strprintf("%x:%x:%x:%x:%x:%x:%x:%x",
                             netmask[0] << 8 | netmask[1], netmask[2] << 8 | netmask[3],
                             netmask[4] << 8 | netmask[5], netmask[6] << 8 | netmask[7],
                             netmask[8] << 8 | netmask[9], netmask[10] << 8 | netmask[11],
                             netmask[12] << 8 | netmask[13], netmask[14] << 8 | netmask[15]);
    }

    return network.ToString() + "/" + strNetmask;
}

bool CSubNet::IsValid() const
{
    return valid;
}

bool operator==(const CSubNet& a, const CSubNet& b)
{
    return a.valid == b.valid && a.network == b.network && !memcmp(a.netmask, b.netmask, 16);
}

bool operator!=(const CSubNet& a, const CSubNet& b)
{
    return !(a==b);
}

bool operator<(const CSubNet& a, const CSubNet& b)
{
    return (a.network < b.network || (a.network == b.network && memcmp(a.netmask, b.netmask, 16) < 0));
}
#ifdef WIN32
std::string NetworkErrorString(int err)
{
    char buf[256];
    buf[0] = 0;
    if(FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
            NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            buf, sizeof(buf), NULL))
    {
        return strprintf("%s (%d)", buf, err);
    }
    else
    {
        return strprintf("Unknown error (%d)", err);
    }
}
#else
std::string NetworkErrorString(int err)
{
    char buf[256];
    const char *s = buf;
    buf[0] = 0;
    /* Too bad there are two incompatible implementations of the
     * thread-safe strerror. */
#ifdef STRERROR_R_CHAR_P /* GNU variant can return a pointer outside the passed buffer */
    s = strerror_r(err, buf, sizeof(buf));
#else /* POSIX variant always returns message in buffer */
    (void) strerror_r(err, buf, sizeof(buf));
#endif
    return strprintf("%s (%d)", s, err);
}
#endif
