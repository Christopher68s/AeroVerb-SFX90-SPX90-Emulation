#pragma once

#include "Spx90Types.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace aero {

// Second-order biquad filter for steep anti-aliasing / reconstruction cascades & EQ
// Includes parameter caching so per-sample setter calls skip trig/pow when unchanged.
struct BiquadFilter {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1L = 0.0f, z2L = 0.0f;
    float z1R = 0.0f, z2R = 0.0f;

    int cachedType = -1;
    double cachedSr = 0.0;
    double cachedFreq = -1.0;
    double cachedQ = -1.0;
    double cachedGain = -999.0;

    void reset() {
        z1L = z2L = z1R = z2R = 0.0f;
    }

    inline bool checkAndUpdateCache(int type, double sr, double freq, double q, double gain) {
        if (cachedType == type &&
            std::abs(cachedSr - sr) < 1e-3 &&
            std::abs(cachedFreq - freq) < 1e-3 &&
            std::abs(cachedQ - q) < 1e-4 &&
            std::abs(cachedGain - gain) < 1e-3) {
            return true; // Already up to date
        }
        cachedType = type;
        cachedSr = sr;
        cachedFreq = freq;
        cachedQ = q;
        cachedGain = gain;
        return false;
    }

    void setLowPass(double sampleRate, double cutoffHz, double Q) {
        cutoffHz = std::clamp(cutoffHz, 20.0, sampleRate * 0.47);
        if (checkAndUpdateCache(0, sampleRate, cutoffHz, Q, 0.0)) return;
        const double w0 = 2.0 * 3.14159265358979323846 * cutoffHz / sampleRate;
        const double cosW0 = std::cos(w0);
        const double sinW0 = std::sin(w0);
        const double alpha = sinW0 / (2.0 * Q);
        const double a0 = 1.0 + alpha;
        const double invA0 = 1.0 / a0;
        b0 = static_cast<float>((1.0 - cosW0) * 0.5 * invA0);
        b1 = static_cast<float>((1.0 - cosW0) * invA0);
        b2 = b0;
        a1 = static_cast<float>((-2.0 * cosW0) * invA0);
        a2 = static_cast<float>((1.0 - alpha) * invA0);
    }

    void setHighPass(double sampleRate, double cutoffHz, double Q) {
        cutoffHz = std::clamp(cutoffHz, 5.0, sampleRate * 0.45);
        if (checkAndUpdateCache(1, sampleRate, cutoffHz, Q, 0.0)) return;
        const double w0 = 2.0 * 3.14159265358979323846 * cutoffHz / sampleRate;
        const double cosW0 = std::cos(w0);
        const double sinW0 = std::sin(w0);
        const double alpha = sinW0 / (2.0 * Q);
        const double a0 = 1.0 + alpha;
        const double invA0 = 1.0 / a0;
        b0 = static_cast<float>((1.0 + cosW0) * 0.5 * invA0);
        b1 = static_cast<float>(-(1.0 + cosW0) * invA0);
        b2 = b0;
        a1 = static_cast<float>((-2.0 * cosW0) * invA0);
        a2 = static_cast<float>((1.0 - alpha) * invA0);
    }

    void setPeakingEq(double sampleRate, double freqHz, double Q, double gainDb) {
        freqHz = std::clamp(freqHz, 20.0, sampleRate * 0.45);
        if (checkAndUpdateCache(2, sampleRate, freqHz, Q, gainDb)) return;
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
        const double cosW0 = std::cos(w0);
        const double sinW0 = std::sin(w0);
        const double alpha = sinW0 / (2.0 * std::max(0.1, Q));
        const double a0 = 1.0 + alpha / A;
        const double invA0 = 1.0 / a0;
        b0 = static_cast<float>((1.0 + alpha * A) * invA0);
        b1 = static_cast<float>((-2.0 * cosW0) * invA0);
        b2 = static_cast<float>((1.0 - alpha * A) * invA0);
        a1 = b1;
        a2 = static_cast<float>((1.0 - alpha / A) * invA0);
    }

    void setLowShelf(double sampleRate, double freqHz, double gainDb) {
        freqHz = std::clamp(freqHz, 20.0, sampleRate * 0.45);
        if (checkAndUpdateCache(3, sampleRate, freqHz, 0.707, gainDb)) return;
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
        const double cosW0 = std::cos(w0);
        const double sinW0 = std::sin(w0);
        const double sqrtA2Alpha = 2.0 * std::sqrt(A) * (sinW0 / 2.0);
        const double a0 = (A + 1.0) + (A - 1.0) * cosW0 + sqrtA2Alpha;
        const double invA0 = 1.0 / a0;
        b0 = static_cast<float>((A * ((A + 1.0) - (A - 1.0) * cosW0 + sqrtA2Alpha)) * invA0);
        b1 = static_cast<float>((2.0 * A * ((A - 1.0) - (A + 1.0) * cosW0)) * invA0);
        b2 = static_cast<float>((A * ((A + 1.0) - (A - 1.0) * cosW0 - sqrtA2Alpha)) * invA0);
        a1 = static_cast<float>((-2.0 * ((A - 1.0) + (A + 1.0) * cosW0)) * invA0);
        a2 = static_cast<float>(((A + 1.0) + (A - 1.0) * cosW0 - sqrtA2Alpha) * invA0);
    }

    void setHighShelf(double sampleRate, double freqHz, double gainDb) {
        freqHz = std::clamp(freqHz, 20.0, sampleRate * 0.45);
        if (checkAndUpdateCache(4, sampleRate, freqHz, 0.707, gainDb)) return;
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
        const double cosW0 = std::cos(w0);
        const double sinW0 = std::sin(w0);
        const double sqrtA2Alpha = 2.0 * std::sqrt(A) * (sinW0 / 2.0);
        const double a0 = (A + 1.0) - (A - 1.0) * cosW0 + sqrtA2Alpha;
        const double invA0 = 1.0 / a0;
        b0 = static_cast<float>((A * ((A + 1.0) + (A - 1.0) * cosW0 + sqrtA2Alpha)) * invA0);
        b1 = static_cast<float>((-2.0 * A * ((A - 1.0) + (A + 1.0) * cosW0)) * invA0);
        b2 = static_cast<float>((A * ((A + 1.0) + (A - 1.0) * cosW0 - sqrtA2Alpha)) * invA0);
        a1 = static_cast<float>((2.0 * ((A - 1.0) - (A + 1.0) * cosW0)) * invA0);
        a2 = static_cast<float>(((A + 1.0) - (A - 1.0) * cosW0 - sqrtA2Alpha) * invA0);
    }

    inline float processL(float x) {
        float y = b0 * x + z1L;
        z1L = b1 * x - a1 * y + z2L;
        z2L = b2 * x - a2 * y;
        return y;
    }

    inline float processR(float x) {
        float y = b0 * x + z1R;
        z1R = b1 * x - a1 * y + z2R;
        z2R = b2 * x - a2 * y;
        return y;
    }

    inline void processStereo(float& l, float& r) {
        l = processL(l);
        r = processR(r);
    }
};

class ConverterEngine {
public:
    static constexpr double kDefaultInternalClockHz = 31250.0;
    static constexpr double kDefaultAntiAliasCutoffHz = 12000.0;

    void prepare(double hostRate) {
        hostSampleRate_ = std::max(8000.0, hostRate);
        updateFilters();
        reset();
    }

    void reset() {
        for (auto& f : aaFilters_) f.reset();
        for (auto& f : recFilters_) f.reset();
        dcBlockerIn_.reset();
        dcBlockerOut_.reset();
        phaseAccumulator_ = 0.0;
        prevDspOutL_ = currDspOutL_ = 0.0f;
        prevDspOutR_ = currDspOutR_ = 0.0f;
        overflowFlag_ = false;
    }

    void setParameters(BitDepthMode bitMode,
                       float clockHz,
                       float converterDriveDb,
                       float noiseFloorDb,
                       bool hiFiBypass) {
        bitMode_ = bitMode;
        double clampedClock = std::clamp(static_cast<double>(clockHz), 15000.0, 48000.0);
        if (std::abs(clampedClock - internalClockHz_) > 1.0) {
            internalClockHz_ = clampedClock;
            updateFilters();
        }
        converterDriveLinear_ = std::pow(10.0f, std::clamp(converterDriveDb, -12.0f, 18.0f) / 20.0f);
        noiseFloorLinear_ = (noiseFloorDb <= -115.0f) ? 0.0f : std::pow(10.0f, std::clamp(noiseFloorDb, -120.0f, -40.0f) / 20.0f);
        hiFiBypass_ = hiFiBypass;
    }

    // A/D stage: analog input drive + soft saturation, then converter noise and quantisation.
    // Only this stage applies Converter Drive and raises the overflow flag.
    inline float adcStage(float x) {
        if (hiFiBypass_) return x;

        float driven = x * converterDriveLinear_;
        if (std::abs(driven) > 0.95f) {
            overflowFlag_ = true;
        }
        // Soft saturation x / (1 + k x^2) peaks at |x| = 1/sqrt(k) and folds back beyond it,
        // so hold the input at that peak to keep the curve monotonic (it is hard-clamped below).
        constexpr float kSatK = 0.22f;
        constexpr float kSatPeakIn = 2.13200716f; // 1 / sqrt(kSatK)
        const float mag = std::min(std::abs(driven), kSatPeakIn);
        const float held = std::copysign(mag, driven);
        float sat = held / (1.0f + kSatK * held * held);
        sat = std::clamp(sat, -1.0f, 0.999969f);

        return quantizeWord(sat);
    }

    // D/A stage: quantise only. Drive and saturation already happened at the A/D stage.
    inline float dacStage(float x) {
        if (hiFiBypass_) return x;
        return quantizeWord(std::clamp(x, -1.0f, 0.999969f));
    }

    // Add converter thermal noise / TPDF dither, then quantise to 12-bit Linear, 12-bit Companded or 16-bit PCM
    inline float quantizeWord(float sat) {
        float dither = nextWhiteNoise() * noiseFloorLinear_;
        float s = std::clamp(sat + dither, -1.0f, 0.999969f);

        switch (bitMode_) {
            case BitDepthMode::Linear12Bit: {
                // 12-bit signed (-2048 .. +2047)
                constexpr float kScale12 = 2048.0f;
                return std::floor(s * kScale12 + 0.5f) / kScale12;
            }
            case BitDepthMode::Companded12Bit: {
                // Non-linear 12-bit floating/chord converter emulation:
                // Fine steps near zero, coarser steps on loud peaks (classic mid-80s DAC grit)
                float sign = (s < 0.0f) ? -1.0f : 1.0f;
                float mag = std::abs(s);
                float comp = std::sqrt(mag); // mu-law / chord curve approximation
                constexpr float kScale10 = 1024.0f;
                float q = std::floor(comp * kScale10 + 0.5f) / kScale10;
                return sign * (q * q);
            }
            case BitDepthMode::Linear16Bit:
            default: {
                // 16-bit signed (-32768 .. +32767)
                constexpr float kScale16 = 32768.0f;
                return std::floor(s * kScale16 + 0.5f) / kScale16;
            }
        }
    }

    // Process one host sample frame; invokes dspTickCallback(float& l, float& r) at 31.25 kHz
    template <typename DspTickFn>
    inline void processSampleFrame(float& l, float& r, DspTickFn&& dspTick) {
        // 20 Hz input DC / subsonic high-pass filter
        dcBlockerIn_.processStereo(l, r);

        if (hiFiBypass_) {
            // In Hi-Fi bypass mode, still run DSP at internal clock ratio for authentic delay times,
            // or tick directly scaled
            const double step = internalClockHz_ / hostSampleRate_;
            phaseAccumulator_ += step;
            while (phaseAccumulator_ >= 1.0) {
                phaseAccumulator_ -= 1.0;
                prevDspOutL_ = currDspOutL_;
                prevDspOutR_ = currDspOutR_;
                float dl = l;
                float dr = r;
                dspTick(dl, dr);
                currDspOutL_ = dl;
                currDspOutR_ = dr;
            }
            float frac = static_cast<float>(phaseAccumulator_);
            l = prevDspOutL_ + frac * (currDspOutL_ - prevDspOutL_);
            r = prevDspOutR_ + frac * (currDspOutR_ - prevDspOutR_);
            return;
        }

        // 1. Steep 6th-order 12 kHz Anti-Aliasing filter (3 cascaded biquads with Chebyshev-like Q)
        for (auto& f : aaFilters_) {
            f.processStereo(l, r);
        }

        // 2. Sample-and-Hold Downsampler to 31.25 kHz internal clock
        const double step = internalClockHz_ / hostSampleRate_;
        phaseAccumulator_ += step;
        while (phaseAccumulator_ >= 1.0) {
            phaseAccumulator_ -= 1.0;
            prevDspOutL_ = currDspOutL_;
            prevDspOutR_ = currDspOutR_;

            // A/D Quantization
            float ql = adcStage(l);
            float qr = adcStage(r);

            // Execute YM3804 / YM3807 DSP tick at 31.25 kHz
            dspTick(ql, qr);

            // D/A Quantization
            currDspOutL_ = dacStage(ql);
            currDspOutR_ = dacStage(qr);
        }

        // 3. DAC Sample-and-Hold + linear reconstruction blend
        float frac = static_cast<float>(phaseAccumulator_);
        // Blend 65% zero-order hold staircase + 35% linear for authentic DAC S&H image energy before reconstruction filter
        float zohL = currDspOutL_;
        float zohR = currDspOutR_;
        float linL = prevDspOutL_ + frac * (currDspOutL_ - prevDspOutL_);
        float linR = prevDspOutR_ + frac * (currDspOutR_ - prevDspOutR_);
        l = 0.65f * zohL + 0.35f * linL;
        r = 0.65f * zohR + 0.35f * linR;

        // 4. Steep 6th-order 12 kHz Reconstruction Low-Pass Filter + 20 Hz Output High-Pass
        for (auto& f : recFilters_) {
            f.processStereo(l, r);
        }
        dcBlockerOut_.processStereo(l, r);
    }

    bool consumeOverflowFlag() {
        bool v = overflowFlag_;
        overflowFlag_ = false;
        return v;
    }

    double getInternalClockHz() const { return internalClockHz_; }

private:
    void updateFilters() {
        // Cutoff scales proportionally if user underclocks/overclocks the 31.25 kHz clock
        const double cutoff = std::min(kDefaultAntiAliasCutoffHz * (internalClockHz_ / kDefaultInternalClockHz),
                                       hostSampleRate_ * 0.45);
        // 6th-order Butterworth/Chebyshev-style Q values for steep 36 dB/oct rolloff at 12 kHz
        constexpr std::array<double, 3> kQs = {0.54, 0.80, 2.15};
        for (size_t i = 0; i < 3; ++i) {
            aaFilters_[i].setLowPass(hostSampleRate_, cutoff, kQs[i]);
            recFilters_[i].setLowPass(hostSampleRate_, cutoff, kQs[i]);
        }
        dcBlockerIn_.setHighPass(hostSampleRate_, 20.0, 0.707);
        dcBlockerOut_.setHighPass(hostSampleRate_, 20.0, 0.707);
    }

    inline float nextWhiteNoise() {
        rngState_ = rngState_ * 1664525u + 1013904223u;
        float u1 = static_cast<float>(rngState_ & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
        rngState_ = rngState_ * 1664525u + 1013904223u;
        float u2 = static_cast<float>(rngState_ & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
        return (u1 - u2); // Triangular PDF dither/hiss in [-1, +1]
    }

    double hostSampleRate_ = 48000.0;
    double internalClockHz_ = kDefaultInternalClockHz;
    double phaseAccumulator_ = 0.0;

    BitDepthMode bitMode_ = BitDepthMode::Linear16Bit;
    float converterDriveLinear_ = 1.0f;
    float noiseFloorLinear_ = 0.0001f; // ~ -80 dB default analog + converter noise floor
    bool hiFiBypass_ = false;
    bool overflowFlag_ = false;

    float prevDspOutL_ = 0.0f, currDspOutL_ = 0.0f;
    float prevDspOutR_ = 0.0f, currDspOutR_ = 0.0f;

    std::array<BiquadFilter, 3> aaFilters_{};
    std::array<BiquadFilter, 3> recFilters_{};
    BiquadFilter dcBlockerIn_{};
    BiquadFilter dcBlockerOut_{};
    uint32_t rngState_ = 0x19855090u;
};

} // namespace aero
