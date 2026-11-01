#include "../DSP/Spx90PresetDatabase.h"
#include "../DSP/Spx90DspEngine.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>

namespace {

float computeRms(const std::vector<float>& buf) {
    double sum = 0.0;
    for (float x : buf) sum += static_cast<double>(x) * static_cast<double>(x);
    return static_cast<float>(std::sqrt(sum / std::max<size_t>(1, buf.size())));
}

bool hasInvalidSample(const std::vector<float>& buf) {
    for (float x : buf) {
        if (std::isnan(x) || std::isinf(x)) return true;
    }
    return false;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << " Digital Symphony Instruments - Aero FX90 DSP Test Suite\n";
    std::cout << "=========================================================\n\n";

    constexpr double kHostSr = 48000.0;
    constexpr int kBlockSize = 8192;

    aero::Spx90DspEngine engine;
    engine.prepare(kHostSr);

    int passed = 0;
    int failed = 0;

    // 1. Test 12 kHz Anti-Aliasing & Reconstruction Filter Bandwidth at 31.25 kHz Clock
    {
        engine.reset();
        auto eqPass = aero::Spx90PresetDatabase::getRomPreset(29); // #30 PARAMETRIC EQ (flat)
        eqPass.balance = 100.0f;
        engine.setActiveProgram(eqPass);
        engine.setHardwareConfig(aero::BitDepthMode::Linear16Bit,
                                 aero::RamModel::Spx90_MkI,
                                 31250.0f, 0.0f, -96.0f, false, false, false, 1.0f);

        std::vector<float> l1k(kBlockSize), r1k(kBlockSize);
        std::vector<float> l15k(kBlockSize), r15k(kBlockSize);
        for (int i = 0; i < kBlockSize; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(kHostSr);
            l1k[i] = r1k[i] = 0.5f * std::sin(2.0f * 3.14159265f * 1000.0f * t);
            l15k[i] = r15k[i] = 0.5f * std::sin(2.0f * 3.14159265f * 15000.0f * t);
        }

        engine.processBlock(l1k.data(), r1k.data(), kBlockSize);
        engine.reset();
        engine.setActiveProgram(eqPass);
        engine.processBlock(l15k.data(), r15k.data(), kBlockSize);

        float rms1k = computeRms(l1k);
        float rms15k = computeRms(l15k);
        float attenDb = 20.0f * std::log10(std::max(1e-6f, rms15k) / std::max(1e-6f, rms1k));

        std::cout << "[TEST 1] 12 kHz Steep Filter Bandwidth Check:\n"
                  << "  1 kHz Passband RMS : " << rms1k << "\n"
                  << "  15 kHz Stopband RMS: " << rms15k << " (" << attenDb << " dB relative)\n";
        if (rms1k > 0.20f && attenDb < -20.0f) {
            std::cout << "  -> PASS (Steep rolloff above 12 kHz confirmed)\n\n";
            ++passed;
        } else {
            std::cout << "  -> FAIL\n\n";
            ++failed;
        }
    }

    // 2. Test All 30 Factory ROM Presets for stability, output energy, and zero NaN/Inf
    std::cout << "[TEST 2] Verifying all 30 ROM Presets (YM3804 + YM3807 Algorithms):\n";
    for (int p = 0; p < aero::kNumRomPresets; ++p) {
        engine.reset();
        const auto& preset = aero::Spx90PresetDatabase::getRomPreset(p);
        engine.setActiveProgram(preset);
        engine.setHardwareConfig(aero::BitDepthMode::Linear12Bit,
                                 aero::RamModel::Spx90_MkI,
                                 31250.0f, 1.5f, -80.0f, false, false, false, 1.0f);

        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        // Feed a burst + sustained chord input to trigger gates, freeze, reverbs, and pitch shifters
        for (int i = 0; i < kBlockSize / 2; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(kHostSr);
            float sig = 0.6f * std::sin(2.0f * 3.14159265f * 220.0f * t)
                      + 0.3f * std::sin(2.0f * 3.14159265f * 440.0f * t);
            left[i] = sig;
            right[i] = sig;
        }

        engine.handleMidiNoteOn(64, 100);
        engine.processBlock(left.data(), right.data(), kBlockSize);

        bool invalid = hasInvalidSample(left) || hasInvalidSample(right);
        float rmsL = computeRms(left);
        float rmsR = computeRms(right);

        bool ok = !invalid && (rmsL > 0.005f || rmsR > 0.005f);
        std::cout << "  #" << std::setw(2) << preset.memoryNumber << " "
                  << std::left << std::setw(16) << preset.name
                  << " | RMS L=" << std::fixed << std::setprecision(4) << rmsL
                  << " R=" << rmsR << (ok ? " [OK]\n" : " [FAIL]\n");

        if (ok) ++passed;
        else ++failed;
    }

    // 3. Verify Pitch B (Mono) vs Pitch C (Stereo Widener)
    {
        engine.reset();
        auto pitchB = aero::Spx90PresetDatabase::getRomPreset(21); // #22 PITCH CHANGE B
        pitchB.balance = 100.0f;
        engine.setActiveProgram(pitchB);

        std::vector<float> lB(kBlockSize), rB(kBlockSize);
        for (int i = 0; i < kBlockSize; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(kHostSr);
            lB[i] = rB[i] = 0.5f * std::sin(2.0f * 3.14159265f * 330.0f * t);
        }
        engine.processBlock(lB.data(), rB.data(), kBlockSize);

        float diffB = 0.0f;
        for (int i = 0; i < kBlockSize; ++i) diffB += std::abs(lB[i] - rB[i]);
        diffB /= static_cast<float>(kBlockSize);

        engine.reset();
        auto pitchC = aero::Spx90PresetDatabase::getRomPreset(22); // #23 PITCH CHANGE C (+8 / -8 cents)
        pitchC.balance = 100.0f;
        engine.setActiveProgram(pitchC);

        std::vector<float> lC(kBlockSize), rC(kBlockSize);
        for (int i = 0; i < kBlockSize; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(kHostSr);
            lC[i] = rC[i] = 0.5f * std::sin(2.0f * 3.14159265f * 330.0f * t);
        }
        engine.processBlock(lC.data(), rC.data(), kBlockSize);

        float diffC = 0.0f;
        for (int i = 0; i < kBlockSize; ++i) diffC += std::abs(lC[i] - rC[i]);
        diffC /= static_cast<float>(kBlockSize);

        std::cout << "\n[TEST 3] Pitch B (Mono) vs Pitch C (Stereo +8/-8c) Character Check:\n"
                  << "  Pitch B L-R Avg Diff: " << diffB << " (expect ~0.0 for mono)\n"
                  << "  Pitch C L-R Avg Diff: " << diffC << " (expect >0.02 for stereo detune)\n";
        if (diffB < 1e-4f && diffC > 0.02f) {
            std::cout << "  -> PASS\n";
            ++passed;
        } else {
            std::cout << "  -> FAIL\n";
            ++failed;
        }
    }

    // 4. Converter Drive must act once (A/D stage only): +6 dB drive => ~+6 dB small-signal wet gain
    {
        auto wetGainDb = [&](float driveDb) {
            aero::ConverterEngine conv;
            conv.prepare(kHostSr);
            conv.setParameters(aero::BitDepthMode::Linear16Bit, 31250.0f, driveDb, -120.0f, false);
            std::vector<float> out;
            for (int i = 0; i < 9600; ++i) {
                float l = 0.05f * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / static_cast<float>(kHostSr));
                float r = l;
                conv.processSampleFrame(l, r, [](float&, float&) {});
                if (i >= 4800) out.push_back(l);
            }
            return 20.0f * std::log10(std::max(1e-9f, computeRms(out)) / (0.05f / std::sqrt(2.0f)));
        };
        const float g0 = wetGainDb(0.0f);
        const float g6 = wetGainDb(6.0f);
        const float g12 = wetGainDb(12.0f);
        std::cout << "\n[TEST 4] Converter Drive applied once (identity DSP tick):\n"
                  << "  0 dB drive: " << g0 << " dB | +6 dB: " << g6 << " dB | +12 dB: " << g12 << " dB\n";
        if (std::abs(g0) < 0.5f && std::abs((g6 - g0) - 6.0f) < 1.0f && std::abs((g12 - g0) - 12.0f) < 1.5f) {
            std::cout << "  -> PASS\n";
            ++passed;
        } else {
            std::cout << "  -> FAIL (drive is being applied more than once)\n";
            ++failed;
        }
    }

    // 5. Converter transfer must be monotonic: a louder input never yields a quieter output
    {
        auto wetRms = [&](float amp) {
            aero::ConverterEngine conv;
            conv.prepare(kHostSr);
            conv.setParameters(aero::BitDepthMode::Linear16Bit, 31250.0f, 12.0f, -120.0f, false);
            std::vector<float> out;
            for (int i = 0; i < 9600; ++i) {
                float l = amp * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / static_cast<float>(kHostSr));
                float r = l;
                conv.processSampleFrame(l, r, [](float&, float&) {});
                if (i >= 4800) out.push_back(l);
            }
            return computeRms(out);
        };
        const float amps[] = {0.1f, 0.2f, 0.35f, 0.5f, 0.9f, 1.5f, 3.0f};
        bool monotonic = true;
        float prev = 0.0f;
        std::cout << "\n[TEST 5] Converter saturation is monotonic at +12 dB drive:\n  RMS by input amplitude:";
        for (float a : amps) {
            const float v = wetRms(a);
            std::cout << " " << a << "->" << v;
            if (v < prev * 0.98f) monotonic = false;
            prev = v;
        }
        std::cout << "\n";
        if (monotonic) {
            std::cout << "  -> PASS\n";
            ++passed;
        } else {
            std::cout << "  -> FAIL (output level falls as input rises)\n";
            ++failed;
        }
    }

    // 6. Pitch Change A at 99% feedback and 0 semitones must decay, not self-oscillate
    {
        engine.reset();
        auto pitchA = aero::Spx90PresetDatabase::getRomPreset(20); // #21 PITCH CHANGE A
        pitchA.params = {0.0f, 0.0f, 0.1f, 99.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        pitchA.balance = 100.0f;
        pitchA.outLevel = 100.0f;
        engine.setHardwareConfig(aero::BitDepthMode::Linear16Bit, aero::RamModel::Spx90_MkI,
                                 31250.0f, 0.0f, -120.0f, false, false, false, 1.0f);
        engine.setActiveProgram(pitchA);

        const int sec = static_cast<int>(kHostSr);
        std::vector<float> l(sec), r(sec);
        float tailPeak = 0.0f;
        for (int s = 0; s < 5; ++s) {
            for (int i = 0; i < sec; ++i) {
                float x = (s == 0 && i < 9600)
                    ? 0.3f * std::sin(2.0f * 3.14159265f * 1800.0f * static_cast<float>(i) / static_cast<float>(kHostSr))
                    : 0.0f;
                l[i] = r[i] = x;
            }
            engine.processBlock(l.data(), r.data(), sec);
            if (s == 4) {
                for (int i = 0; i < sec; ++i) tailPeak = std::max(tailPeak, std::abs(l[i]));
            }
        }
        std::cout << "\n[TEST 6] Pitch Change A, feedback 99%, 0 st: peak in 5th second = " << tailPeak << "\n";
        if (tailPeak < 0.02f) {
            std::cout << "  -> PASS\n";
            ++passed;
        } else {
            std::cout << "  -> FAIL (feedback loop is not stable)\n";
            ++failed;
        }
    }

    // 7. RAM Model Scaling: Delay L,R scales 4x in MkII (2000 ms), whereas Reverb Init Dly stays 50 ms
    {
        const auto& delayMetas = aero::Spx90PresetDatabase::getAlgorithmParamMeta(aero::AlgorithmType::DelayLR);
        const auto& revMetas   = aero::Spx90PresetDatabase::getAlgorithmParamMeta(aero::AlgorithmType::RevHall);

        std::string dlyMkI  = aero::Spx90PresetDatabase::formatParamValue(500.0f, delayMetas[0], aero::RamModel::Spx90_MkI);
        std::string dlyMkII = aero::Spx90PresetDatabase::formatParamValue(500.0f, delayMetas[0], aero::RamModel::Spx90_MkII);
        std::string revMkI  = aero::Spx90PresetDatabase::formatParamValue(50.0f,  revMetas[2],   aero::RamModel::Spx90_MkI);
        std::string revMkII = aero::Spx90PresetDatabase::formatParamValue(50.0f,  revMetas[2],   aero::RamModel::Spx90_MkII);

        std::cout << "\n[TEST 7] MkI vs MkII RAM Scaling (Finding #8):\n"
                  << "  Delay L,R (500ms base)  : MkI=" << dlyMkI << " | MkII=" << dlyMkII << "\n"
                  << "  Reverb Init Dly (50ms)  : MkI=" << revMkI << " | MkII=" << revMkII << "\n";
        if (dlyMkI == "500.0ms" && dlyMkII == "2000.0ms" && revMkI == "50.0ms" && revMkII == "50.0ms") {
            std::cout << "  -> PASS\n";
            ++passed;
        } else {
            std::cout << "  -> FAIL\n";
            ++failed;
        }
    }

    // 8. Reverb RT60 Progression up to 99.0s (Finding #9)
    {
        auto measureTailRmsAt3Sec = [&](float revTimeSec) {
            engine.reset();
            auto hall = aero::Spx90PresetDatabase::getRomPreset(0); // #01 REV 1 HALL
            hall.params[0] = revTimeSec;
            hall.params[1] = 1.0f; // High = 1.0
            hall.balance = 100.0f;
            engine.setHardwareConfig(aero::BitDepthMode::Linear16Bit, aero::RamModel::Spx90_MkI,
                                     31250.0f, 0.0f, -120.0f, false, false, false, 1.0f);
            engine.setActiveProgram(hall);

            const int burstSamples = static_cast<int>(kHostSr * 0.25); // 250ms burst
            const int tailSamples  = static_cast<int>(kHostSr * 3.0);  // 3.0s decay
            std::vector<float> l(burstSamples), r(burstSamples);
            for (int i = 0; i < burstSamples; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(kHostSr);
                l[i] = r[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * t);
            }
            engine.processBlock(l.data(), r.data(), burstSamples);

            std::vector<float> tl(tailSamples, 0.0f), tr(tailSamples, 0.0f);
            engine.processBlock(tl.data(), tr.data(), tailSamples);

            // Measure RMS over the final 0.5s window (2.5s..3.0s)
            std::vector<float> window(tl.end() - static_cast<int>(kHostSr * 0.5), tl.end());
            return computeRms(window);
        };

        const float rms2s  = measureTailRmsAt3Sec(2.0f);
        const float rms15s = measureTailRmsAt3Sec(15.0f);
        const float rms50s = measureTailRmsAt3Sec(50.0f);
        const float rms99s = measureTailRmsAt3Sec(99.0f);

        std::cout << "\n[TEST 8] Reverb RT60 Progression up to 99s (Finding #9):\n"
                  << "  Tail RMS @ 3.0s -> 2s: " << rms2s
                  << " | 15s: " << rms15s
                  << " | 50s: " << rms50s
                  << " | 99s: " << rms99s << "\n";
        if (rms15s > rms2s * 2.0f && rms50s > rms15s * 1.15f && rms99s > rms50s * 1.04f) {
            std::cout << "  -> PASS\n";
            ++passed;
        } else {
            std::cout << "  -> FAIL\n";
            ++failed;
        }
    }

    // 9. ADR / Noise Gate Negative (Look-Ahead) vs Positive Trigger Delay (Finding #16)
    {
        // Param layout (#18): [0] Trg Level, [1] Trg Delay, [2] Attack, [3] Hold, [4] Release, [5] MIDI Trg
        // A 30 ms burst: with negative Trg Delay (look-ahead) the gate is open before the audio arrives,
        // so the whole burst passes; with positive Trg Delay the gate opens 20 ms late and cuts the head.
        auto gatedBurstRms = [&](float trgDelayMs) {
            engine.reset();
            auto gate = aero::Spx90PresetDatabase::getRomPreset(17); // #18 ADR / NOISE GATE
            gate.params = {40.0f, trgDelayMs, 5.0f, 300.0f, 250.0f, 0.0f, 0.0f, 0.0f};
            gate.balance = 100.0f;
            engine.setHardwareConfig(aero::BitDepthMode::Linear16Bit, aero::RamModel::Spx90_MkI,
                                     31250.0f, 0.0f, -120.0f, false, false, false, 1.0f);
            engine.setActiveProgram(gate);

            const int total = static_cast<int>(kHostSr * 0.25);
            const int onset = static_cast<int>(kHostSr * 0.05);
            const int burst = static_cast<int>(kHostSr * 0.03);
            std::vector<float> l(total, 0.0f), r(total, 0.0f);
            for (int i = onset; i < onset + burst; ++i) {
                float t = static_cast<float>(i - onset) / static_cast<float>(kHostSr);
                l[i] = r[i] = 0.6f * std::sin(2.0f * 3.14159265f * 500.0f * t);
            }
            engine.processBlock(l.data(), r.data(), total);
            return computeRms(l);
        };

        const float rmsNeg = gatedBurstRms(-20.0f);
        const float rmsPos = gatedBurstRms(+20.0f);
        std::cout << "\n[TEST 9] ADR / Noise Gate Trigger Delay Polarity (Finding #16):\n"
                  << "  Trg Delay = -20 ms gated burst RMS: " << rmsNeg << "\n"
                  << "  Trg Delay = +20 ms gated burst RMS: " << rmsPos << "\n";
        if (rmsNeg > rmsPos * 1.5f && rmsNeg > 0.01f) {
            std::cout << "  -> PASS\n";
            ++passed;
        } else {
            std::cout << "  -> FAIL\n";
            ++failed;
        }
    }

    // 10. Multi-Chain Serial Routing (MOD -> PITCH -> DELAY -> REVERB)
    {
        engine.reset();
        auto sym = aero::Spx90PresetDatabase::getRomPreset(14); // #15 SYMPHONIC
        sym.balance = 100.0f;
        engine.setActiveProgram(sym);

        aero::MultiChainConfig mc;
        mc.enabled = true;
        mc.enableMod = true;
        mc.enablePitch = true;
        mc.enableDelay = true;
        mc.enableReverb = true;
        engine.setMultiChainConfig(mc);

        std::vector<float> l(kBlockSize, 0.0f), r(kBlockSize, 0.0f);
        for (int i = 0; i < kBlockSize / 2; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(kHostSr);
            l[i] = r[i] = 0.5f * std::sin(2.0f * 3.14159265f * 330.0f * t);
        }
        engine.processBlock(l.data(), r.data(), kBlockSize);
        bool ok = !hasInvalidSample(l) && !hasInvalidSample(r) && computeRms(l) > 0.02f;
        std::cout << "\n[TEST 10] Modern Multi-Chain Mode (MOD+PITCH+DELAY+REV): RMS L="
                  << computeRms(l) << (ok ? " -> PASS\n" : " -> FAIL\n");
        if (ok) ++passed;
        else ++failed;
    }

    std::cout << "\n=========================================================\n";
    std::cout << " Summary: " << passed << " Passed, " << failed << " Failed\n";
    std::cout << "=========================================================\n";

    return (failed == 0) ? 0 : 1;
}
