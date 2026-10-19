// HNodeVST - DMX -> video grid serializers.
// Faithful port of HNode Assets/Plugin/Serializers/*.cs
#pragma once

#include "Common.h"

#include <cmath>
#include <algorithm>

namespace HNodeVST
{

// A serializer writes one channel's worth of data into an RGBA8 pixel buffer.
// Pixel origin: index 0 == bottom-left, matching HNode's TextureWriter.PixelToIndex
// (which flips Y before indexing).
class ISerializer
{
public:
    virtual ~ISerializer() {}
    virtual SerializerType type() const = 0;
    virtual void InitFrame() {}
    // Write channel `channel` (global, universe-major) holding `value`.
    virtual void SerializeChannel(std::vector<uint8_t>& pixels, int w, int h,
                                  uint8_t value, int channel) = 0;
    // Flush anything deferred (e.g. MDMX CRC columns).
    virtual void CompleteFrame(std::vector<uint8_t>& pixels, int w, int h,
                               const std::vector<uint8_t>& channels) {}
};

ISerializer* CreateSerializer(SerializerType t, const ShowConfig& cfg);

// Shared helpers ------------------------------------------------------------
namespace detail
{
    // HNode TextureWriter.PixelToIndex: y=0 is the BOTTOM row of the image.
    inline int PixelToIndex(int x, int y, int w, int h)
    {
        if (x < 0 || x >= w || y < 0 || y >= h) return -1;
        y = h - 1 - y;
        return y * w + x;
    }

    inline void MakeColorBlock(std::vector<uint8_t>& px, int w, int h,
                               int x, int y, uint8_t r, uint8_t g, uint8_t b,
                               uint8_t a, int size)
    {
        for (int i = 0; i < size; ++i)
        {
            for (int j = 0; j < size; ++j)
            {
                int idx = PixelToIndex(x + i, y + j, w, h);
                if (idx < 0) return;   // HNode returns out of the whole block on OOB
                px[idx * 4 + 0] = r;
                px[idx * 4 + 1] = g;
                px[idx * 4 + 2] = b;
                px[idx * 4 + 3] = a;
            }
        }
    }

    // MixColorBlock only touches the requested colour channel (VRSL RGB mode).
    enum class ColorChannel { Red, Green, Blue };
    inline void MixColorBlock(std::vector<uint8_t>& px, int w, int h,
                              int x, int y, uint8_t value, ColorChannel c,
                              int size)
    {
        for (int i = 0; i < size; ++i)
        {
            for (int j = 0; j < size; ++j)
            {
                int idx = PixelToIndex(x + i, y + j, w, h);
                if (idx < 0) return;
                int base = idx * 4;
                switch (c)
                {
                    case ColorChannel::Red:   px[base + 0] = value; break;
                    case ColorChannel::Green: px[base + 1] = value; break;
                    case ColorChannel::Blue:  px[base + 2] = value; break;
                }
                px[base + 3] = 255;
            }
        }
    }

    // Util.GetBlockAlpha
    inline uint8_t GetBlockAlpha(uint8_t v, bool autoMaskOnZero)
    {
        if (autoMaskOnZero && v == 0) return 0;
        return 255;
    }

    // Unity Color.linear  ==  sRGB -> linear
    inline uint8_t SrgbToLinearByte(uint8_t v)
    {
        float c = (float)v / 255.0f;
        float lin = (c <= 0.04045f) ? (c / 12.92f)
                                    : powf((c + 0.055f) / 1.055f, 2.4f);
        float out = lin * 255.0f;
        if (out < 0.0f) out = 0.0f;
        if (out > 255.0f) out = 255.0f;
        return (uint8_t)(out + 0.5f);
    }
} // namespace detail

// ===========================================================================
// VRSL - the VRChat standard. blockSize 16, 13 channels per column.
// ===========================================================================
class SerializerVRSL : public ISerializer
{
public:
    SerializerType type() const override { return SerializerType::VRSL; }

    SerializerVRSL(const ShowConfig& c) : cfg(c) {}

    void SerializeChannel(std::vector<uint8_t>& px, int w, int h,
                          uint8_t value, int channel) override
    {
        using namespace detail;
        const int blockSize = 16;

        int x, y;
        GetPositionData(channel, w, h, x, y);

        uint8_t outR = value, outG = value, outB = value;
        if (cfg.gammaCorrection)
        {
            outR = outG = outB = SrgbToLinearByte(value);
        }
        uint8_t alpha = GetBlockAlpha(value, cfg.autoMaskOnZero);

        if (cfg.rgbGridMode)
        {
            uint8_t v;
            ColorChannel cch;
            switch (GetUniverseWrap(channel))
            {
                case 0: v = outR; cch = ColorChannel::Red;   break;
                case 1: v = outG; cch = ColorChannel::Green; break;
                case 2: v = outB; cch = ColorChannel::Blue;  break;
                default: return;
            }
            MixColorBlock(px, w, h, x, y, v, cch, blockSize);
        }
        else
        {
            MakeColorBlock(px, w, h, x, y, outR, outG, outB, alpha, blockSize);
        }
    }

private:
    ShowConfig cfg;

    static const int blockSize     = 16;
    static const int blocksPerCol  = 13;

    void GetPositionData(int channel, int w, int h, int& x, int& y) const
    {
        int channelInUniverse = channel % 512;
        int universe = (channel - channelInUniverse) / 512;

        if (cfg.rgbGridMode)
            universe = universe % 3;

        // (512 / 13) == 39 in integer math, exactly as HNode does it.
        int universeOffset = universe * ((512 / blocksPerCol * 16) + 16);

        int tempY = (channelInUniverse % blocksPerCol) * 16;
        int tempX = ((channelInUniverse / blocksPerCol) * 16) + universeOffset;

        bool isVertical = (cfg.outputConfig == OutputConfig::VerticalLeft ||
                           cfg.outputConfig == OutputConfig::VerticalRight);
        x = isVertical ? tempY : tempX;
        y = isVertical ? tempX : tempY;

        switch (cfg.outputConfig)
        {
            case OutputConfig::HorizontalTop:
                break;
            case OutputConfig::HorizontalBottom:
                y += h - (blocksPerCol * 16);
                break;
            case OutputConfig::VerticalLeft:
                y = h - y - 16;
                break;
            case OutputConfig::VerticalRight:
                y = h - y - 16;
                x += w - (blocksPerCol * 16);
                break;
            default:
                break;
        }
    }

    int GetUniverseWrap(int channel) const
    {
        int universe = channel / 512;
        return universe / 3;
    }
};

// ===========================================================================
// Binary - blockSize 4, 52 blocks per column, 8 bits per channel LSB first.
// ===========================================================================
class SerializerBinary : public ISerializer
{
public:
    SerializerType type() const override { return SerializerType::Binary; }
    SerializerBinary(const ShowConfig& c) : cfg(c) {}

    void SerializeChannel(std::vector<uint8_t>& px, int w, int h,
                          uint8_t value, int channel) override
    {
        using namespace detail;
        const int blockSize = 4;
        const int blocksPerCol = 52;
        uint8_t alpha = GetBlockAlpha(value, cfg.autoMaskOnZero);

        for (int i = 0; i < 8; ++i)
        {
            int newChannel = (channel * 8) + i;
            int x = (newChannel / blocksPerCol) * blockSize;
            int y = (newChannel % blocksPerCol) * blockSize;
            if (x >= w || y >= h) continue;

            uint8_t bit = ((value >> i) & 1) ? 255 : 0;
            MakeColorBlock(px, w, h, x, y, bit, bit, bit, alpha, blockSize);
        }
    }
private:
    ShowConfig cfg;
};

// ===========================================================================
// ColorBinary - 8 bits + 1 dummy, interlaced 3 at a time into RGB.
// ===========================================================================
class SerializerColorBinary : public ISerializer
{
public:
    SerializerType type() const override { return SerializerType::ColorBinary; }
    SerializerColorBinary(const ShowConfig& c) : cfg(c) {}

    void SerializeChannel(std::vector<uint8_t>& px, int w, int h,
                          uint8_t value, int channel) override
    {
        using namespace detail;
        const int blockSize = 4;
        const int blocksPerCol = 52;
        uint8_t alpha = GetBlockAlpha(value, cfg.autoMaskOnZero);

        bool bits[9];
        for (int i = 0; i < 8; ++i) bits[i] = ((value >> i) & 1) != 0;
        bits[8] = false;   // dummy bit for clean interlacing

        for (int i = 0; i < 9; i += 3)
        {
            int newChannel = (channel * 3) + (i / 3);
            int x = (newChannel / blocksPerCol) * blockSize;
            int y = (newChannel % blocksPerCol) * blockSize;
            if (x >= w || y >= h) continue;

            uint8_t r = bits[i + 0] ? 255 : 0;
            uint8_t g = bits[i + 1] ? 255 : 0;
            uint8_t b = bits[i + 2] ? 255 : 0;
            MakeColorBlock(px, w, h, x, y, r, g, b, alpha, blockSize);
        }
    }
private:
    ShowConfig cfg;
};

// ===========================================================================
// Ternary - 6 base-3 digits per channel, mapped to intensity.
// ===========================================================================
class SerializerTernary : public ISerializer
{
public:
    SerializerType type() const override { return SerializerType::Ternary; }
    SerializerTernary(const ShowConfig& c) : cfg(c) {}

    void SerializeChannel(std::vector<uint8_t>& px, int w, int h,
                          uint8_t value, int channel) override
    {
        using namespace detail;
        const int blockSize = 4;
        const int blocksPerCol = 48;   // 8*6
        uint8_t alpha = GetBlockAlpha(value, cfg.autoMaskOnZero);

        int bits[6];
        {
            int N = value;
            int index = 5;
            for (int i = 0; i < 6; ++i) { bits[index--] = N % 3; N /= 3; }
        }

        for (int i = 0; i < 6; ++i)
        {
            int newChannel = (channel * 6) + i;
            int x = (newChannel / blocksPerCol) * blockSize;
            int y = (newChannel % blocksPerCol) * blockSize;
            if (x >= w || y >= h) continue;

            float t = (float)bits[i] / 2.0f;                       // InverseLerp(0,2,bit)
            uint8_t intensity = (uint8_t)(0.0f + (255.0f - 0.0f) * t); // Lerp(0,255,t)
            MakeColorBlock(px, w, h, x, y, intensity, intensity, intensity,
                           alpha, blockSize);
        }
    }
private:
    ShowConfig cfg;
};

// ===========================================================================
// Spiral - walks a spiral over 8x8 blocks.
// ===========================================================================
class SerializerSpiral : public ISerializer
{
public:
    SerializerType type() const override { return SerializerType::Spiral; }
    SerializerSpiral(const ShowConfig& c) : cfg(c) {}

    void InitFrame() override
    {
        x = 0; y = 0; state = 0;
        visited.clear();
    }

    void SerializeChannel(std::vector<uint8_t>& px, int w, int h,
                          uint8_t value, int channel) override
    {
        using namespace detail;
        const int blockSize = 8;
        uint8_t alpha = GetBlockAlpha(value, cfg.autoMaskOnZero);

        int scaledWidth  = w / blockSize;
        int scaledHeight = h / blockSize;

        int xfinal = x * blockSize;
        int yfinal = y * blockSize;

        MakeColorBlock(px, w, h, xfinal, yfinal, value, value, value, alpha, blockSize);

        int nextX = x, nextY = y;
        CalculateNextMove(nextX, nextY);

        bool hit = Visited(nextX, nextY) || nextX < 0 || nextY < 0 ||
                   nextX >= scaledWidth || nextY >= scaledHeight;
        if (hit)
        {
            state++; if (state > 3) state = 0;
            nextX = x; nextY = y;
            CalculateNextMove(nextX, nextY);
        }

        visited.push_back({x, y});
        x = nextX;
        y = nextY;
    }
private:
    ShowConfig cfg;
    int x = 0, y = 0, state = 0;
    std::vector<std::pair<int,int>> visited;

    void CalculateNextMove(int& nx, int& ny) const
    {
        nx = x; ny = y;
        switch (state)
        {
            case 0: nx = x + 1; break;
            case 1: ny = y + 1; break;
            case 2: nx = x - 1; break;
            default: ny = y - 1; break;
        }
    }
    bool Visited(int nx, int ny) const
    {
        for (const auto& p : visited)
            if (p.first == nx && p.second == ny) return true;
        return false;
    }
};

// ===========================================================================
// MDMX / BinaryStageFlight - 4px blocks, 48 per column, 8 bits, +CRC4 column.
// ===========================================================================
class SerializerMDMX : public ISerializer
{
public:
    SerializerType type() const override { return SerializerType::MDMX; }
    SerializerMDMX(const ShowConfig& c) : cfg(c) {}

    void SerializeChannel(std::vector<uint8_t>& px, int w, int h,
                          uint8_t value, int channel) override
    {
        using namespace detail;
        const int blockSize = 4;
        uint8_t alpha = GetBlockAlpha(value, cfg.autoMaskOnZero);

        for (int i = 0; i < 8; ++i)
        {
            int x, y;
            GetPositionData(channel, i, w, x, y);
            uint8_t bit = ((value >> i) & 1) ? 255 : 0;
            MakeColorBlock(px, w, h, x, y, bit, bit, bit, alpha, blockSize);
        }
    }

    void CompleteFrame(std::vector<uint8_t>& px, int w, int h,
                       const std::vector<uint8_t>& channels) override
    {
        using namespace detail;
        const int blockSize = 4;
        const int channelsPerCol = 6;
        const int blocksPerCol = channelsPerCol * 8;   // 48
        const int CRCBits = 4;
        uint8_t alpha = GetBlockAlpha(255, cfg.autoMaskOnZero);

        int startY = blocksPerCol * blockSize;         // 192

        for (int i = 0; i < (int)channels.size(); i += channelsPerCol)
        {
            uint8_t values[6];
            for (int k = 0; k < channelsPerCol; ++k)
                values[k] = (i + k < (int)channels.size()) ? channels[i + k] : 0;

            uint8_t crc = Crc4(values, channelsPerCol);

            int x = (i / channelsPerCol) * blockSize;
            for (int j = 0; j < CRCBits; ++j)
            {
                int y = startY + j * blockSize;
                int xd = x, yd = y;
                CalculateWrapping(x, y, w, xd, yd);
                uint8_t bit = ((crc >> (7 - j)) & 1) ? 255 : 0;
                MakeColorBlock(px, w, h, xd, yd, bit, bit, bit, alpha, blockSize);
            }
        }
    }

private:
    ShowConfig cfg;

    void GetPositionData(int channel, int i, int w, int& x, int& y) const
    {
        const int blocksPerCol = 48;
        const int blockSize = 4;
        int newChannel = (channel * 8) + (7 - i);
        x = (newChannel / blocksPerCol) * blockSize;
        y = (newChannel % blocksPerCol) * blockSize;
        CalculateWrapping(x, y, w, x, y);
    }

    static void CalculateWrapping(int x, int y, int texW, int& adjx, int& adjy)
    {
        const int blocksPerCol = 48;
        const int CRCBits = 4;
        const int blockSize = 4;
        int wrap = x / texW;
        adjx = x % texW;
        adjy = y + (wrap * (blocksPerCol + CRCBits) * blockSize);
    }

    // HNode Crc4: polynomial 0x03, nibble accumulator, result shifted left 4.
    static uint8_t Crc4(const uint8_t* data, int len)
    {
        uint32_t crc = 0;
        const uint32_t polynomial = 0x03;
        for (int n = 0; n < len; ++n)
        {
            uint32_t v = data[n];
            for (int bit = 7; bit >= 0; --bit)
            {
                uint32_t inBit = (v >> bit) & 1u;
                bool top = (crc & 0x8u) != 0u;
                crc = ((crc << 1) | inBit) & 0xFu;
                if (top) crc ^= polynomial;
            }
        }
        return (uint8_t)(crc << CRCBits);
    }
    static const int CRCBits = 4;
};

inline ISerializer* CreateSerializer(SerializerType t, const ShowConfig& cfg)
{
    switch (t)
    {
        case SerializerType::VRSL:        return new SerializerVRSL(cfg);
        case SerializerType::Binary:      return new SerializerBinary(cfg);
        case SerializerType::ColorBinary: return new SerializerColorBinary(cfg);
        case SerializerType::Ternary:     return new SerializerTernary(cfg);
        case SerializerType::Spiral:      return new SerializerSpiral(cfg);
        case SerializerType::MDMX:        return new SerializerMDMX(cfg);
        default:                          return new SerializerVRSL(cfg);
    }
}

} // namespace HNodeVST
