#include "ArtNet.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#endif

#include <cstdio>

namespace HNodeVST
{

static const char kArtNetId[8] = {'A','r','t','-','N','e','t',0};

bool ArtNet::StartReceiver(int port, const std::string& bindAddr, OnUniverse cb)
{
    callback = cb;

#ifdef _WIN32
    sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == (uintptr_t)INVALID_SOCKET) return false;

    // Art-Net nodes need broadcast + reuse so several binders can share 6454.
    int reuse = 1;
    ::setsockopt((SOCKET)sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    int bcast = 1;
    ::setsockopt((SOCKET)sock, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast, sizeof(bcast));

    // Generous buffer: a 512-channel packet is ~530 bytes; bursts come fast.
    int rcvbuf = 1 << 20;
    ::setsockopt((SOCKET)sock, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    if (bindAddr.empty() || bindAddr == "0.0.0.0" || bindAddr == "255.255.255.255")
        addr.sin_addr.s_addr = INADDR_ANY;
    else
        ::inet_pton(AF_INET, bindAddr.c_str(), &addr.sin_addr);

    if (::bind((SOCKET)sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        ::closesocket((SOCKET)sock);
        sock = (uintptr_t)-1;
        return false;
    }

    stopFlag = false;
    running = true;
    thread = std::thread([this, port, bindAddr]() { ThreadMain(port, bindAddr); });
    return true;
#else
    (void)port; (void)bindAddr;
    return false;
#endif
}

void ArtNet::Shutdown()
{
    stopFlag = true;
    running = false;
#ifdef _WIN32
    if (sock != (uintptr_t)-1)
    {
        ::closesocket((SOCKET)sock);
        sock = (uintptr_t)-1;
    }
#endif
    if (thread.joinable()) thread.join();
}

void ArtNet::ThreadMain(int port, std::string bindAddr)
{
#ifdef _WIN32
    (void)port; (void)bindAddr;
    std::vector<uint8_t> buf(2048);
    DWORD tv = 200;   // ms, so Shutdown() does not hang on a blocking recv
    ::setsockopt((SOCKET)sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    while (!stopFlag)
    {
        int n = ::recv((SOCKET)sock, (char*)buf.data(), (int)buf.size(), 0);
        if (n <= 0) continue;

        if (n < 18) continue;
        if (std::memcmp(buf.data(), kArtNetId, 8) != 0) continue;

        // OpCode is little-endian on the wire: 0x5000 == OpOutput / OpDmx
        uint16_t op = (uint16_t)(buf[8] | (buf[9] << 8));
        if (op != 0x5000) continue;

        // Protocol version is big-endian, must be >= 14
        uint16_t ver = (uint16_t)((buf[10] << 8) | buf[11]);
        if (ver < 14) continue;

        uint16_t universe = (uint16_t)(buf[14] | (buf[15] << 8));
        uint16_t length   = (uint16_t)((buf[16] << 8) | buf[17]);

        if (length > 512) length = 512;
        int avail = n - 18;
        if (avail < 0) avail = 0;
        if (length > (uint16_t)avail) length = (uint16_t)avail;

        if (callback && length > 0)
            callback(universe, buf.data() + 18, length);
    }
#endif
}

bool ArtNet::SendUniverse(const std::string& host, int port,
                          uint16_t universe, const uint8_t* data, int len,
                          uint8_t sequence)
{
    if (len <= 0 || len > 512) len = 512;

#ifdef _WIN32
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;

    int bcast = 1;
    ::setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast, sizeof(bcast));

    std::vector<uint8_t> pkt(18 + len);
    std::memcpy(pkt.data(), kArtNetId, 8);
    pkt[8]  = 0x00;            // OpCode lo
    pkt[9]  = 0x50;            // OpCode hi  -> 0x5000
    pkt[10] = 0x00;            // Protocol hi
    pkt[11] = 14;              // Protocol lo
    pkt[12] = sequence;
    pkt[13] = 0;               // Physical
    pkt[14] = (uint8_t)(universe & 0xFF);
    pkt[15] = (uint8_t)(universe >> 8);
    pkt[16] = (uint8_t)(len >> 8);
    pkt[17] = (uint8_t)(len & 0xFF);
    std::memcpy(pkt.data() + 18, data, len);

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    ::inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    int sent = ::sendto(s, (const char*)pkt.data(), (int)pkt.size(), 0,
                        (sockaddr*)&addr, sizeof(addr));
    ::closesocket(s);
    return sent == (int)pkt.size();
#else
    (void)host; (void)port; (void)universe; (void)data; (void)len; (void)sequence;
    return false;
#endif
}

} // namespace HNodeVST
