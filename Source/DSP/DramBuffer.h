#pragma once

#include "Spx90Types.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace aero {

// Models the Yamaha SPX90 / SPX90II DRAM circular memory buffer at 31.25 kHz
class DramBuffer {
public:
    static constexpr size_t kMaxDramWords = 65536; // 2.097 seconds at 31.25 kHz (SPX90II)
    static constexpr size_t kMask = kMaxDramWords - 1;

    DramBuffer() {
        bufferL_.resize(kMaxDramWords, 0.0f);
        bufferR_.resize(kMaxDramWords, 0.0f);
    }

    void clear() {
        std::fill(bufferL_.begin(), bufferL_.end(), 0.0f);
        std::fill(bufferR_.begin(), bufferR_.end(), 0.0f);
        writePos_ = 0;
    }

    void setConfig(BitDepthMode bitMode, RamModel ramModel) {
        bitMode_ = bitMode;
        ramModel_ = ramModel;
    }

    inline float quantizeDramWord(float x) const {
        x = std::clamp(x, -1.0f, 0.999969f);
        if (bitMode_ == BitDepthMode::Linear12Bit) {
            return std::floor(x * 2048.0f + 0.5f) / 2048.0f;
        }
        if (bitMode_ == BitDepthMode::Companded12Bit) {
            float sign = (x < 0.0f) ? -1.0f : 1.0f;
            float q = std::floor(std::sqrt(std::abs(x)) * 1024.0f + 0.5f) / 1024.0f;
            return sign * (q * q);
        }
        return std::floor(x * 32768.0f + 0.5f) / 32768.0f;
    }

    inline void writeStereo(float l, float r) {
        bufferL_[writePos_] = quantizeDramWord(l);
        bufferR_[writePos_] = quantizeDramWord(r);
    }

    inline void advanceWriteHead() {
        writePos_ = (writePos_ + 1) & kMask;
    }

    // YM3804 + YM3807 modulated read:
    // Uses integer address offset + 8-bit quantized fractional gain interpolation between adjacent DRAM words
    inline float readL(float delaySamples) const {
        float maxSamples = (ramModel_ == RamModel::Spx90_MkII) ? 62500.0f : 15625.0f;
        delaySamples = std::clamp(delaySamples, 1.0f, maxSamples);
        int intDelay = static_cast<int>(delaySamples);
        float frac = delaySamples - static_cast<float>(intDelay);
        // Quantize fractional interpolation coefficient to 8-bit (256 steps) like YM3807 gain bus
        float qFrac = std::floor(frac * 256.0f) / 256.0f;

        size_t idx0 = (writePos_ - static_cast<size_t>(intDelay)) & kMask;
        size_t idx1 = (idx0 - 1) & kMask;
        return bufferL_[idx0] * (1.0f - qFrac) + bufferL_[idx1] * qFrac;
    }

    inline float readR(float delaySamples) const {
        float maxSamples = (ramModel_ == RamModel::Spx90_MkII) ? 62500.0f : 15625.0f;
        delaySamples = std::clamp(delaySamples, 1.0f, maxSamples);
        int intDelay = static_cast<int>(delaySamples);
        float frac = delaySamples - static_cast<float>(intDelay);
        float qFrac = std::floor(frac * 256.0f) / 256.0f;

        size_t idx0 = (writePos_ - static_cast<size_t>(intDelay)) & kMask;
        size_t idx1 = (idx0 - 1) & kMask;
        return bufferR_[idx0] * (1.0f - qFrac) + bufferR_[idx1] * qFrac;
    }

    static inline float msToSamples(float ms, float sr = 31250.0f) {
        return std::max(1.0f, ms * 0.001f * sr);
    }

private:
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    size_t writePos_ = 0;
    BitDepthMode bitMode_ = BitDepthMode::Linear16Bit;
    RamModel ramModel_ = RamModel::Spx90_MkI;
};

} // namespace aero
