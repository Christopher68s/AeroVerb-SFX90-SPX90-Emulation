#pragma once

#include "Spx90Types.h"
#include "ConverterEngine.h"
#include "Ym3807LfoBank.h"
#include <vector>
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>

namespace aero {

class EqPanFreezeEngine {
public:
    static constexpr size_t kMaxFreezeSamples = 65536; // >2000 ms at 31.25 kHz
    static constexpr size_t kLookaheadSize = 8192;     // >170 ms at 48 kHz
    static constexpr size_t kLookaheadMask = kLookaheadSize - 1;

    EqPanFreezeEngine() {
        freezeBufL_.resize(kMaxFreezeSamples, 0.0f);
        freezeBufR_.resize(kMaxFreezeSamples, 0.0f);
        lookaheadBufL_.resize(kLookaheadSize, 0.0f);
        lookaheadBufR_.resize(kLookaheadSize, 0.0f);
    }

    void reset() {
        hpf_.reset();
        lowShelf_.reset();
        midPeak_.reset();
        highPeak_.reset();
        lpf_.reset();
        panLowEq_.reset();
        panHighEq_.reset();
        lfo_.reset();

        std::fill(freezeBufL_.begin(), freezeBufL_.end(), 0.0f);
        std::fill(freezeBufR_.begin(), freezeBufR_.end(), 0.0f);
        std::fill(lookaheadBufL_.begin(), lookaheadBufL_.end(), 0.0f);
        std::fill(lookaheadBufR_.begin(), lookaheadBufR_.end(), 0.0f);
        lookaheadWrite_ = 0;

        gateState_ = GateState::Idle;
        gateEnv_ = 0.0f;
        gateTimerSamples_ = 0.0f;
        compEnv_ = 0.0f;

        isRecordingFreeze_ = false;
        hasFrozenAudio_ = false;
        freezeWriteIdx_ = 0;
        freezeReadPos_ = 0.0;
        prevManualTrig_ = false;
        midiTriggered_.store(false, std::memory_order_relaxed);
    }

    void setConfig(BitDepthMode bitMode, RamModel ramModel) {
        bitMode_ = bitMode;
        ramModel_ = ramModel;
    }

    void triggerMidiNote(int midiNote, int velocity) {
        if (velocity > 0) {
            lastMidiNote_.store(midiNote, std::memory_order_relaxed);
            midiTriggered_.store(true, std::memory_order_relaxed);
        }
    }

    void processTick(AlgorithmType alg,
                     const std::array<float, kMaxProgramParams>& params,
                     float& l,
                     float& r,
                     float sampleRate = 31250.0f) {
        switch (alg) {
            case AlgorithmType::ParametricEQ:
                processParametricEq(params, l, r, sampleRate);
                break;
            case AlgorithmType::AutoPan:
                processAutoPan(params, l, r, sampleRate);
                break;
            case AlgorithmType::TriggeredPan:
                processTriggeredPan(params, l, r, sampleRate);
                break;
            case AlgorithmType::AdrNoiseGate:
                processAdrGate(params, l, r, sampleRate);
                break;
            case AlgorithmType::Compressor:
                processCompressor(params, l, r, sampleRate);
                break;
            case AlgorithmType::FreezeA:
                processFreeze(false, params, l, r, sampleRate);
                break;
            case AlgorithmType::FreezeB:
                processFreeze(true, params, l, r, sampleRate);
                break;
            default:
                break;
        }
        midiTriggered_.store(false, std::memory_order_relaxed);
    }

private:
    enum class GateState { Idle, Attack, Hold, Release };

    // #30 PARAMETRIC EQ
    void processParametricEq(const std::array<float, kMaxProgramParams>& p,
                             float& l, float& r, float sr) {
        const float hpfFreq  = std::clamp(p[0], 20.0f, 1000.0f);
        const float lowGain  = std::clamp(p[1], -15.0f, 15.0f);
        const float midFreq  = std::clamp(p[2], 315.0f, 4000.0f);
        const float midGain  = std::clamp(p[3], -15.0f, 15.0f);
        const float midQ     = std::clamp(p[4], 0.5f, 5.0f);
        const float highFreq = std::clamp(p[5], 800.0f, 11000.0f);
        const float highGain = std::clamp(p[6], -15.0f, 15.0f);
        const float lpfFreq  = std::clamp(p[7], 1000.0f, 12000.0f);

        if (hpfFreq > 25.0f) {
            hpf_.setHighPass(sr, hpfFreq, 0.707);
            hpf_.processStereo(l, r);
        }
        if (std::abs(lowGain) > 0.1f) {
            lowShelf_.setLowShelf(sr, 120.0, lowGain);
            lowShelf_.processStereo(l, r);
        }
        if (std::abs(midGain) > 0.1f) {
            midPeak_.setPeakingEq(sr, midFreq, midQ, midGain);
            midPeak_.processStereo(l, r);
        }
        if (std::abs(highGain) > 0.1f) {
            highPeak_.setHighShelf(sr, highFreq, highGain);
            highPeak_.processStereo(l, r);
        }
        if (lpfFreq < 11500.0f) {
            lpf_.setLowPass(sr, lpfFreq, 0.707);
            lpf_.processStereo(l, r);
        }
    }

    // #27 AUTO PAN
    void processAutoPan(const std::array<float, kMaxProgramParams>& p,
                        float& l, float& r, float sr) {
        const float speed = std::clamp(p[0], 0.1f, 20.0f);
        const int dir     = std::clamp(static_cast<int>(std::round(p[1])), 0, 2);
        const float depth = std::clamp(p[2], 0.0f, 100.0f) * 0.01f;
        const float lowG  = std::clamp(p[3], -15.0f, 15.0f);
        const float highG = std::clamp(p[4], -15.0f, 15.0f);

        LfoWaveform wave = LfoWaveform::Triangle;
        if (dir == 0) wave = LfoWaveform::SawUp;     // L -> R
        else if (dir == 1) wave = LfoWaveform::SawDown; // L <- R

        lfo_.setLfoConfig(0, speed, -1.0f, wave);
        lfo_.tick(sr);

        float panBipolar = lfo_.getOutput(0).bipolar * depth; // [-depth, +depth]
        float pan01 = 0.5f * (panBipolar + 1.0f);             // 0 = full L, 1 = full R

        float mono = 0.5f * (l + r);
        l = mono * std::cos(pan01 * 1.5707963f) * 1.414f;
        r = mono * std::sin(pan01 * 1.5707963f) * 1.414f;

        if (std::abs(lowG) > 0.1f || std::abs(highG) > 0.1f) {
            panLowEq_.setLowShelf(sr, 200.0, lowG);
            panHighEq_.setHighShelf(sr, 4000.0, highG);
            panLowEq_.processStereo(l, r);
            panHighEq_.processStereo(l, r);
        }
    }

    // #28 TRIGGERED PAN
    void processTriggeredPan(const std::array<float, kMaxProgramParams>& p,
                             float& l, float& r, float sr) {
        const float trgLvl = std::clamp(p[0], 1.0f, 100.0f) * 0.006f;
        const int dir      = std::clamp(static_cast<int>(std::round(p[1])), 0, 2);
        const float atkMs  = std::clamp(p[2], 5.0f, 1000.0f);
        const float holdMs = std::clamp(p[3], 5.0f, 1000.0f);
        const float relMs  = std::clamp(p[4], 5.0f, 2000.0f);
        const float depth  = std::clamp(p[5], 0.0f, 100.0f) * 0.01f;

        updateAhrEnvelope(std::max(std::abs(l), std::abs(r)), trgLvl, atkMs, holdMs, relMs, sr);

        float sweep = gateEnv_;
        float panBipolar = 0.0f;
        if (dir == 0) panBipolar = (2.0f * sweep - 1.0f) * depth;        // L -> R
        else if (dir == 1) panBipolar = (1.0f - 2.0f * sweep) * depth;   // R -> L
        else panBipolar = std::sin(sweep * 6.2831853f) * depth;          // L <-> R

        float pan01 = 0.5f * (panBipolar + 1.0f);
        float mono = 0.5f * (l + r);
        l = mono * std::cos(pan01 * 1.5707963f) * 1.414f;
        r = mono * std::sin(pan01 * 1.5707963f) * 1.414f;
    }

    // #18 ADR / NOISE GATE
    void processAdrGate(const std::array<float, kMaxProgramParams>& p,
                        float& l, float& r, float sr) {
        const float trgLvl = std::clamp(p[0], 1.0f, 100.0f) * 0.006f;
        const float trgDly = std::clamp(p[1], -100.0f, 100.0f);
        const float atkMs  = std::clamp(p[2], 5.0f, 2000.0f);
        const float holdMs = std::clamp(p[3], 5.0f, 2000.0f);
        const float relMs  = std::clamp(p[4], 5.0f, 2000.0f);
        const bool midiTrg = (p[5] >= 0.5f);

        // Negative Trg Delay: delay audio path (lookahead). Positive Trg Delay: delay sidechain trigger.
        lookaheadBufL_[lookaheadWrite_] = l;
        lookaheadBufR_[lookaheadWrite_] = r;
        int audioDlySamples = (trgDly < 0.0f) ? static_cast<int>(std::clamp(-trgDly, 0.0f, 100.0f) * 0.001f * sr) : 0;
        int trigDlySamples  = (trgDly > 0.0f) ? static_cast<int>(std::clamp( trgDly, 0.0f, 100.0f) * 0.001f * sr) : 0;
        size_t audioReadIdx = (lookaheadWrite_ - static_cast<size_t>(audioDlySamples)) & kLookaheadMask;
        size_t trigReadIdx  = (lookaheadWrite_ - static_cast<size_t>(trigDlySamples))  & kLookaheadMask;
        lookaheadWrite_ = (lookaheadWrite_ + 1) & kLookaheadMask;

        float detSig = std::max(std::abs(lookaheadBufL_[trigReadIdx]), std::abs(lookaheadBufR_[trigReadIdx]));
        if (midiTrg) {
            detSig = midiTriggered_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        }

        updateAhrEnvelope(detSig, trgLvl, atkMs, holdMs, relMs, sr);

        l = lookaheadBufL_[audioReadIdx] * gateEnv_;
        r = lookaheadBufR_[audioReadIdx] * gateEnv_;
    }

    // #19 COMPRESSOR
    void processCompressor(const std::array<float, kMaxProgramParams>& p,
                           float& l, float& r, float sr) {
        const float sens   = std::clamp(p[0], 1.0f, 100.0f) * 0.01f;
        const float trgDly = std::clamp(p[1], -100.0f, 100.0f);
        const float atkMs  = std::clamp(p[2], 5.0f, 200.0f);
        const float relMs  = std::clamp(p[3], 10.0f, 2000.0f);

        lookaheadBufL_[lookaheadWrite_] = l;
        lookaheadBufR_[lookaheadWrite_] = r;
        int audioDlySamples = (trgDly < 0.0f) ? static_cast<int>(std::clamp(-trgDly, 0.0f, 100.0f) * 0.001f * sr) : 0;
        int trigDlySamples  = (trgDly > 0.0f) ? static_cast<int>(std::clamp( trgDly, 0.0f, 100.0f) * 0.001f * sr) : 0;
        size_t audioReadIdx = (lookaheadWrite_ - static_cast<size_t>(audioDlySamples)) & kLookaheadMask;
        size_t trigReadIdx  = (lookaheadWrite_ - static_cast<size_t>(trigDlySamples))  & kLookaheadMask;
        lookaheadWrite_ = (lookaheadWrite_ + 1) & kLookaheadMask;

        float peak = std::max(std::abs(lookaheadBufL_[trigReadIdx]), std::abs(lookaheadBufR_[trigReadIdx]));
        float atkCoeff = std::exp(-1.0f / std::max(1.0f, atkMs * 0.001f * sr));
        float relCoeff = std::exp(-1.0f / std::max(1.0f, relMs * 0.001f * sr));

        if (peak > compEnv_)
            compEnv_ = atkCoeff * compEnv_ + (1.0f - atkCoeff) * peak;
        else
            compEnv_ = relCoeff * compEnv_ + (1.0f - relCoeff) * peak;

        float thresh = std::max(0.05f, 0.65f * (1.0f - 0.8f * sens));
        float gain = 1.0f;
        if (compEnv_ > thresh) {
            float over = compEnv_ / thresh;
            gain = std::pow(over, -0.70f); // ~3.3:1 vintage digital compression curve
        }
        float makeup = 1.0f + 0.85f * sens;
        l = std::clamp(lookaheadBufL_[audioReadIdx] * gain * makeup, -1.0f, 1.0f);
        r = std::clamp(lookaheadBufR_[audioReadIdx] * gain * makeup, -1.0f, 1.0f);
    }

    // #25 FREEZE A & #26 FREEZE B
    void processFreeze(bool isFreezeB,
                       const std::array<float, kMaxProgramParams>& p,
                       float& l, float& r, float sr) {
        const float ramScale = (ramModel_ == RamModel::Spx90_MkII) ? 4.0f : 1.0f;
        const bool autoMode  = (p[0] >= 0.5f);
        const float trgLvl   = std::clamp(p[1], 1.0f, 100.0f) * 0.005f;
        const bool manualTrg = (p[5] >= 0.5f);

        size_t maxBufSamples = static_cast<size_t>(std::min<double>(kMaxFreezeSamples - 2, 0.500 * ramScale * sr));
        size_t startIdx = 0;
        size_t endIdx = maxBufSamples;

        float pitchRatio = 1.0f;
        float fbGain = 0.0f;

        if (!isFreezeB) {
            float startMs = std::clamp(p[2], 0.0f, 450.0f) * ramScale;
            float endMs   = std::clamp(p[3], 10.0f, 500.0f) * ramScale;
            if (endMs <= startMs + 5.0f) endMs = startMs + 10.0f;
            startIdx = std::min(maxBufSamples - 16, static_cast<size_t>(startMs * 0.001f * sr));
            endIdx   = std::clamp(static_cast<size_t>(endMs * 0.001f * sr), startIdx + 16, maxBufSamples);
            fbGain   = std::clamp(p[4], 0.0f, 99.0f) * 0.01f;
        } else {
            float semi = std::clamp(p[2], -12.0f, 12.0f);
            float fine = std::clamp(p[3], -100.0f, 100.0f);
            int baseKey = static_cast<int>(std::round(p[4]));
            int midiNote = lastMidiNote_.load(std::memory_order_relaxed);
            if (baseKey >= 36 && midiNote >= 0) {
                semi += static_cast<float>(midiNote - baseKey);
            }
            pitchRatio = std::pow(2.0f, (semi + fine * 0.01f) / 12.0f);
            pitchRatio = std::clamp(pitchRatio, 0.25f, 4.0f);
        }

        float peak = std::max(std::abs(l), std::abs(r));
        const bool midiTrig = midiTriggered_.load(std::memory_order_relaxed);
        bool trigEdge = (manualTrg && !prevManualTrig_) || midiTrig ||
                        (autoMode && !isRecordingFreeze_ && !hasFrozenAudio_ && peak > trgLvl);
        prevManualTrig_ = manualTrg;

        if (trigEdge) {
            isRecordingFreeze_ = true;
            freezeWriteIdx_ = 0;
            freezeReadPos_ = static_cast<double>(startIdx);
        }

        if (isRecordingFreeze_) {
            freezeBufL_[freezeWriteIdx_] = l + freezeBufL_[freezeWriteIdx_] * fbGain;
            freezeBufR_[freezeWriteIdx_] = r + freezeBufR_[freezeWriteIdx_] * fbGain;
            ++freezeWriteIdx_;
            if (freezeWriteIdx_ >= endIdx) {
                isRecordingFreeze_ = false;
                hasFrozenAudio_ = true;
            }
        }

        if (hasFrozenAudio_ || isRecordingFreeze_) {
            size_t validEnd = hasFrozenAudio_ ? endIdx : std::max<size_t>(startIdx + 1, freezeWriteIdx_);
            size_t i0 = static_cast<size_t>(freezeReadPos_);
            if (i0 >= validEnd) i0 = startIdx;
            size_t i1 = (i0 + 1 < validEnd) ? (i0 + 1) : startIdx;
            float frac = static_cast<float>(freezeReadPos_ - std::floor(freezeReadPos_));

            l = freezeBufL_[i0] * (1.0f - frac) + freezeBufL_[i1] * frac;
            r = freezeBufR_[i0] * (1.0f - frac) + freezeBufR_[i1] * frac;

            freezeReadPos_ += pitchRatio;
            if (freezeReadPos_ >= static_cast<double>(validEnd)) {
                freezeReadPos_ = static_cast<double>(startIdx) +
                                 std::fmod(freezeReadPos_ - static_cast<double>(startIdx),
                                           static_cast<double>(std::max<size_t>(1, validEnd - startIdx)));
            }
        }
    }

    void updateAhrEnvelope(float detSig, float thresh, float atkMs, float holdMs, float relMs, float sr) {
        if ((detSig > thresh || midiTriggered_.load(std::memory_order_relaxed)) &&
            (gateState_ == GateState::Idle || gateState_ == GateState::Release)) {
            gateState_ = GateState::Attack;
        }

        switch (gateState_) {
            case GateState::Idle:
                gateEnv_ = 0.0f;
                break;
            case GateState::Attack: {
                float step = 1.0f / std::max(1.0f, atkMs * 0.001f * sr);
                gateEnv_ += step;
                if (gateEnv_ >= 1.0f) {
                    gateEnv_ = 1.0f;
                    gateState_ = GateState::Hold;
                    gateTimerSamples_ = holdMs * 0.001f * sr;
                }
                break;
            }
            case GateState::Hold:
                gateEnv_ = 1.0f;
                gateTimerSamples_ -= 1.0f;
                if (gateTimerSamples_ <= 0.0f && detSig <= thresh) {
                    gateState_ = GateState::Release;
                }
                break;
            case GateState::Release: {
                float step = 1.0f / std::max(1.0f, relMs * 0.001f * sr);
                gateEnv_ -= step;
                if (gateEnv_ <= 0.0f) {
                    gateEnv_ = 0.0f;
                    gateState_ = GateState::Idle;
                }
                break;
            }
        }
    }

    BiquadFilter hpf_{}, lowShelf_{}, midPeak_{}, highPeak_{}, lpf_{};
    BiquadFilter panLowEq_{}, panHighEq_{};
    Ym3807LfoBank lfo_;

    std::vector<float> freezeBufL_;
    std::vector<float> freezeBufR_;
    std::vector<float> lookaheadBufL_;
    std::vector<float> lookaheadBufR_;
    size_t lookaheadWrite_ = 0;

    GateState gateState_ = GateState::Idle;
    float gateEnv_ = 0.0f;
    float gateTimerSamples_ = 0.0f;
    float compEnv_ = 0.0f;

    bool isRecordingFreeze_ = false;
    bool hasFrozenAudio_ = false;
    size_t freezeWriteIdx_ = 0;
    double freezeReadPos_ = 0.0;
    bool prevManualTrig_ = false;

    std::atomic<int> lastMidiNote_{60};
    std::atomic<bool> midiTriggered_{false};
    BitDepthMode bitMode_ = BitDepthMode::Linear16Bit;
    RamModel ramModel_ = RamModel::Spx90_MkI;
};

} // namespace aero
