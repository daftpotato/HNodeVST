// HNodeVST - minimal OSC 1.0 sender/receiver.
// VRSL's editor-only GridReader listens on OSCPort+1 and expects the /VRSL prefix.
#pragma once

#include "Common.h"
#include <thread>
#include <functional>

namespace HNodeVST
{

class Osc
{
public:
    using OnMessage = std::function<void(const std::string& address,
                                         const std::vector<int>& ints,
                                         const std::vector<float>& floats)>;

    Osc() = default;
    ~Osc() { Shutdown(); }

    bool StartReceiver(int port, OnMessage cb);
    void Shutdown();

    bool IsReceiverRunning() const { return running.load(); }

    // Synchronous send helpers.
    static bool SendInts(const std::string& host, int port,
                         const std::string& address,
                         const std::vector<int>& ints);
    static bool SendInt(const std::string& host, int port,
                        const std::string& address, int value);

private:
    void ThreadMain(int port);

    std::atomic<bool> stopFlag{false};
    std::atomic<bool> running{false};
    std::thread thread;
    OnMessage callback;

#ifdef _WIN32
    uintptr_t sock = (uintptr_t)-1;
#endif
};

// Encode/decode helpers (also used to build ArtNet-adjacent payloads).
std::vector<uint8_t> OscEncodeMessage(const std::string& address,
                                      const std::vector<int>& ints,
                                      const std::vector<float>& floats);
bool OscDecodeMessage(const uint8_t* buf, int len, std::string& address,
                      std::vector<int>& ints, std::vector<float>& floats);

} // namespace HNodeVST
