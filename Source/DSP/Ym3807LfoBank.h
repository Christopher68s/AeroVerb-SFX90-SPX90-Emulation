#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace aero {

enum class LfoWaveform : int {
    Sine = 0,
    Triangle,
    SawUp,
    SawDown,
    QuasiRandom
};

struct Ym3807Output {
    float bipolar = 0.0f;   // [-1.0, +1.0]
    float unipolar = 0.5f;  // [0.0, 1.0]
};

// Emulates the Yamaha YM3807 16-channel LFO modulation generator IC
class Ym3807LfoBank {
public:
    static constexpr int kNumLfos = 16;

    void reset() {
        for (int i = 0; i < kNumLfos; ++i) {
            // Default hardware phase spreading across the 16 generators
            phases_[i] = static_cast<float>(i) / static_cast<float>(kNumLfos);
            freqsHz_[i] = 0.7f;
            waveforms_[i] = LfoWaveform::Sine;
            outputs_[i] = {};
        }
        rng_ = 0x38071985u;
    }

    void setLfoConfig(int channel, float freqHz, float initialPhase01, LfoWaveform wave = LfoWaveform::Sine) {
        if (channel < 0 || channel >= kNumLfos) return;
        freqsHz_[channel] = std::clamp(freqHz, 0.02f, 25.0f);
        waveforms_[channel] = wave;
        if (initialPhase01 >= 0.0f) {
            phases_[channel] = initialPhase01 - std::floor(initialPhase01);
        }
    }

    void setLfoFrequency(int channel, float freqHz) {
        if (channel < 0 || channel >= kNumLfos) return;
        freqsHz_[channel] = std::clamp(freqHz, 0.02f, 25.0f);
    }

    // Advance all 16 LFOs by one 31.25 kHz sample period (32 microseconds)
    inline void tick(float sampleRate = 31250.0f) {
        const float invSr = 1.0f / std::max(1000.0f, sampleRate);
        for (int i = 0; i < kNumLfos; ++i) {
            float p = phases_[i] + freqsHz_[i] * invSr;
            if (p >= 1.0f) {
                p -= std::floor(p);
                if (waveforms_[i] == LfoWaveform::QuasiRandom) {
                    rng_ = rng_ * 1664525u + 1013904223u;
                    randTarget_[i] = (static_cast<float>(rng_ & 0xFFFFu) / 32768.0f) - 1.0f;
                }
            }
            phases_[i] = p;

            float bi = 0.0f;
            switch (waveforms_[i]) {
                case LfoWaveform::Sine:
                    bi = std::sin(p * 6.28318530718f);
                    break;
                case LfoWaveform::Triangle:
                    bi = 1.0f - 4.0f * std::abs(p - 0.5f);
                    break;
                case LfoWaveform::SawUp:
                    bi = 2.0f * p - 1.0f;
                    break;
                case LfoWaveform::SawDown:
                    bi = 1.0f - 2.0f * p;
                    break;
                case LfoWaveform::QuasiRandom:
                    randCurrent_[i] += 0.005f * (randTarget_[i] - randCurrent_[i]);
                    bi = randCurrent_[i];
                    break;
            }
            // Quantize LFO modulation step to 10-bit resolution like the YM3807 register output
            bi = std::floor(bi * 512.0f + 0.5f) / 512.0f;
            outputs_[i].bipolar = bi;
            outputs_[i].unipolar = 0.5f * (bi + 1.0f);
        }
    }

    inline const Ym3807Output& getOutput(int channel) const {
        return outputs_[std::clamp(channel, 0, kNumLfos - 1)];
    }

private:
    std::array<float, kNumLfos> phases_{};
    std::array<float, kNumLfos> freqsHz_{};
    std::array<LfoWaveform, kNumLfos> waveforms_{};
    std::array<Ym3807Output, kNumLfos> outputs_{};
    std::array<float, kNumLfos> randTarget_{};
    std::array<float, kNumLfos> randCurrent_{};
    uint32_t rng_ = 0x38071985u;
};

} // namespace aero
