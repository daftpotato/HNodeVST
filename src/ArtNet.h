// HNodeVST - Art-Net DMX receiver/sender over UDP.
// Packet layout matches the ArtNet-Unity package HNode uses (naoyakohda).
#pragma once

#include "Common.h"
#include <thread>
#include <functional>

namespace HNodeVST
{

class ArtNet
{
public:
    using OnUniverse = std::function<void(uint16_t universe, const uint8_t* data, int len)>;

    ArtNet() = default;
    ~ArtNet() { Shutdown(); }

    // Bind and start listening. Returns false if the socket could not be opened.
    bool StartReceiver(int port, const std::string& bindAddr, OnUniverse cb);
    void Shutdown();

    // Synchronous single-shot send of one universe.
    static bool SendUniverse(const std::string& host, int port,
                             uint16_t universe, const uint8_t* data, int len,
                             uint8_t sequence);

    bool IsReceiverRunning() const { return running.load(); }

private:
    void ThreadMain(int port, std::string bindAddr);

    std::atomic<bool> running{false};
    std::atomic<bool> stopFlag{false};
    std::thread thread;
    OnUniverse callback;

#ifdef _WIN32
    uintptr_t sock = (uintptr_t)-1;   // SOCKET
#endif
};

} // namespace HNodeVST
