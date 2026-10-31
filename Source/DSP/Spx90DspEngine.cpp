#include "Spx90DspEngine.h"
#include "Spx90PresetDatabase.h"
#include <algorithm>
#include <cmath>

namespace aero {

void Spx90DspEngine::prepare(double hostSampleRate) {
    converter_.prepare(hostSampleRate);
    currentProgram_ = Spx90PresetDatabase::getRomPreset(14); // Default #15 SYMPHONIC
    hasInitializedProgram_ = false;
    hasPendingProgramSwitch_ = false;
    recallWetEnv_ = 1.0f;
    reset();
}

void Spx90DspEngine::reset() {
    converter_.reset();
    modDelayEngine_.reset();
    eqPanFreezeEngine_.reset();
    reverbEngine_.reset();
    pitchEngine_.reset();
    hasInitializedProgram_ = false;
    hasPendingProgramSwitch_ = false;
    recallWetEnv_ = 1.0f;
    smoothInputGain_ = inputGainLinear_;
    smoothWetMix_ = std::clamp(currentProgram_.balance, 0.0f, 100.0f) * 0.01f;
    smoothOutGain_ = std::clamp(currentProgram_.outLevel, 0.0f, 100.0f) * 0.01f;
    peakInputLevel_.store(0.0f, std::memory_order_relaxed);
    overflowLed_.store(false, std::memory_order_relaxed);
    overflowHoldSamples_ = 0;
}

void Spx90DspEngine::setHardwareConfig(BitDepthMode bitMode,
                                      RamModel ramModel,
                                      float clockRateHz,
                                      float converterDriveDb,
                                      float noiseFloorDb,
                                      bool hiFiBypass,
                                      bool monoInputMode,
                                      bool hardwareBypass,
                                      float inputGainLinear) {
    bitMode_ = bitMode;
    ramModel_ = ramModel;
    monoInputMode_ = monoInputMode;
    hardwareBypass_ = hardwareBypass;
    inputGainLinear_ = std::clamp(inputGainLinear, 0.0f, 4.0f);

    converter_.setParameters(bitMode, clockRateHz, converterDriveDb, noiseFloorDb, hiFiBypass);
    modDelayEngine_.setConfig(bitMode, ramModel);
    eqPanFreezeEngine_.setConfig(bitMode, ramModel);
    reverbEngine_.setConfig(bitMode, ramModel);
    pitchEngine_.setConfig(bitMode, ramModel);
}

void Spx90DspEngine::setMultiChainConfig(const MultiChainConfig& cfg) {
    multiChain_ = cfg;
}

void Spx90DspEngine::setActiveProgram(const Spx90Program& program) {
    if (!hasInitializedProgram_) {
        currentProgram_ = program;
        smoothWetMix_ = std::clamp(currentProgram_.balance, 0.0f, 100.0f) * 0.01f;
        smoothOutGain_ = std::clamp(currentProgram_.outLevel, 0.0f, 100.0f) * 0.01f;
        hasInitializedProgram_ = true;
        hasPendingProgramSwitch_ = false;
        recallWetEnv_ = 1.0f;
        return;
    }

    // If switching to a different memory slot or algorithm while running, trigger click-free VCA duck & tank clear
    if (program.algorithm != currentProgram_.algorithm ||
        program.memoryNumber != currentProgram_.memoryNumber) {
        pendingProgram_ = program;
        hasPendingProgramSwitch_ = true;
    } else {
        currentProgram_ = program;
        if (hasPendingProgramSwitch_) {
            pendingProgram_ = program;
        }
    }
}

void Spx90DspEngine::handleMidiNoteOn(int midiNote, int velocity) {
    eqPanFreezeEngine_.triggerMidiNote(midiNote, velocity);
    pitchEngine_.setMidiNote(midiNote, velocity);
}

void Spx90DspEngine::processInternalDspTick(float& l, float& r, float internalSr) {
    if (multiChain_.enabled) {
        // Modern Multi-Chain Mode: runs selected blocks in series using current program's parameters
        // or curated SPX90 defaults when current program is a different category
        const auto& activeAlg = currentProgram_.algorithm;

        if (multiChain_.enableMod) {
            bool isModAlg = (activeAlg == AlgorithmType::Symphonic ||
                             activeAlg == AlgorithmType::ChorusA ||
                             activeAlg == AlgorithmType::ChorusB ||
                             activeAlg == AlgorithmType::StereoFlangeA ||
                             activeAlg == AlgorithmType::StereoFlangeB ||
                             activeAlg == AlgorithmType::StereoPhasing ||
                             activeAlg == AlgorithmType::Tremolo);
            AlgorithmType modAlg = isModAlg ? activeAlg : AlgorithmType::Symphonic;
            const auto& modParams = isModAlg
                ? currentProgram_.params
                : Spx90PresetDatabase::getRomPreset(14).params; // #15 SYMPHONIC
            float wl = l, wr = r;
            modDelayEngine_.processTick(modAlg, modParams, wl, wr, internalSr);
            l = 0.5f * (l + wl);
            r = 0.5f * (r + wr);
        }

        if (multiChain_.enablePitch) {
            bool isPitchAlg = (activeAlg == AlgorithmType::PitchChangeA ||
                               activeAlg == AlgorithmType::PitchChangeB ||
                               activeAlg == AlgorithmType::PitchChangeC ||
                               activeAlg == AlgorithmType::PitchChangeD);
            AlgorithmType pitchAlg = isPitchAlg ? activeAlg : AlgorithmType::PitchChangeC;
            const auto& pitchParams = isPitchAlg
                ? currentProgram_.params
                : Spx90PresetDatabase::getRomPreset(22).params; // #23 PITCH CHANGE C
            float wl = l, wr = r;
            pitchEngine_.processTick(pitchAlg, pitchParams, wl, wr, internalSr);
            l = 0.55f * l + 0.45f * wl;
            r = 0.55f * r + 0.45f * wr;
        }

        if (multiChain_.enableDelay) {
            bool isDelayAlg = (activeAlg == AlgorithmType::DelayLR ||
                               activeAlg == AlgorithmType::StereoEcho);
            AlgorithmType dlyAlg = isDelayAlg ? activeAlg : AlgorithmType::StereoEcho;
            const auto& dlyParams = isDelayAlg
                ? currentProgram_.params
                : Spx90PresetDatabase::getRomPreset(7).params; // #8 STEREO ECHO
            float wl = l, wr = r;
            modDelayEngine_.processTick(dlyAlg, dlyParams, wl, wr, internalSr);
            l = 0.65f * l + 0.35f * wl;
            r = 0.65f * r + 0.35f * wr;
        }

        if (multiChain_.enableReverb) {
            bool isRevAlg = (activeAlg == AlgorithmType::RevHall ||
                             activeAlg == AlgorithmType::RevRoom ||
                             activeAlg == AlgorithmType::RevVocal ||
                             activeAlg == AlgorithmType::RevPlate ||
                             activeAlg == AlgorithmType::EarlyRef1 ||
                             activeAlg == AlgorithmType::EarlyRef2 ||
                             activeAlg == AlgorithmType::GateReverb ||
                             activeAlg == AlgorithmType::ReverseGate ||
                             activeAlg == AlgorithmType::ReverbAndGate);
            AlgorithmType revAlg = isRevAlg ? activeAlg : AlgorithmType::RevHall;
            const auto& revParams = isRevAlg
                ? currentProgram_.params
                : Spx90PresetDatabase::getRomPreset(0).params; // #1 REV 1 HALL
            float wl = l, wr = r;
            reverbEngine_.processTick(revAlg, revParams, wl, wr, internalSr);
            l = 0.60f * l + 0.40f * wl;
            r = 0.60f * r + 0.40f * wr;
        }
        return;
    }

    // Authentic Single-Program SPX90 Routing
    const AlgorithmType alg = currentProgram_.algorithm;
    const auto& params = currentProgram_.params;

    switch (alg) {
        case AlgorithmType::RevHall:
        case AlgorithmType::RevRoom:
        case AlgorithmType::RevVocal:
        case AlgorithmType::RevPlate:
        case AlgorithmType::EarlyRef1:
        case AlgorithmType::EarlyRef2:
        case AlgorithmType::GateReverb:
        case AlgorithmType::ReverseGate:
        case AlgorithmType::ReverbAndGate:
            reverbEngine_.processTick(alg, params, l, r, internalSr);
            break;

        case AlgorithmType::DelayLR:
        case AlgorithmType::StereoEcho:
        case AlgorithmType::StereoFlangeA:
        case AlgorithmType::StereoFlangeB:
        case AlgorithmType::ChorusA:
        case AlgorithmType::ChorusB:
        case AlgorithmType::StereoPhasing:
        case AlgorithmType::Tremolo:
        case AlgorithmType::Symphonic:
        case AlgorithmType::DelayVibrato:
            modDelayEngine_.processTick(alg, params, l, r, internalSr);
            break;

        case AlgorithmType::AdrNoiseGate:
        case AlgorithmType::Compressor:
        case AlgorithmType::FreezeA:
        case AlgorithmType::FreezeB:
        case AlgorithmType::AutoPan:
        case AlgorithmType::TriggeredPan:
        case AlgorithmType::ParametricEQ:
            eqPanFreezeEngine_.processTick(alg, params, l, r, internalSr);
            break;

        case AlgorithmType::PitchChangeA:
        case AlgorithmType::PitchChangeB:
        case AlgorithmType::PitchChangeC:
        case AlgorithmType::PitchChangeD:
            pitchEngine_.processTick(alg, params, l, r, internalSr);
            break;

        default:
            break;
    }
}

void Spx90DspEngine::processBlock(float* leftChannel, float* rightChannel, int numSamples) {
    if (!leftChannel || numSamples <= 0) return;

    float blockPeak = 0.0f;
    const float targetWetMix  = std::clamp(currentProgram_.balance, 0.0f, 100.0f) * 0.01f;
    const float targetOutGain = std::clamp(currentProgram_.outLevel, 0.0f, 100.0f) * 0.01f;
    const float internalSr = static_cast<float>(converter_.getInternalClockHz());
    constexpr float kSmoothCoeff = 0.004f; // ~5.5 ms zipper-free smoothing at 44.1/48 kHz
    constexpr float kFadeStep = 0.0032f;   // ~7 ms smooth VCA duck & recovery on preset recall

    for (int i = 0; i < numSamples; ++i) {
        smoothInputGain_ += kSmoothCoeff * (inputGainLinear_ - smoothInputGain_);
        smoothWetMix_    += kSmoothCoeff * (targetWetMix - smoothWetMix_);
        smoothOutGain_   += kSmoothCoeff * (targetOutGain - smoothOutGain_);

        // Handle click-free SPX90 program switch VCA ducking & DRAM clear
        if (hasPendingProgramSwitch_) {
            recallWetEnv_ -= kFadeStep;
            if (recallWetEnv_ <= 0.0f) {
                recallWetEnv_ = 0.0f;
                modDelayEngine_.reset();
                eqPanFreezeEngine_.reset();
                reverbEngine_.reset();
                pitchEngine_.reset();
                currentProgram_ = pendingProgram_;
                hasPendingProgramSwitch_ = false;
            }
        } else if (recallWetEnv_ < 1.0f) {
            recallWetEnv_ = std::min(1.0f, recallWetEnv_ + kFadeStep);
        }

        float inL = leftChannel[i] * smoothInputGain_;
        float inR = (rightChannel ? rightChannel[i] : leftChannel[i]) * smoothInputGain_;

        if (monoInputMode_) {
            float m = 0.5f * (inL + inR);
            inL = inR = m;
        }

        float absMax = std::max(std::abs(inL), std::abs(inR));
        if (absMax > blockPeak) blockPeak = absMax;

        if (hardwareBypass_) {
            leftChannel[i] = inL;
            if (rightChannel) rightChannel[i] = inR;
            continue;
        }

        float dryL = inL;
        float dryR = inR;
        float wetL = inL;
        float wetR = inR;

        converter_.processSampleFrame(wetL, wetR, [this, internalSr](float& dl, float& dr) {
            processInternalDspTick(dl, dr, internalSr);
        });

        wetL *= recallWetEnv_;
        wetR *= recallWetEnv_;

        const float dryMix = 1.0f - smoothWetMix_;

        // SPX90 Output Mixer: parallel analog dry + reconstructed DAC wet * outLevel
        float finalL = (dryL * dryMix + wetL * smoothWetMix_) * smoothOutGain_;
        float finalR = (dryR * dryMix + wetR * smoothWetMix_) * smoothOutGain_;

        leftChannel[i] = finalL;
        if (rightChannel) rightChannel[i] = finalR;
    }

    // Smooth peak meter decay for the front-panel 7-LED bargraph
    float prevPeak = peakInputLevel_.load(std::memory_order_relaxed);
    float newPeak = (blockPeak > prevPeak) ? blockPeak : (prevPeak * 0.88f);
    peakInputLevel_.store(newPeak, std::memory_order_relaxed);

    if (converter_.consumeOverflowFlag() || blockPeak > 0.95f) {
        overflowHoldSamples_ = 8000;
        overflowLed_.store(true, std::memory_order_relaxed);
    } else if (overflowHoldSamples_ > 0) {
        overflowHoldSamples_ -= numSamples;
        if (overflowHoldSamples_ <= 0) {
            overflowLed_.store(false, std::memory_order_relaxed);
        }
    }
}

} // namespace aero
