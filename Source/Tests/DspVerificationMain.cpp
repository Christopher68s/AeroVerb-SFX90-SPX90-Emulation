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

    std::cout << "\n=========================================================\n";
    std::cout << " Summary: " << passed << " Passed, " << failed << " Failed\n";
    std::cout << "=========================================================\n";

    return (failed == 0) ? 0 : 1;
}
