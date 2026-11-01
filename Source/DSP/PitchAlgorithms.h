#pragma once

#include "Spx90Types.h"
#include "DramBuffer.h"
#include "ConverterEngine.h"
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>

namespace aero {

// Single vintage YM3804 + YM3807 dual-pointer circular splice pitch shifter voice
struct VintagePitchVoice {
    // 1024-sample (~32.7 ms at 31.25 kHz) splice window gives the authentic SPX90 latency & splice character
    static constexpr float kSpliceWindowSamples = 1024.0f;
    float phasor = 0.0f;

    void reset(float initialPhase = 0.0f) {
        phasor = initialPhase;
    }

    inline float processFromDram(const DramBuffer& dram,
                                 bool readRightChannel,
                                 float semi,
                                 float fineCents,
                                 float preDelayMs,
                                 float sr = 31250.0f) {
        const float totalSemis = semi + fineCents * 0.01f;
        const float pitchRatio = std::pow(2.0f, totalSemis / 12.0f);
        // Rate at which delay time changes per sample: d(delay)/dt = 1.0 - pitchRatio
        const float deltaDelay = 1.0f - pitchRatio;
        const float phaseInc = deltaDelay / kSpliceWindowSamples;

        phasor += phaseInc;
        phasor -= std::floor(phasor); // Wrap to [0.0, 1.0)

        float phA = phasor;
        float phB = phasor + 0.5f;
        if (phB >= 1.0f) phB -= 1.0f;

        // Quantize window crossfade gain to 7-bit steps like YM3807 envelope/gain generator
        // Trapezoidal/triangular splice window (creates the classic SPX90 splice artifact and frequency coloring)
        float winA = 1.0f - std::abs(2.0f * phA - 1.0f);
        float winB = 1.0f - std::abs(2.0f * phB - 1.0f);
        winA = std::floor(winA * 128.0f) / 128.0f;
        winB = std::floor(winB * 128.0f) / 128.0f;

        // Baseline latency (~14 ms minimum + splice window + user pre-delay)
        float baseDelaySamples = DramBuffer::msToSamples(std::max(0.1f, preDelayMs) + 12.5f, sr);
        float dlyA = baseDelaySamples + phA * kSpliceWindowSamples;
        float dlyB = baseDelaySamples + phB * kSpliceWindowSamples;

        float sA = readRightChannel ? dram.readR(dlyA) : dram.readL(dlyA);
        float sB = readRightChannel ? dram.readR(dlyB) : dram.readL(dlyB);

        return sA * winA + sB * winB;
    }
};

class PitchChangeEngine {
public:
    void reset() {
        dram_.clear();
        voice1_.reset(0.0f);
        voice2_.reset(0.25f);
        fbL_ = fbR_ = 0.0f;
        toneFilter_.reset();
    }

    void setConfig(BitDepthMode bitMode, RamModel ramModel) {
        dram_.setConfig(bitMode, ramModel);
        ramModel_ = ramModel;
    }

    void setMidiNote(int midiNote, int velocity) {
        if (velocity > 0) {
            activeMidiNote_.store(midiNote, std::memory_order_relaxed);
        }
    }

    void processTick(AlgorithmType alg,
                     const std::array<float, kMaxProgramParams>& p,
                     float& l,
                     float& r,
                     float sampleRate = 31250.0f) {
        switch (alg) {
            case AlgorithmType::PitchChangeA:
                processPitchA(p, l, r, sampleRate);
                break;
            case AlgorithmType::PitchChangeB:
                processPitchB(p, l, r, sampleRate);
                break;
            case AlgorithmType::PitchChangeC:
                processPitchC(p, l, r, sampleRate);
                break;
            case AlgorithmType::PitchChangeD:
                processPitchD(p, l, r, sampleRate);
                break;
            default:
                break;
        }
    }

private:
    // #21 PITCH CHANGE A (1-Voice + Feedback + MIDI Base Key tracking)
    void processPitchA(const std::array<float, kMaxProgramParams>& p,
                       float& l, float& r, float sr) {
        float semi    = std::clamp(p[0], -12.0f, 12.0f);
        float fine    = std::clamp(p[1], -100.0f, 100.0f);
        float dlyMs   = std::clamp(p[2], 0.1f, 400.0f);
        float fbGain  = std::clamp(p[3], -99.0f, 99.0f) * 0.009f;
        int baseKey   = static_cast<int>(std::round(p[4]));

        int midiNote = activeMidiNote_.load(std::memory_order_relaxed);
        if (baseKey >= 36 && midiNote >= 0) {
            semi = std::clamp(static_cast<float>(midiNote - baseKey), -24.0f, 24.0f);
        }

        float monoIn = 0.5f * (l + r);
        dram_.writeStereo(std::clamp(monoIn + fbL_ * fbGain, -1.5f, 1.5f), 0.0f);

        float shifted = voice1_.processFromDram(dram_, false, semi, fine, dlyMs, sr);
        dram_.advanceWriteHead();

        // Tap feedback before the tone filter: its +1.8 dB peak inside the loop pushed loop gain
        // above 1 (self-oscillation near 90%+ feedback). The window sum is <= 1, so |fbGain| < 1 is stable.
        fbL_ = shifted;

        // Subtle mid-band emphasis characteristic of SPX90 pitch stage
        toneFilter_.setPeakingEq(sr, 1800.0, 0.8, 1.8);
        shifted = toneFilter_.processL(shifted);

        l = shifted;
        r = shifted;
    }

    // #22 PITCH CHANGE B (Mono Dual-Voice Algorithm: both voices summed to mono out)
    void processPitchB(const std::array<float, kMaxProgramParams>& p,
                       float& l, float& r, float sr) {
        const float semi1 = std::clamp(p[0], -12.0f, 12.0f);
        const float fine1 = std::clamp(p[1], -100.0f, 100.0f);
        const float dly1  = std::clamp(p[2], 0.1f, 400.0f);
        const float semi2 = std::clamp(p[3], -12.0f, 12.0f);
        const float fine2 = std::clamp(p[4], -100.0f, 100.0f);
        const float dly2  = std::clamp(p[5], 0.1f, 400.0f);

        float monoIn = 0.5f * (l + r);
        dram_.writeStereo(monoIn, monoIn);

        float v1 = voice1_.processFromDram(dram_, false, semi1, fine1, dly1, sr);
        float v2 = voice2_.processFromDram(dram_, false, semi2, fine2, dly2, sr);
        dram_.advanceWriteHead();

        // Pitch B is strictly a mono dual-voice algorithm
        float monoOut = 0.65f * (v1 + v2);
        toneFilter_.setPeakingEq(sr, 1650.0, 0.75, 1.6);
        monoOut = toneFilter_.processL(monoOut);

        l = monoOut;
        r = monoOut;
    }

    // #23 PITCH CHANGE C (Stereo Dual-Voice Algorithm: Voice L -> Left, Voice R -> Right, classic +8/-8 detune)
    void processPitchC(const std::array<float, kMaxProgramParams>& p,
                       float& l, float& r, float sr) {
        const float semiL = std::clamp(p[0], -12.0f, 12.0f);
        const float fineL = std::clamp(p[1], -100.0f, 100.0f);
        const float dlyL  = std::clamp(p[2], 0.1f, 400.0f);
        const float semiR = std::clamp(p[3], -12.0f, 12.0f);
        const float fineR = std::clamp(p[4], -100.0f, 100.0f);
        const float dlyR  = std::clamp(p[5], 0.1f, 400.0f);

        dram_.writeStereo(l, r);

        float outL = voice1_.processFromDram(dram_, false, semiL, fineL, dlyL, sr);
        float outR = voice2_.processFromDram(dram_, true,  semiR, fineR, dlyR, sr);
        dram_.advanceWriteHead();

        toneFilter_.setPeakingEq(sr, 1750.0, 0.8, 1.5);
        toneFilter_.processStereo(outL, outR);

        l = outL;
        r = outR;
    }

    // #24 PITCH CHANGE D (Stereo Dual-Voice + Independent Feedback + MIDI Base Key)
    void processPitchD(const std::array<float, kMaxProgramParams>& p,
                       float& l, float& r, float sr) {
        float semiL   = std::clamp(p[0], -12.0f, 12.0f);
        float fineL   = std::clamp(p[1], -100.0f, 100.0f);
        float fbGainL = std::clamp(p[2], -99.0f, 99.0f) * 0.009f;
        float semiR   = std::clamp(p[3], -12.0f, 12.0f);
        float fineR   = std::clamp(p[4], -100.0f, 100.0f);
        float fbGainR = std::clamp(p[5], -99.0f, 99.0f) * 0.009f;
        int baseKey   = static_cast<int>(std::round(p[6]));

        int midiNote = activeMidiNote_.load(std::memory_order_relaxed);
        if (baseKey >= 36 && midiNote >= 0) {
            float offset = static_cast<float>(midiNote - baseKey);
            semiL = std::clamp(semiL + offset, -24.0f, 24.0f);
            semiR = std::clamp(semiR + offset, -24.0f, 24.0f);
        }

        dram_.writeStereo(std::clamp(l + fbL_ * fbGainL, -1.5f, 1.5f),
                          std::clamp(r + fbR_ * fbGainR, -1.5f, 1.5f));

        float outL = voice1_.processFromDram(dram_, false, semiL, fineL, 5.0f, sr);
        float outR = voice2_.processFromDram(dram_, true,  semiR, fineR, 5.0f, sr);
        dram_.advanceWriteHead();

        fbL_ = outL;
        fbR_ = outR;
        l = outL;
        r = outR;
    }

    DramBuffer dram_;
    VintagePitchVoice voice1_{};
    VintagePitchVoice voice2_{};
    BiquadFilter toneFilter_{};
    float fbL_ = 0.0f, fbR_ = 0.0f;
    std::atomic<int> activeMidiNote_{60};
    RamModel ramModel_ = RamModel::Spx90_MkI;
};

} // namespace aero
