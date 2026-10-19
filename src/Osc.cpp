#include "Osc.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#endif

namespace HNodeVST
{

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------
static void Pad4(std::vector<uint8_t>& b)
{
    while (b.size() % 4 != 0) b.push_back(0);
}

static void AppendPaddedString(std::vector<uint8_t>& b, const std::string& s)
{
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
    while (b.size() % 4 != 0) b.push_back(0);
}

std::vector<uint8_t> OscEncodeMessage(const std::string& address,
                                      const std::vector<int>& ints,
                                      const std::vector<float>& floats)
{
    std::vector<uint8_t> out;
    AppendPaddedString(out, address);

    std::string tags = ",";
    for (size_t i = 0; i < ints.size();  ++i) tags += "i";
    for (size_t i = 0; i < floats.size(); ++i) tags += "f";
    AppendPaddedString(out, tags);

    for (int v : ints)
    {
        uint8_t t[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                         (uint8_t)(v >> 8),  (uint8_t)v };
        out.insert(out.end(), t, t + 4);
    }
    for (float f : floats)
    {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        uint8_t t[4] = { (uint8_t)(u >> 24), (uint8_t)(u >> 16),
                         (uint8_t)(u >> 8),  (uint8_t)u };
        out.insert(out.end(), t, t + 4);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------
bool OscDecodeMessage(const uint8_t* buf, int len, std::string& address,
                      std::vector<int>& ints, std::vector<float>& floats)
{
    ints.clear(); floats.clear();
    if (!buf || len <= 0) return false;

    int pos = 0;
    auto readStr = [&](std::string& s) -> bool {
        int start = pos;
        while (pos < len && buf[pos] != 0) ++pos;
        if (pos >= len) return false;
        s.assign((const char*)buf + start, pos - start);
        ++pos;
        while (pos % 4 != 0) ++pos;
        return true;
    };

    if (!readStr(address)) return false;
    if (address.empty() || address[0] != '/') return false;

    std::string tags;
    if (!readStr(tags)) return false;
    if (tags.empty() || tags[0] != ',') return false;

    for (size_t i = 1; i < tags.size(); ++i)
    {
        char c = tags[i];
        if (pos + 4 > len) break;
        uint32_t u = ((uint32_t)buf[pos] << 24) | ((uint32_t)buf[pos+1] << 16) |
                     ((uint32_t)buf[pos+2] << 8) | (uint32_t)buf[pos+3];
        pos += 4;
        if (c == 'i') ints.push_back((int32_t)u);
        else if (c == 'f') { float f; std::memcpy(&f, &u, 4); floats.push_back(f); }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------
bool Osc::StartReceiver(int port, OnMessage cb)
{
    callback = cb;
#ifdef _WIN32
    sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == (uintptr_t)INVALID_SOCKET) return false;

    int reuse = 1;
    ::setsockopt((SOCKET)sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind((SOCKET)sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        ::closesocket((SOCKET)sock);
        sock = (uintptr_t)-1;
        return false;
    }

    stopFlag = false;
    running = true;
    thread = std::thread([this, port]() { ThreadMain(port); });
    return true;
#else
    (void)port;
    return false;
#endif
}

void Osc::Shutdown()
{
    stopFlag = true;
    running = false;
#ifdef _WIN32
    if (sock != (uintptr_t)-1) { ::closesocket((SOCKET)sock); sock = (uintptr_t)-1; }
#endif
    if (thread.joinable()) thread.join();
}

void Osc::ThreadMain(int port)
{
#ifdef _WIN32
    (void)port;
    std::vector<uint8_t> buf(8192);
    DWORD tv = 200;
    ::setsockopt((SOCKET)sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    while (!stopFlag)
    {
        int n = ::recv((SOCKET)sock, (char*)buf.data(), (int)buf.size(), 0);
        if (n <= 0) continue;
        std::string addr;
        std::vector<int> ints;
        std::vector<float> floats;
        if (OscDecodeMessage(buf.data(), n, addr, ints, floats) && callback)
            callback(addr, ints, floats);
    }
#endif
}

static bool UdpSend(const std::string& host, int port, const uint8_t* data, int len)
{
#ifdef _WIN32
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;
    int bcast = 1;
    ::setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast, sizeof(bcast));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    ::inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    int sent = ::sendto(s, (const char*)data, len, 0, (sockaddr*)&addr, sizeof(addr));
    ::closesocket(s);
    return sent == len;
#else
    (void)host; (void)port; (void)data; (void)len;
    return false;
#endif
}

bool Osc::SendInts(const std::string& host, int port,
                   const std::string& address, const std::vector<int>& ints)
{
    auto pkt = OscEncodeMessage(address, ints, {});
    return UdpSend(host, port, pkt.data(), (int)pkt.size());
}

bool Osc::SendInt(const std::string& host, int port,
                  const std::string& address, int value)
{
    std::vector<int> v{ value };
    return SendInts(host, port, address, v);
}

} // namespace HNodeVST
