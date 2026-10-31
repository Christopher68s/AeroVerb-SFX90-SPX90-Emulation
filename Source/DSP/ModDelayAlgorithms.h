#pragma once

#include "Spx90Types.h"
#include "Ym3807LfoBank.h"
#include "DramBuffer.h"
#include "ConverterEngine.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace aero {

class ModDelayEngine {
public:
    void reset() {
        dram_.clear();
        lfoBank_.reset();
        // Configure 6 multi-phase LFOs for Symphonic / Chorus / Flanger
        lfoBank_.setLfoConfig(0, 0.7f, 0.000f, LfoWaveform::Sine);
        lfoBank_.setLfoConfig(1, 0.7f, 0.333f, LfoWaveform::Sine);
        lfoBank_.setLfoConfig(2, 0.7f, 0.667f, LfoWaveform::Sine);
        lfoBank_.setLfoConfig(3, 0.7f, 0.167f, LfoWaveform::Sine);
        lfoBank_.setLfoConfig(4, 0.7f, 0.500f, LfoWaveform::Sine);
        lfoBank_.setLfoConfig(5, 0.7f, 0.833f, LfoWaveform::Sine);
        // Triangle LFOs for Phasing / Flange B
        lfoBank_.setLfoConfig(6, 0.5f, 0.000f, LfoWaveform::Triangle);
        lfoBank_.setLfoConfig(7, 0.5f, 0.250f, LfoWaveform::Triangle);

        fbStateL_ = fbStateR_ = 0.0f;
        lpDampL_ = lpDampR_ = 0.0f;
        vibEnv_ = 0.0f;
        vibRampSamples_ = 0.0f;
        lowEq_.reset();
        highEq_.reset();
        for (auto& s : phaserStateL_) s = 0.0f;
        for (auto& s : phaserStateR_) s = 0.0f;
    }

    void setConfig(BitDepthMode bitMode, RamModel ramModel) {
        dram_.setConfig(bitMode, ramModel);
        ramModel_ = ramModel;
    }

    // Process one stereo sample at 31.25 kHz (returns wet signal in l, r)
    void processTick(AlgorithmType alg,
                     const std::array<float, kMaxProgramParams>& params,
                     float& l,
                     float& r,
                     float sampleRate = 31250.0f) {
        const float ramScale = (ramModel_ == RamModel::Spx90_MkII) ? 4.0f : 1.0f;

        switch (alg) {
            case AlgorithmType::Symphonic:
                processSymphonic(params, l, r, sampleRate);
                break;
            case AlgorithmType::ChorusA:
            case AlgorithmType::ChorusB:
                processChorus(alg == AlgorithmType::ChorusB, params, l, r, sampleRate);
                break;
            case AlgorithmType::StereoFlangeA:
            case AlgorithmType::StereoFlangeB:
                processFlanger(alg == AlgorithmType::StereoFlangeB, params, l, r, sampleRate);
                break;
            case AlgorithmType::StereoPhasing:
                processPhaser(params, l, r, sampleRate);
                break;
            case AlgorithmType::Tremolo:
                processTremolo(params, l, r, sampleRate);
                break;
            case AlgorithmType::DelayVibrato:
                processDelayVibrato(params, l, r, sampleRate);
                break;
            case AlgorithmType::DelayLR:
                processDelayLR(params, l, r, sampleRate, ramScale, false);
                break;
            case AlgorithmType::StereoEcho:
                processDelayLR(params, l, r, sampleRate, ramScale, true);
                break;
            default:
                break;
        }
    }

private:
    // #15 SYMPHONIC:
    // Three-voice short-delay chorus per channel driven by YM3807 multi-phase LFO bank.
    // Minimum delay times sweep down to ~0.18 ms so modulated voices brush right against
    // the dry signal, creating the iconic SPX90 flange-like whoosh without feedback resonance.
    void processSymphonic(const std::array<float, kMaxProgramParams>& p,
                          float& l, float& r, float sr) {
        const float modFreq  = std::clamp(p[0], 0.1f, 20.0f);
        const float modDepth = std::clamp(p[1], 0.0f, 100.0f) * 0.01f;
        const float lowGain  = std::clamp(p[2], -15.0f, 15.0f);
        const float highGain = std::clamp(p[3], -15.0f, 15.0f);

        // Slight detune across the 3 LFO voice pairs for organic movement
        lfoBank_.setLfoFrequency(0, modFreq * 0.97f);
        lfoBank_.setLfoFrequency(1, modFreq * 1.00f);
        lfoBank_.setLfoFrequency(2, modFreq * 1.03f);
        lfoBank_.setLfoFrequency(3, modFreq * 0.98f);
        lfoBank_.setLfoFrequency(4, modFreq * 1.01f);
        lfoBank_.setLfoFrequency(5, modFreq * 1.04f);
        lfoBank_.tick(sr);

        float monoIn = 0.5f * (l + r);
        dram_.writeStereo(l + 0.15f * monoIn, r + 0.15f * monoIn);

        // 3 ultra-short center delays (ms) that sweep down close to 0.15 ms at high depth
        constexpr float kBaseMs[3]  = { 1.15f, 1.95f, 2.85f };
        constexpr float kSweepMs[3] = { 0.98f, 1.65f, 2.40f };

        float wetL = 0.0f;
        float wetR = 0.0f;

        for (int v = 0; v < 3; ++v) {
            const auto& lfoL = lfoBank_.getOutput(v);
            const auto& lfoR = lfoBank_.getOutput(v + 3);

            float dlyL_ms = kBaseMs[v] + lfoL.bipolar * kSweepMs[v] * modDepth;
            float dlyR_ms = kBaseMs[v] + lfoR.bipolar * kSweepMs[v] * modDepth;

            // YM3807 synchronous subtle amplitude modulation per voice
            float amL = 1.0f - 0.18f * modDepth * lfoL.bipolar;
            float amR = 1.0f - 0.18f * modDepth * lfoR.bipolar;

            wetL += dram_.readL(DramBuffer::msToSamples(dlyL_ms, sr)) * amL;
            wetR += dram_.readR(DramBuffer::msToSamples(dlyR_ms, sr)) * amR;
        }
        dram_.advanceWriteHead();

        wetL *= 0.42f;
        wetR *= 0.42f;

        if (std::abs(lowGain) > 0.1f || std::abs(highGain) > 0.1f) {
            lowEq_.setLowShelf(sr, 200.0, lowGain);
            highEq_.setHighShelf(sr, 4000.0, highGain);
            lowEq_.processStereo(wetL, wetR);
            highEq_.processStereo(wetL, wetR);
        }

        l = wetL;
        r = wetR;
    }

    // #11 CHORUS A & #12 CHORUS B
    void processChorus(bool isChorusB,
                       const std::array<float, kMaxProgramParams>& p,
                       float& l, float& r, float sr) {
        const float modFreq = std::clamp(p[0], 0.1f, 20.0f);
        const float dmDepth = std::clamp(p[1], 0.0f, 100.0f) * 0.01f;
        const float baseDly = std::clamp(p[2], 0.1f, 50.0f);
        const float amDepth = std::clamp(p[3], 0.0f, 100.0f) * 0.01f;
        const float lowG    = std::clamp(p[4], -15.0f, 15.0f);
        const float highG   = std::clamp(p[5], -15.0f, 15.0f);

        lfoBank_.setLfoFrequency(0, modFreq);
        lfoBank_.setLfoFrequency(1, modFreq * (isChorusB ? 1.08f : 1.0f));
        lfoBank_.setLfoFrequency(2, modFreq * 0.93f);
        lfoBank_.tick(sr);

        dram_.writeStereo(l, r);

        float sweepMs = std::min(baseDly * 0.85f, 6.0f) * dmDepth;
        const auto& lfo0 = lfoBank_.getOutput(0);
        const auto& lfo1 = lfoBank_.getOutput(1);
        const auto& lfo2 = lfoBank_.getOutput(2);

        float dlyL1 = baseDly + lfo0.bipolar * sweepMs;
        float dlyR1 = baseDly - lfo0.bipolar * sweepMs; // Quadrature/inverted stereo
        float dlyL2 = baseDly * 1.35f + lfo1.bipolar * sweepMs * 0.8f;
        float dlyR2 = baseDly * 1.35f + lfo2.bipolar * sweepMs * 0.8f;

        float amL = 1.0f - amDepth * 0.5f * lfo0.unipolar;
        float amR = 1.0f - amDepth * 0.5f * (1.0f - lfo0.unipolar);

        float wetL = isChorusB
            ? 0.55f * (dram_.readL(DramBuffer::msToSamples(dlyL1, sr)) + dram_.readL(DramBuffer::msToSamples(dlyL2, sr))) * amL
            : dram_.readL(DramBuffer::msToSamples(dlyL1, sr)) * amL;
        float wetR = isChorusB
            ? 0.55f * (dram_.readR(DramBuffer::msToSamples(dlyR1, sr)) + dram_.readR(DramBuffer::msToSamples(dlyR2, sr))) * amR
            : dram_.readR(DramBuffer::msToSamples(dlyR1, sr)) * amR;

        dram_.advanceWriteHead();

        if (std::abs(lowG) > 0.1f || std::abs(highG) > 0.1f) {
            lowEq_.setLowShelf(sr, 200.0, lowG);
            highEq_.setHighShelf(sr, 4000.0, highG);
            lowEq_.processStereo(wetL, wetR);
            highEq_.processStereo(wetL, wetR);
        }

        l = wetL;
        r = wetR;
    }

    // #9 STEREO FLANGE A & #10 STEREO FLANGE B
    void processFlanger(bool isFlangeB,
                        const std::array<float, kMaxProgramParams>& p,
                        float& l, float& r, float sr) {
        const float modFreq  = std::clamp(p[0], 0.1f, 20.0f);
        const float modDepth = std::clamp(p[1], 0.0f, 100.0f) * 0.01f;
        const float modDelay = std::clamp(p[2], 0.1f, 50.0f);
        const float fbGain   = std::clamp(p[3], -99.0f, 99.0f) * 0.0095f;

        int lfoIdxL = isFlangeB ? 6 : 0;
        int lfoIdxR = isFlangeB ? 7 : 3;
        lfoBank_.setLfoFrequency(lfoIdxL, modFreq);
        lfoBank_.setLfoFrequency(lfoIdxR, modFreq);
        lfoBank_.tick(sr);

        float inL = std::clamp(l + fbStateL_ * fbGain, -1.5f, 1.5f);
        float inR = std::clamp(r + fbStateR_ * fbGain, -1.5f, 1.5f);
        dram_.writeStereo(inL, inR);

        float sweepMs = std::max(0.2f, modDelay * 2.5f) * modDepth;
        float dlyL = std::max(0.1f, modDelay + lfoBank_.getOutput(lfoIdxL).unipolar * sweepMs);
        float dlyR = std::max(0.1f, modDelay + (1.0f - lfoBank_.getOutput(lfoIdxR).unipolar) * sweepMs);

        float wetL = dram_.readL(DramBuffer::msToSamples(dlyL, sr));
        float wetR = dram_.readR(DramBuffer::msToSamples(dlyR, sr));
        dram_.advanceWriteHead();

        fbStateL_ = wetL;
        fbStateR_ = wetR;

        if (isFlangeB) {
            const float lowG  = std::clamp(p[4], -15.0f, 15.0f);
            const float highG = std::clamp(p[5], -15.0f, 15.0f);
            if (std::abs(lowG) > 0.1f || std::abs(highG) > 0.1f) {
                lowEq_.setLowShelf(sr, 200.0, lowG);
                highEq_.setHighShelf(sr, 4000.0, highG);
                lowEq_.processStereo(wetL, wetR);
                highEq_.processStereo(wetL, wetR);
            }
        }

        l = wetL;
        r = wetR;
    }

    // #13 STEREO PHASING
    void processPhaser(const std::array<float, kMaxProgramParams>& p,
                       float& l, float& r, float sr) {
        const float modFreq  = std::clamp(p[0], 0.1f, 20.0f);
        const float modDepth = std::clamp(p[1], 0.0f, 100.0f) * 0.01f;
        const float baseDly  = std::clamp(p[2], 0.1f, 8.0f);
        const float fbGain   = std::clamp(p[3], 0.0f, 99.0f) * 0.0085f;

        lfoBank_.setLfoFrequency(6, modFreq);
        lfoBank_.setLfoFrequency(7, modFreq);
        lfoBank_.tick(sr);

        // 6-stage all-pass cascade + short DRAM comb delay
        float lfoL = lfoBank_.getOutput(6).unipolar;
        float lfoR = 1.0f - lfoL;

        float minF = 180.0f / (0.5f + 0.5f * baseDly);
        float maxF = std::min(6500.0f, minF * (2.0f + 10.0f * modDepth));
        float fL = minF + lfoL * (maxF - minF);
        float fR = minF + lfoR * (maxF - minF);

        float tL = std::tan(3.14159265f * std::clamp(fL / sr, 0.005f, 0.42f));
        float tR = std::tan(3.14159265f * std::clamp(fR / sr, 0.005f, 0.42f));
        float aL = (1.0f - tL) / (1.0f + tL);
        float aR = (1.0f - tR) / (1.0f + tR);

        float xL = std::clamp(l + fbStateL_ * fbGain, -1.5f, 1.5f);
        float xR = std::clamp(r + fbStateR_ * fbGain, -1.5f, 1.5f);

        for (int s = 0; s < 6; ++s) {
            float yL = -aL * xL + phaserStateL_[s];
            phaserStateL_[s] = xL + aL * yL;
            xL = yL;

            float yR = -aR * xR + phaserStateR_[s];
            phaserStateR_[s] = xR + aR * yR;
            xR = yR;
        }

        fbStateL_ = xL;
        fbStateR_ = xR;
        l = xL;
        r = xR;
    }

    // #14 TREMOLO
    void processTremolo(const std::array<float, kMaxProgramParams>& p,
                        float& l, float& r, float sr) {
        const float modFreq = std::clamp(p[0], 0.1f, 20.0f);
        const float amDepth = std::clamp(p[1], 0.0f, 100.0f) * 0.01f;
        const float dmDepth = std::clamp(p[2], 0.0f, 100.0f) * 0.01f;
        const float lowG    = std::clamp(p[3], -15.0f, 15.0f);
        const float highG   = std::clamp(p[4], -15.0f, 15.0f);

        lfoBank_.setLfoFrequency(0, modFreq);
        lfoBank_.tick(sr);

        dram_.writeStereo(l, r);
        const auto& lfo = lfoBank_.getOutput(0);

        float dlyMs = 1.5f + lfo.unipolar * 2.5f * dmDepth;
        float wetL = dram_.readL(DramBuffer::msToSamples(dlyMs, sr));
        float wetR = dram_.readR(DramBuffer::msToSamples(dlyMs, sr));
        dram_.advanceWriteHead();

        float gain = 1.0f - amDepth * lfo.unipolar;
        wetL *= gain;
        wetR *= gain;

        if (std::abs(lowG) > 0.1f || std::abs(highG) > 0.1f) {
            lowEq_.setLowShelf(sr, 200.0, lowG);
            highEq_.setHighShelf(sr, 4000.0, highG);
            lowEq_.processStereo(wetL, wetR);
            highEq_.processStereo(wetL, wetR);
        }

        l = wetL;
        r = wetR;
    }

    // #29 DELAY VIBRATO
    void processDelayVibrato(const std::array<float, kMaxProgramParams>& p,
                             float& l, float& r, float sr) {
        const float trgLvl   = std::clamp(p[0], 1.0f, 100.0f) * 0.005f;
        const float vibDelay = std::clamp(p[1], 0.0f, 3000.0f);
        const float maxSpeed = std::clamp(p[2], 0.1f, 20.0f);
        const float maxDepth = std::clamp(p[3], 0.0f, 100.0f) * 0.01f;
        const float baseDly  = std::clamp(p[4], 0.5f, 20.0f);

        float peak = std::max(std::abs(l), std::abs(r));
        if (peak > trgLvl && vibEnv_ < 0.2f) {
            vibRampSamples_ = 0.0f;
        }
        vibEnv_ = (peak > vibEnv_) ? (0.95f * vibEnv_ + 0.05f * peak) : (0.9992f * vibEnv_);

        float onsetSamples = std::max(1.0f, vibDelay * 0.001f * sr);
        vibRampSamples_ = std::min(vibRampSamples_ + 1.0f, onsetSamples * 2.0f);
        float ramp01 = std::clamp((vibRampSamples_ - onsetSamples * 0.5f) / onsetSamples, 0.0f, 1.0f);

        float curSpeed = 0.5f + ramp01 * (maxSpeed - 0.5f);
        float curDepth = ramp01 * maxDepth;

        lfoBank_.setLfoFrequency(0, curSpeed);
        lfoBank_.tick(sr);

        dram_.writeStereo(l, r);
        float dlyMs = baseDly + lfoBank_.getOutput(0).bipolar * (baseDly * 0.7f * curDepth);
        float samples = DramBuffer::msToSamples(dlyMs, sr);
        l = dram_.readL(samples);
        r = dram_.readR(samples);
        dram_.advanceWriteHead();
    }

    // #7 DELAY L,R & #8 STEREO ECHO
    void processDelayLR(const std::array<float, kMaxProgramParams>& p,
                        float& l, float& r, float sr, float ramScale, bool isEcho) {
        const float maxMs = (isEcho ? 250.0f : 500.0f) * ramScale;
        const float dlyL_ms = std::clamp(p[0] * ramScale, 0.1f, maxMs);
        const float fbL     = std::clamp(p[1], -99.0f, 99.0f) * 0.0095f;
        const float dlyR_ms = std::clamp(p[2] * ramScale, 0.1f, maxMs);
        const float fbR     = std::clamp(p[3], -99.0f, 99.0f) * 0.0095f;
        const float highDamp = std::clamp(p[4], 0.1f, 1.0f);

        float tapL = dram_.readL(DramBuffer::msToSamples(dlyL_ms, sr));
        float tapR = dram_.readR(DramBuffer::msToSamples(dlyR_ms, sr));

        // 1-pole high-frequency damping filter inside the delay feedback loop (HIGH = 0.1 dark .. 1.0 bright)
        float dampCoeff = 0.12f + 0.86f * highDamp;
        lpDampL_ += dampCoeff * (tapL - lpDampL_);
        lpDampR_ += dampCoeff * (tapR - lpDampR_);

        // Stereo Echo has subtle cross-coupling between L and R feedback loops
        float fbSigL = isEcho ? (0.90f * lpDampL_ + 0.10f * lpDampR_) : lpDampL_;
        float fbSigR = isEcho ? (0.90f * lpDampR_ + 0.10f * lpDampL_) : lpDampR_;

        dram_.writeStereo(std::clamp(l + fbSigL * fbL, -1.5f, 1.5f),
                          std::clamp(r + fbSigR * fbR, -1.5f, 1.5f));
        dram_.advanceWriteHead();

        l = lpDampL_;
        r = lpDampR_;
    }

    DramBuffer dram_;
    Ym3807LfoBank lfoBank_;
    RamModel ramModel_ = RamModel::Spx90_MkI;

    float fbStateL_ = 0.0f, fbStateR_ = 0.0f;
    float lpDampL_ = 0.0f, lpDampR_ = 0.0f;
    float vibEnv_ = 0.0f;
    float vibRampSamples_ = 0.0f;
    BiquadFilter lowEq_{};
    BiquadFilter highEq_{};
    std::array<float, 6> phaserStateL_{};
    std::array<float, 6> phaserStateR_{};
};

} // namespace aero
