#pragma once

#include "Spx90Types.h"
#include "ConverterEngine.h"
#include "ModDelayAlgorithms.h"
#include "EqPanFreezeAlgorithms.h"
#include "ReverbAlgorithms.h"
#include "PitchAlgorithms.h"
#include <atomic>
#include <array>

namespace aero {

struct MultiChainConfig {
    bool enabled = false;
    bool enableMod = true;     // Runs Symphonic (#15)
    bool enablePitch = false;  // Runs Pitch C (#23)
    bool enableDelay = false;  // Runs Delay L,R (#7)
    bool enableReverb = true;  // Runs Rev 1 Hall (#1)
};

class Spx90DspEngine {
public:
    void prepare(double hostSampleRate);
    void reset();

    void setHardwareConfig(BitDepthMode bitMode,
                           RamModel ramModel,
                           float clockRateHz,
                           float converterDriveDb,
                           float noiseFloorDb,
                           bool hiFiBypass,
                           bool monoInputMode,
                           bool hardwareBypass,
                           float inputGainLinear);

    void setMultiChainConfig(const MultiChainConfig& cfg);
    void setActiveProgram(const Spx90Program& program);
    void handleMidiNoteOn(int midiNote, int velocity);

    void processBlock(float* leftChannel, float* rightChannel, int numSamples);

    float getPeakInputLevel() const { return peakInputLevel_.load(std::memory_order_relaxed); }
    bool getOverflowLed() const { return overflowLed_.load(std::memory_order_relaxed); }

private:
    void processInternalDspTick(float& l, float& r, float internalSr);

    ConverterEngine converter_;
    ModDelayEngine modDelayEngine_;
    EqPanFreezeEngine eqPanFreezeEngine_;
    ReverbEngine reverbEngine_;
    PitchChangeEngine pitchEngine_;

    Spx90Program currentProgram_{};
    Spx90Program pendingProgram_{};
    bool hasInitializedProgram_ = false;
    bool hasPendingProgramSwitch_ = false;
    float recallWetEnv_ = 1.0f;
    MultiChainConfig multiChain_{};

    BitDepthMode bitMode_ = BitDepthMode::Linear16Bit;
    RamModel ramModel_ = RamModel::Spx90_MkI;
    bool monoInputMode_ = false;
    bool hardwareBypass_ = false;
    float inputGainLinear_ = 1.0f;

    float smoothInputGain_ = 1.0f;
    float smoothWetMix_ = 0.5f;
    float smoothOutGain_ = 1.0f;
    float smoothBypassEnv_ = 1.0f;
    float smoothMonoMix_ = 0.0f;

    double hostSampleRate_ = 48000.0;
    float smoothCoeff_ = 0.004f;
    float fadeStep_ = 0.0032f;

    std::atomic<float> peakInputLevel_{0.0f};
    std::atomic<bool> overflowLed_{false};
    int overflowHoldSamples_ = 0;
};

} // namespace aero
