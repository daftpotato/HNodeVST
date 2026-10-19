// HNodeVST - Spout2 sender.
//
// Row-order note (verified against SpoutGL's spoutCopy::rgba2rgba):
//   bInvert == false  ->  source row y lands on D3D texture row y (row 0 = top).
// HNode's TextureWriter writes index 0 == Unity texture row 0 == image top, and our
// pixel buffer reproduces that byte layout 1:1, so bInvert must be false.
#pragma once

#include "Common.h"

namespace HNodeVST
{

class SpoutSender
{
public:
    SpoutSender() = default;
    ~SpoutSender() { Shutdown(); }

    bool IsAvailable() const { return loaded; }

    // Creates (or recreates) the sender. Safe to call every frame; it no-ops when
    // the size and name have not changed.
    bool Send(const uint8_t* rgba, int width, int height, const std::string& name);

    void Shutdown();

    const std::string& LastError() const { return lastError; }
    const std::string& SenderName() const { return senderName; }

private:
    bool loaded = false;
    bool created = false;
    int  curW = 0, curH = 0;
    std::string senderName;
    std::string lastError;
};

} // namespace HNodeVST
