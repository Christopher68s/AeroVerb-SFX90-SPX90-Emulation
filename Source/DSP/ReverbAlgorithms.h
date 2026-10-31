#pragma once

#include "Spx90Types.h"
#include "DramBuffer.h"
#include "Ym3807LfoBank.h"
#include "ConverterEngine.h"
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace aero {

class ReverbEngine {
public:
    ReverbEngine() {
        preDelayBuf_.resize(16384, 0.0f);
        for (size_t i = 0; i < kNumCombs; ++i) {
            combBufL_[i].resize(4096, 0.0f);
            combBufR_[i].resize(4096, 0.0f);
        }
        for (size_t i = 0; i < kNumAllPass; ++i) {
            apBufL_[i].resize(1024, 0.0f);
            apBufR_[i].resize(1024, 0.0f);
        }
        for (size_t i = 0; i < 2; ++i) {
            erDiffBufL_[i].resize(512, 0.0f);
            erDiffBufR_[i].resize(512, 0.0f);
        }
    }

    void reset() {
        std::fill(preDelayBuf_.begin(), preDelayBuf_.end(), 0.0f);
        preDelayWrite_ = 0;
        for (size_t i = 0; i < kNumCombs; ++i) {
            std::fill(combBufL_[i].begin(), combBufL_[i].end(), 0.0f);
            std::fill(combBufR_[i].begin(), combBufR_[i].end(), 0.0f);
            combIdxL_[i] = combIdxR_[i] = 0;
            combFilterL_[i] = combFilterR_[i] = 0.0f;
        }
        for (size_t i = 0; i < kNumAllPass; ++i) {
            std::fill(apBufL_[i].begin(), apBufL_[i].end(), 0.0f);
            std::fill(apBufR_[i].begin(), apBufR_[i].end(), 0.0f);
            apIdxL_[i] = apIdxR_[i] = 0;
        }
        for (size_t i = 0; i < 2; ++i) {
            std::fill(erDiffBufL_[i].begin(), erDiffBufL_[i].end(), 0.0f);
            std::fill(erDiffBufR_[i].begin(), erDiffBufR_[i].end(), 0.0f);
            erDiffIdxL_[i] = erDiffIdxR_[i] = 0;
        }
        erDram_.clear();
        erFbL_ = erFbR_ = 0.0f;
        lfo_.reset();
        lfo_.setLfoConfig(0, 0.65f, 0.0f, LfoWaveform::Sine);
        lfo_.setLfoConfig(1, 0.81f, 0.5f, LfoWaveform::Sine);
        hpf_.reset();
        lpf_.reset();
        gateEnv_ = 0.0f;
        gateTimer_ = 0.0f;
        revSwellProgress_ = 0.0f;
        gateOpen_ = false;
    }

    void setConfig(BitDepthMode bitMode, RamModel ramModel) {
        bitMode_ = bitMode;
        ramModel_ = ramModel;
        erDram_.setConfig(bitMode, ramModel);
    }

    void processTick(AlgorithmType alg,
                     const std::array<float, kMaxProgramParams>& params,
                     float& l,
                     float& r,
                     float sampleRate = 31250.0f) {
        switch (alg) {
            case AlgorithmType::RevHall:
                processReverb(0, params, l, r, sampleRate);
                break;
            case AlgorithmType::RevRoom:
                processReverb(1, params, l, r, sampleRate);
                break;
            case AlgorithmType::RevVocal:
                processReverb(2, params, l, r, sampleRate);
                break;
            case AlgorithmType::RevPlate:
                processReverb(3, params, l, r, sampleRate);
                break;
            case AlgorithmType::EarlyRef1:
            case AlgorithmType::EarlyRef2:
                processEarlyReflections(params, l, r, sampleRate, false, false);
                break;
            case AlgorithmType::GateReverb:
                processEarlyReflections(params, l, r, sampleRate, true, false);
                break;
            case AlgorithmType::ReverseGate:
                processEarlyReflections(params, l, r, sampleRate, true, true);
                break;
            case AlgorithmType::ReverbAndGate:
                processReverbAndGate(params, l, r, sampleRate);
                break;
            default:
                break;
        }
    }

private:
    static constexpr size_t kNumCombs = 6;
    static constexpr size_t kNumAllPass = 4;
    static constexpr int kNumErTaps = 24;

    inline float quantizeWord(float x) const {
        x = std::clamp(x, -1.2f, 1.2f);
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

    // #1 REV 1 HALL, #2 REV 2 ROOM, #3 REV 3 VOCAL, #4 REV 4 PLATE
    void processReverb(int revType,
                       const std::array<float, kMaxProgramParams>& p,
                       float& l, float& r, float sr) {
        const float revTime  = std::clamp(p[0], 0.3f, 99.0f);
        const float highDamp = std::clamp(p[1], 0.1f, 1.0f);
        const float preDlyMs = std::clamp(p[2], 0.1f, 50.0f);
        const float hpfFreq  = std::clamp(p[3], 20.0f, 1000.0f);
        const float lpfFreq  = std::clamp(p[4], 1000.0f, 12000.0f);

        runCoreReverbTank(revType, revTime, highDamp, preDlyMs, hpfFreq, lpfFreq, l, r, sr);
    }

    // #20 REVERB & GATE
    void processReverbAndGate(const std::array<float, kMaxProgramParams>& p,
                              float& l, float& r, float sr) {
        const float revTime  = std::clamp(p[0], 0.3f, 99.0f);
        const float highDamp = std::clamp(p[1], 0.1f, 1.0f);
        const float trgLvl   = std::clamp(p[2], 1.0f, 100.0f) * 0.004f;
        const float holdMs   = std::clamp(p[3], 5.0f, 2000.0f);
        const float relMs    = std::clamp(p[4], 5.0f, 2000.0f);
        const float hpfFreq  = std::clamp(p[5], 20.0f, 1000.0f);
        const float lpfFreq  = std::clamp(p[6], 1000.0f, 12000.0f);

        float inPeak = std::max(std::abs(l), std::abs(r));
        updateGateEnvelope(inPeak, trgLvl, holdMs, relMs, sr);

        runCoreReverbTank(3, revTime, highDamp, 12.0f, hpfFreq, lpfFreq, l, r, sr);
        l *= gateEnv_;
        r *= gateEnv_;
    }

    void runCoreReverbTank(int revType, float revTime, float highDamp, float preDlyMs,
                           float hpfFreq, float lpfFreq, float& l, float& r, float sr) {
        static constexpr float kTypeScale[4] = { 1.15f, 0.72f, 0.96f, 0.84f };
        static constexpr int kBaseCombL[kNumCombs] = { 997, 1153, 1303, 1459, 1609, 1777 };
        static constexpr int kBaseCombR[kNumCombs] = { 1031, 1187, 1361, 1493, 1657, 1823 };
        static constexpr int kBaseApL[kNumAllPass] = { 113, 199, 313, 439 };
        static constexpr int kBaseApR[kNumAllPass] = { 131, 223, 337, 463 };

        const int clampedType = std::clamp(revType, 0, 3);
        const float scale = kTypeScale[clampedType];

        // Recompute comb decay gains only when revType, revTime, or sampleRate changes
        if (clampedType != cachedRevType_ ||
            std::abs(revTime - cachedRevTime_) > 1e-4f ||
            std::abs(sr - cachedCombSr_) > 1.0f) {
            cachedRevType_ = clampedType;
            cachedRevTime_ = revTime;
            cachedCombSr_  = sr;
            for (size_t i = 0; i < kNumCombs; ++i) {
                int baseLenL = std::clamp(static_cast<int>(kBaseCombL[i] * scale), 64, 4000);
                int baseLenR = std::clamp(static_cast<int>(kBaseCombR[i] * scale), 64, 4000);
                float gL = std::pow(10.0f, (-3.0f * static_cast<float>(baseLenL)) / (revTime * sr));
                float gR = std::pow(10.0f, (-3.0f * static_cast<float>(baseLenR)) / (revTime * sr));
                cachedCombGainL_[i] = std::clamp(gL, 0.1f, 0.985f);
                cachedCombGainR_[i] = std::clamp(gR, 0.1f, 0.985f);
            }
        }

        lfo_.tick(sr);
        int modOffsetL = static_cast<int>(lfo_.getOutput(0).bipolar * 3.0f);
        int modOffsetR = static_cast<int>(lfo_.getOutput(1).bipolar * 3.0f);

        float monoIn = 0.5f * (l + r);
        preDelayBuf_[preDelayWrite_] = quantizeWord(monoIn);
        int preDelaySamples = std::clamp(static_cast<int>(preDlyMs * 0.001f * sr), 1, 16000);
        size_t pdRead = (preDelayWrite_ - static_cast<size_t>(preDelaySamples)) & 16383u;
        preDelayWrite_ = (preDelayWrite_ + 1) & 16383u;

        float x = preDelayBuf_[pdRead];

        // 2-stage short input all-pass diffusion before the 6-comb tank to soften initial transient flutter
        float xL = x;
        float xR = x;
        static constexpr size_t kInDiffLenL[2] = { 43, 97 };
        static constexpr size_t kInDiffLenR[2] = { 53, 109 };
        constexpr float kInDiffGain = 0.58f;
        for (size_t s = 0; s < 2; ++s) {
            size_t iL = erDiffIdxL_[s];
            float bL = erDiffBufL_[s][iL];
            float inL = xL + bL * kInDiffGain;
            erDiffBufL_[s][iL] = quantizeWord(inL);
            xL = bL - inL * kInDiffGain;
            erDiffIdxL_[s] = (iL + 1 >= kInDiffLenL[s]) ? 0 : (iL + 1);

            size_t iR = erDiffIdxR_[s];
            float bR = erDiffBufR_[s][iR];
            float inR = xR + bR * kInDiffGain;
            erDiffBufR_[s][iR] = quantizeWord(inR);
            xR = bR - inR * kInDiffGain;
            erDiffIdxR_[s] = (iR + 1 >= kInDiffLenR[s]) ? 0 : (iR + 1);
        }

        float sumL = 0.0f;
        float sumR = 0.0f;
        const float damp = 0.15f + 0.80f * highDamp;

        for (size_t i = 0; i < kNumCombs; ++i) {
            int lenL = std::clamp(static_cast<int>(kBaseCombL[i] * scale) + ((i == 0) ? modOffsetL : 0), 64, 4000);
            int lenR = std::clamp(static_cast<int>(kBaseCombR[i] * scale) + ((i == 1) ? modOffsetR : 0), 64, 4000);

            const float gL = cachedCombGainL_[i];
            const float gR = cachedCombGainR_[i];

            size_t idxL = combIdxL_[i];
            float yL = combBufL_[i][idxL];
            combFilterL_[i] += damp * (yL - combFilterL_[i]);
            combBufL_[i][idxL] = quantizeWord(xL + combFilterL_[i] * gL);
            combIdxL_[i] = (idxL + 1 >= static_cast<size_t>(lenL)) ? 0 : (idxL + 1);
            sumL += yL;

            size_t idxR = combIdxR_[i];
            float yR = combBufR_[i][idxR];
            combFilterR_[i] += damp * (yR - combFilterR_[i]);
            combBufR_[i][idxR] = quantizeWord(xR + combFilterR_[i] * gR);
            combIdxR_[i] = (idxR + 1 >= static_cast<size_t>(lenR)) ? 0 : (idxR + 1);
            sumR += yR;
        }

        // Subtle stereo cross-coupling across comb banks
        float wetL = (0.90f * sumL + 0.10f * sumR) * 0.24f;
        float wetR = (0.90f * sumR + 0.10f * sumL) * 0.24f;

        constexpr float kApGain = 0.55f;
        for (size_t i = 0; i < kNumAllPass; ++i) {
            size_t lenL = static_cast<size_t>(std::clamp(static_cast<int>(kBaseApL[i] * scale), 16, 1000));
            size_t idxL = apIdxL_[i];
            float bufOutL = apBufL_[i][idxL];
            float inL = wetL + bufOutL * kApGain;
            apBufL_[i][idxL] = quantizeWord(inL);
            wetL = bufOutL - inL * kApGain;
            apIdxL_[i] = (idxL + 1 >= lenL) ? 0 : (idxL + 1);

            size_t lenR = static_cast<size_t>(std::clamp(static_cast<int>(kBaseApR[i] * scale), 16, 1000));
            size_t idxR = apIdxR_[i];
            float bufOutR = apBufR_[i][idxR];
            float inR = wetR + bufOutR * kApGain;
            apBufR_[i][idxR] = quantizeWord(inR);
            wetR = bufOutR - inR * kApGain;
            apIdxR_[i] = (idxR + 1 >= lenR) ? 0 : (idxR + 1);
        }

        if (hpfFreq > 25.0f) {
            hpf_.setHighPass(sr, hpfFreq, 0.707);
            hpf_.processStereo(wetL, wetR);
        }
        if (lpfFreq < 11500.0f) {
            lpf_.setLowPass(sr, lpfFreq, 0.707);
            lpf_.processStereo(wetL, wetR);
        }

        l = wetL;
        r = wetR;
    }

    // #5 EARLY REF 1, #6 EARLY REF 2, #16 GATE REVERB, #17 REVERSE GATE
    // Uses 2-stage input all-pass diffusion + dense 24-tap non-linear reflection cluster + liveness feedback
    // so gated drums and vocals sound like a thick, explosive 1985 SPX90 burst rather than a sparse flam.
    void processEarlyReflections(const std::array<float, kMaxProgramParams>& p,
                                 float& l, float& r, float sr,
                                 bool isGated, bool isReverseGate) {
        const int erType     = std::clamp(static_cast<int>(std::round(p[0])), 0, 3);
        const float roomSize = std::clamp(p[1], 0.1f, 20.0f);
        const float liveness = std::clamp(p[2], 0.0f, 10.0f) * 0.1f;
        const float preDlyMs = std::clamp(p[3], 0.1f, 400.0f);

        float holdMs = 120.0f, relMs = 90.0f, trgLvl = 0.08f;
        float hpfFreq = 20.0f, lpfFreq = 10000.0f;
        if (isGated) {
            holdMs  = std::clamp(p[4], 5.0f, 2000.0f);
            relMs   = std::clamp(p[5], 5.0f, 2000.0f);
            trgLvl  = std::clamp(p[6], 1.0f, 100.0f) * 0.0025f;
            lpfFreq = std::clamp(p[7], 1000.0f, 12000.0f);
        } else {
            hpfFreq = std::clamp(p[4], 20.0f, 1000.0f);
            lpfFreq = std::clamp(p[5], 1000.0f, 12000.0f);
        }

        // Precompute and cache the 24 reflection tap weights & polarities only when parameters change
        if (erType != cachedErType_ ||
            std::abs(liveness - cachedErLiveness_) > 1e-4f ||
            isReverseGate != cachedErRevGate_) {
            cachedErType_     = erType;
            cachedErLiveness_ = liveness;
            cachedErRevGate_  = isReverseGate;

            static constexpr float kSignsL[8] = { 1.f, -1.f,  1.f,  1.f, -1.f,  1.f, -1.f, -1.f };
            static constexpr float kSignsR[8] = { 1.f,  1.f, -1.f,  1.f, -1.f, -1.f,  1.f, -1.f };

            for (int t = 0; t < kNumErTaps; ++t) {
                float normIdx = static_cast<float>(t) / static_cast<float>(kNumErTaps - 1);
                float tapGain = 1.0f;
                if (erType == 2 || isReverseGate) {
                    tapGain = 0.06f + 0.94f * (normIdx * normIdx * (1.5f - 0.5f * normIdx));
                } else if (erType == 1) {
                    tapGain = (normIdx < 0.72f) ? (1.0f - 0.15f * normIdx) : std::exp(-6.0f * (normIdx - 0.72f));
                } else {
                    float decaySlope = 2.6f - 2.0f * liveness;
                    tapGain = std::exp(-decaySlope * normIdx);
                }
                cachedErGainL_[t] = tapGain * kSignsL[t & 7];
                cachedErGainR_[t] = tapGain * kSignsR[t & 7];
            }
        }

        float inPeak = std::max(std::abs(l), std::abs(r));
        if (isGated) {
            updateGateEnvelope(inPeak, trgLvl, holdMs, relMs, sr);
        }

        // 1. Two short input all-pass diffusers to smear sharp transients into a dense reflection burst
        float diffL = l;
        float diffR = r;
        static constexpr size_t kErDiffLenL[2] = { 43, 97 };
        static constexpr size_t kErDiffLenR[2] = { 53, 109 };
        constexpr float kErApGain = 0.62f;

        for (size_t s = 0; s < 2; ++s) {
            size_t iL = erDiffIdxL_[s];
            float bL = erDiffBufL_[s][iL];
            float inL = diffL + bL * kErApGain;
            erDiffBufL_[s][iL] = quantizeWord(inL);
            diffL = bL - inL * kErApGain;
            erDiffIdxL_[s] = (iL + 1 >= kErDiffLenL[s]) ? 0 : (iL + 1);

            size_t iR = erDiffIdxR_[s];
            float bR = erDiffBufR_[s][iR];
            float inR = diffR + bR * kErApGain;
            erDiffBufR_[s][iR] = quantizeWord(inR);
            diffR = bR - inR * kErApGain;
            erDiffIdxR_[s] = (iR + 1 >= kErDiffLenR[s]) ? 0 : (iR + 1);
        }

        // Write diffused signal + liveness cross-feedback into DRAM
        const float fbAmt = (0.18f + 0.42f * liveness);
        erDram_.writeStereo(std::clamp(diffL + erFbR_ * fbAmt, -1.5f, 1.5f),
                            std::clamp(diffR + erFbL_ * fbAmt, -1.5f, 1.5f));

        // 24 non-linearly spaced prime reflection tap offsets in milliseconds (at Room Size = 2.0)
        static constexpr float kClusterMsL[kNumErTaps] = {
            1.7f,  3.1f,  4.8f,  6.9f,  9.3f,  11.8f, 14.6f, 17.9f,
            21.4f, 25.3f, 29.7f, 34.2f, 39.5f, 45.1f, 51.3f, 57.8f,
            64.9f, 72.4f, 80.6f, 89.3f, 98.7f, 108.5f,119.2f,130.8f
        };
        static constexpr float kClusterMsR[kNumErTaps] = {
            2.1f,  3.7f,  5.4f,  7.6f,  10.1f, 12.9f, 15.8f, 19.2f,
            22.8f, 26.9f, 31.3f, 36.1f, 41.4f, 47.2f, 53.6f, 60.3f,
            67.7f, 75.5f, 83.9f, 92.8f, 102.4f,112.6f,123.5f,135.1f
        };

        float typeBaseScale = 1.0f;
        if (erType == 0) typeBaseScale = 1.10f;      // HALL
        else if (erType == 1) typeBaseScale = 1.25f; // RANDOM
        else if (erType == 2) typeBaseScale = 1.35f; // REVERSE
        else if (erType == 3) typeBaseScale = 0.72f; // PLATE (tighter, denser)

        const float sizeScale = typeBaseScale * (0.20f + 0.40f * roomSize);
        float wetL = 0.0f;
        float wetR = 0.0f;

        for (int t = 0; t < kNumErTaps; ++t) {
            float tapMsL = preDlyMs + kClusterMsL[t] * sizeScale;
            float tapMsR = preDlyMs + kClusterMsR[t] * sizeScale;
            wetL += erDram_.readL(DramBuffer::msToSamples(tapMsL, sr)) * cachedErGainL_[t];
            wetR += erDram_.readR(DramBuffer::msToSamples(tapMsR, sr)) * cachedErGainR_[t];
        }
        erDram_.advanceWriteHead();

        // Low-pass damp the liveness recirculation tail
        erFbL_ = 0.6f * erFbL_ + 0.4f * (wetL * 0.18f);
        erFbR_ = 0.6f * erFbR_ + 0.4f * (wetR * 0.18f);

        wetL *= 0.24f;
        wetR *= 0.24f;

        // Post-diffusion all-pass stage for extra lushness on Gate Reverb
        {
            size_t idxL = apIdxL_[0];
            float bL = apBufL_[0][idxL];
            float inL = wetL + bL * 0.5f;
            apBufL_[0][idxL] = quantizeWord(inL);
            wetL = bL - inL * 0.5f;
            apIdxL_[0] = (idxL + 1 >= 173) ? 0 : (idxL + 1);

            size_t idxR = apIdxR_[0];
            float bR = apBufR_[0][idxR];
            float inR = wetR + bR * 0.5f;
            apBufR_[0][idxR] = quantizeWord(inR);
            wetR = bR - inR * 0.5f;
            apIdxR_[0] = (idxR + 1 >= 191) ? 0 : (idxR + 1);
        }

        if (hpfFreq > 25.0f) {
            hpf_.setHighPass(sr, hpfFreq, 0.707);
            hpf_.processStereo(wetL, wetR);
        }
        if (lpfFreq < 11500.0f) {
            lpf_.setLowPass(sr, lpfFreq, 0.707);
            lpf_.processStereo(wetL, wetR);
        }

        if (isGated) {
            float env = isReverseGate
                ? (gateEnv_ * (0.12f + 0.88f * revSwellProgress_ * revSwellProgress_))
                : gateEnv_;
            wetL *= env;
            wetR *= env;
        }

        l = wetL;
        r = wetR;
    }

    void updateGateEnvelope(float inPeak, float thresh, float holdMs, float relMs, float sr) {
        float holdSamples = std::max(1.0f, holdMs * 0.001f * sr);
        if (inPeak > thresh) {
            if (!gateOpen_ || gateEnv_ < 0.2f) {
                revSwellProgress_ = 0.0f;
            }
            gateOpen_ = true;
            gateEnv_ = 1.0f;
            gateTimer_ = holdSamples;
        }
        if (gateOpen_) {
            revSwellProgress_ = std::min(1.0f, revSwellProgress_ + 1.0f / holdSamples);
            gateTimer_ -= 1.0f;
            if (gateTimer_ <= 0.0f) {
                gateOpen_ = false;
            }
        } else {
            float relStep = 1.0f / std::max(1.0f, relMs * 0.001f * sr);
            gateEnv_ = std::max(0.0f, gateEnv_ - relStep);
        }
    }

    std::vector<float> preDelayBuf_;
    size_t preDelayWrite_ = 0;

    std::array<std::vector<float>, kNumCombs> combBufL_;
    std::array<std::vector<float>, kNumCombs> combBufR_;
    std::array<size_t, kNumCombs> combIdxL_{};
    std::array<size_t, kNumCombs> combIdxR_{};
    std::array<float, kNumCombs> combFilterL_{};
    std::array<float, kNumCombs> combFilterR_{};

    std::array<std::vector<float>, kNumAllPass> apBufL_;
    std::array<std::vector<float>, kNumAllPass> apBufR_;
    std::array<size_t, kNumAllPass> apIdxL_{};
    std::array<size_t, kNumAllPass> apIdxR_{};

    std::array<std::vector<float>, 2> erDiffBufL_;
    std::array<std::vector<float>, 2> erDiffBufR_;
    std::array<size_t, 2> erDiffIdxL_{};
    std::array<size_t, 2> erDiffIdxR_{};
    float erFbL_ = 0.0f, erFbR_ = 0.0f;

    DramBuffer erDram_;
    Ym3807LfoBank lfo_;
    BiquadFilter hpf_{}, lpf_{};

    float gateEnv_ = 0.0f;
    float gateTimer_ = 0.0f;
    float revSwellProgress_ = 0.0f;
    bool gateOpen_ = false;

    // Cached reverb comb decay gains (recomputed only when revType, revTime, or sr changes)
    int cachedRevType_ = -1;
    float cachedRevTime_ = -1.0f;
    float cachedCombSr_ = -1.0f;
    std::array<float, kNumCombs> cachedCombGainL_{};
    std::array<float, kNumCombs> cachedCombGainR_{};

    // Cached 24-tap early reflection weights (recomputed only when erType, liveness, or isReverseGate changes)
    int cachedErType_ = -1;
    float cachedErLiveness_ = -1.0f;
    bool cachedErRevGate_ = false;
    std::array<float, kNumErTaps> cachedErGainL_{};
    std::array<float, kNumErTaps> cachedErGainR_{};

    BitDepthMode bitMode_ = BitDepthMode::Linear16Bit;
    RamModel ramModel_ = RamModel::Spx90_MkI;
};

} // namespace aero
