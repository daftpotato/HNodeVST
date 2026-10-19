#include "SpoutOut.h"

// SpoutLibrary.h pulls in d3d11.h / dxgiformat.h; the Windows SDK include dir comes
// from vcvars64.
#include "SpoutLibrary.h"

#include <cstdio>

namespace HNodeVST
{

static SPOUTHANDLE gSpout = nullptr;
static int gInitCount = 0;

bool SpoutSender::Send(const uint8_t* rgba, int width, int height,
                       const std::string& name)
{
    if (!rgba || width <= 0 || height <= 0) return false;

    if (!loaded)
    {
        gSpout = GetSpout();
        if (!gSpout) { lastError = "GetSpout() failed"; return false; }
        loaded = true;
    }

    // Dimensions or name changed -> rebuild the sender.
    if (created && (width != curW || height != curH || name != senderName))
    {
        gSpout->ReleaseSender();
        created = false;
        curW = curH = 0;
        senderName.clear();
    }

    if (!created)
    {
        gSpout->SetSenderName(name.c_str());
        if (!gSpout->CreateSender(name.c_str(), (unsigned)width, (unsigned)height))
        {
            lastError = "CreateSender failed";
            return false;
        }
        curW = width; curH = height;
        senderName = name;
        created = true;
    }

    // GL_RGBA + bInvert=false preserves HNode's exact row order.
    if (!gSpout->SendImage(rgba, (unsigned)width, (unsigned)height, GL_RGBA, false))
    {
        lastError = "SendImage failed";
        return false;
    }
    lastError.clear();
    return true;
}

void SpoutSender::Shutdown()
{
    if (created && gSpout)
    {
        gSpout->ReleaseSender();
        created = false;
        curW = curH = 0;
        senderName.clear();
    }
}

} // namespace HNodeVST
