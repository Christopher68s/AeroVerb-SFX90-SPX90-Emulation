#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

AeroFX90AudioProcessor::AeroFX90AudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "AeroFX90State", createParameterLayout()),
      memoryBank_(aero::Spx90PresetDatabase::createDefaultMemoryBank()) {
    rawInputLevel_     = apvts.getRawParameterValue("inputLevel");
    rawBalance_        = apvts.getRawParameterValue("balance");
    rawOutLevel_       = apvts.getRawParameterValue("outLevel");
    rawBypass_         = apvts.getRawParameterValue("bypass");
    rawMonoInput_      = apvts.getRawParameterValue("monoInput");
    rawBitMode_        = apvts.getRawParameterValue("bitMode");
    rawRamModel_       = apvts.getRawParameterValue("ramModel");
    rawClockRate_      = apvts.getRawParameterValue("clockRate");
    rawConverterDrive_ = apvts.getRawParameterValue("converterDrive");
    rawNoiseFloor_     = apvts.getRawParameterValue("noiseFloor");
    rawHiFiBypass_     = apvts.getRawParameterValue("hiFiBypass");
    rawMultiChain_     = apvts.getRawParameterValue("multiChain");
    rawChainMod_       = apvts.getRawParameterValue("chainMod");
    rawChainPitch_     = apvts.getRawParameterValue("chainPitch");
    rawChainDelay_     = apvts.getRawParameterValue("chainDelay");
    rawChainReverb_    = apvts.getRawParameterValue("chainReverb");

    balanceObj_   = apvts.getParameter("balance");
    outLevelObj_  = apvts.getParameter("outLevel");
    bypassObj_    = apvts.getParameter("bypass");
    bitModeObj_   = apvts.getParameter("bitMode");
    ramModelObj_  = apvts.getParameter("ramModel");
    clockRateObj_ = apvts.getParameter("clockRate");

    for (int i = 0; i < aero::kMaxProgramParams; ++i) {
        const juce::String id = "param" + juce::String(i + 1);
        rawParams_[static_cast<size_t>(i)] = apvts.getRawParameterValue(id);
        paramObjs_[static_cast<size_t>(i)] = apvts.getParameter(id);
    }

    activeSlotIndex_.store(14, std::memory_order_relaxed); // #15 SYMPHONIC by default
    previewSlotIndex_.store(14, std::memory_order_relaxed);
    workingProgram_ = memoryBank_[14];
    cachedAudioProgram_ = workingProgram_;
    currentTailSeconds_.store(computeTailLengthForProgram(workingProgram_), std::memory_order_relaxed);
    pushWorkingParamsToApvts();
}

AeroFX90AudioProcessor::~AeroFX90AudioProcessor() {
    cancelPendingUpdate();
}

juce::AudioProcessorValueTreeState::ParameterLayout AeroFX90AudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"inputLevel", 1}, "Input Level",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f), 7.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"balance", 1}, "Mix Balance",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"outLevel", 1}, "Output Level",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f));

    for (int i = 0; i < aero::kMaxProgramParams; ++i) {
        juce::String id = "param" + juce::String(i + 1);
        juce::String name = "Program Param " + juce::String(i + 1);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id, 1}, name,
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 0.5f));
    }

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"bypass", 1}, "Hardware Bypass", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"monoInput", 1}, "Mono Input Mode", false));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"bitMode", 1}, "Bit Depth Mode",
        juce::StringArray{"12-Bit Linear", "12-Bit Companded", "16-Bit Linear PCM"}, 2));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"ramModel", 1}, "DRAM Memory Model",
        juce::StringArray{"SPX90 (1985 500ms)", "SPX90II (1986 2000ms)"}, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"clockRate", 1}, "DSP Clock Rate (Hz)",
        juce::NormalisableRange<float>(15000.0f, 48000.0f, 10.0f), 31250.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"converterDrive", 1}, "Converter Drive (dB)",
        juce::NormalisableRange<float>(-12.0f, 18.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"noiseFloor", 1}, "Noise Floor (dB)",
        juce::NormalisableRange<float>(-120.0f, -50.0f, 0.5f), -82.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"hiFiBypass", 1}, "Hi-Fi Filter Bypass", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"multiChain", 1}, "Modern Multi-Chain Mode", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"chainMod", 1}, "Chain: Modulation", true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"chainPitch", 1}, "Chain: Pitch Change", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"chainDelay", 1}, "Chain: Stereo Echo", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"chainReverb", 1}, "Chain: Reverb", true));

    return {params.begin(), params.end()};
}

const juce::String AeroFX90AudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool AeroFX90AudioProcessor::acceptsMidi() const { return true; }
bool AeroFX90AudioProcessor::producesMidi() const { return false; }
bool AeroFX90AudioProcessor::isMidiEffect() const { return false; }
double AeroFX90AudioProcessor::computeTailLengthForProgram(const aero::Spx90Program& prog) {
    switch (prog.algorithm) {
        case aero::AlgorithmType::RevHall:
        case aero::AlgorithmType::RevRoom:
        case aero::AlgorithmType::RevVocal:
        case aero::AlgorithmType::RevPlate:
        case aero::AlgorithmType::GateReverb:
        case aero::AlgorithmType::ReverseGate:
        case aero::AlgorithmType::ReverbAndGate:
            return std::clamp(static_cast<double>(prog.params[0]) + 1.0, 2.0, 100.0);
        case aero::AlgorithmType::DelayLR:
        case aero::AlgorithmType::StereoEcho:
            return 10.0;
        default:
            return 5.0;
    }
}

double AeroFX90AudioProcessor::getTailLengthSeconds() const {
    return currentTailSeconds_.load(std::memory_order_relaxed);
}

int AeroFX90AudioProcessor::getNumPrograms() {
    return aero::kTotalMemorySlots;
}

int AeroFX90AudioProcessor::getCurrentProgram() {
    return activeSlotIndex_.load(std::memory_order_relaxed);
}

void AeroFX90AudioProcessor::setCurrentProgram(int index) {
    recallSlotDirectly(index);
}

void AeroFX90AudioProcessor::handleAsyncUpdate() {
    const int pgm = pendingMidiProgramChange_.exchange(-1, std::memory_order_acq_rel);
    if (pgm >= 0 && pgm < aero::kTotalMemorySlots) {
        recallSlotDirectly(pgm);
    }
}

const juce::String AeroFX90AudioProcessor::getProgramName(int index) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    int clamped = std::clamp(index, 0, aero::kTotalMemorySlots - 1);
    const auto& p = memoryBank_[static_cast<size_t>(clamped)];
    char buf[48]{};
    std::snprintf(buf, sizeof(buf), "%02d %s", p.memoryNumber, p.name.c_str());
    return juce::String(buf);
}

void AeroFX90AudioProcessor::changeProgramName(int index, const juce::String& newName) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (index >= aero::kNumRomPresets && index < aero::kTotalMemorySlots) {
        memoryBank_[static_cast<size_t>(index)].name =
            newName.toUpperCase().substring(0, 15).toStdString();
    }
}

void AeroFX90AudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    dspEngine_.prepare(sampleRate);
    syncDspFromState();
}

void AeroFX90AudioProcessor::releaseResources() {}

bool AeroFX90AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& outSet = layouts.getMainOutputChannelSet();
    const auto& inSet  = layouts.getMainInputChannelSet();
    if (outSet != juce::AudioChannelSet::stereo() && outSet != juce::AudioChannelSet::mono())
        return false;
    if (inSet != juce::AudioChannelSet::stereo() && inSet != juce::AudioChannelSet::mono())
        return false;
    return true;
}

void AeroFX90AudioProcessor::pushWorkingParamsToApvts() {
    struct ScopedPushGuard {
        std::atomic<int>& counter;
        explicit ScopedPushGuard(std::atomic<int>& c) : counter(c) {
            counter.fetch_add(1, std::memory_order_acq_rel);
        }
        ~ScopedPushGuard() {
            counter.fetch_sub(1, std::memory_order_acq_rel);
        }
    } guard(pushingToApvtsCount_);

    std::array<float, aero::kMaxProgramParams> normVals{};
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        const auto& metas = aero::Spx90PresetDatabase::getAlgorithmParamMeta(workingProgram_.algorithm);
        for (int i = 0; i < aero::kMaxProgramParams; ++i) {
            const auto& m = metas[static_cast<size_t>(i)];
            float span = std::max(1e-5f, m.maxVal - m.minVal);
            float norm = m.active
                ? std::clamp((workingProgram_.params[static_cast<size_t>(i)] - m.minVal) / span, 0.0f, 1.0f)
                : 0.0f;
            lastPushedNormParams_[static_cast<size_t>(i)] = norm;
            normVals[static_cast<size_t>(i)] = norm;
        }
        cachedAudioProgram_ = workingProgram_;
        currentTailSeconds_.store(computeTailLengthForProgram(workingProgram_), std::memory_order_relaxed);
    }
    for (int i = 0; i < aero::kMaxProgramParams; ++i) {
        if (auto* p = paramObjs_[static_cast<size_t>(i)]) {
            p->setValueNotifyingHost(normVals[static_cast<size_t>(i)]);
            if (rawParams_[static_cast<size_t>(i)]) {
                lastPushedNormParams_[static_cast<size_t>(i)] =
                    rawParams_[static_cast<size_t>(i)]->load(std::memory_order_relaxed);
            }
        }
    }
}

void AeroFX90AudioProcessor::syncDspFromState() {
    const float inKnob = rawInputLevel_ ? rawInputLevel_->load(std::memory_order_relaxed) : 7.5f;
    const float inLinear = (inKnob / 7.5f); // 7.5 = unity (0 dB), 10.0 = +2.5 dB boost

    const int bitIdx    = rawBitMode_        ? static_cast<int>(rawBitMode_->load(std::memory_order_relaxed)) : 2;
    const int ramIdx    = rawRamModel_       ? static_cast<int>(rawRamModel_->load(std::memory_order_relaxed)) : 0;
    const float clockHz = rawClockRate_      ? rawClockRate_->load(std::memory_order_relaxed) : 31250.0f;
    const float driveDb = rawConverterDrive_ ? rawConverterDrive_->load(std::memory_order_relaxed) : 0.0f;
    const float noiseDb = rawNoiseFloor_     ? rawNoiseFloor_->load(std::memory_order_relaxed) : -82.0f;
    const bool hiFi     = rawHiFiBypass_     && rawHiFiBypass_->load(std::memory_order_relaxed) >= 0.5f;
    const bool monoIn   = rawMonoInput_      && rawMonoInput_->load(std::memory_order_relaxed) >= 0.5f;
    const bool bypass   = rawBypass_         && rawBypass_->load(std::memory_order_relaxed) >= 0.5f;

    dspEngine_.setHardwareConfig(
        static_cast<aero::BitDepthMode>(std::clamp(bitIdx, 0, 2)),
        static_cast<aero::RamModel>(std::clamp(ramIdx, 0, 1)),
        clockHz, driveDb, noiseDb, hiFi, monoIn, bypass, inLinear);

    aero::MultiChainConfig mc;
    mc.enabled      = rawMultiChain_  && rawMultiChain_->load(std::memory_order_relaxed) >= 0.5f;
    mc.enableMod    = rawChainMod_    && rawChainMod_->load(std::memory_order_relaxed) >= 0.5f;
    mc.enablePitch  = rawChainPitch_  && rawChainPitch_->load(std::memory_order_relaxed) >= 0.5f;
    mc.enableDelay  = rawChainDelay_  && rawChainDelay_->load(std::memory_order_relaxed) >= 0.5f;
    mc.enableReverb = rawChainReverb_ && rawChainReverb_->load(std::memory_order_relaxed) >= 0.5f;
    dspEngine_.setMultiChainConfig(mc);

    float rawNorm[8]{};
    for (int i = 0; i < aero::kMaxProgramParams; ++i) {
        rawNorm[i] = rawParams_[static_cast<size_t>(i)]
            ? rawParams_[static_cast<size_t>(i)]->load(std::memory_order_relaxed)
            : 0.5f;
    }

    const float curBal = rawBalance_  ? rawBalance_->load(std::memory_order_relaxed)  : 50.0f;
    const float curOut = rawOutLevel_ ? rawOutLevel_->load(std::memory_order_relaxed) : 100.0f;
    const bool isPushing = pushingToApvtsCount_.load(std::memory_order_acquire) > 0;

    std::unique_lock<std::mutex> lock(stateMutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        if (!isPushing) {
            const auto& metas = aero::Spx90PresetDatabase::getAlgorithmParamMeta(workingProgram_.algorithm);
            for (int i = 0; i < aero::kMaxProgramParams; ++i) {
                const auto& m = metas[static_cast<size_t>(i)];
                if (m.active && std::abs(rawNorm[i] - lastPushedNormParams_[static_cast<size_t>(i)]) > 2e-4f) {
                    lastPushedNormParams_[static_cast<size_t>(i)] = rawNorm[i];
                    float realVal = m.minVal + rawNorm[i] * (m.maxVal - m.minVal);
                    if (m.step > 0.0f) {
                        realVal = std::round((realVal - m.minVal) / m.step) * m.step + m.minVal;
                    }
                    workingProgram_.params[static_cast<size_t>(i)] = std::clamp(realVal, m.minVal, m.maxVal);
                    currentParamCursor_.store(i, std::memory_order_relaxed);
                    programModified_.store(true, std::memory_order_relaxed);
                }
            }
            workingProgram_.balance  = curBal;
            workingProgram_.outLevel = curOut;
        }
        cachedAudioProgram_ = workingProgram_;
        currentTailSeconds_.store(computeTailLengthForProgram(workingProgram_), std::memory_order_relaxed);
    } else {
        cachedAudioProgram_.balance  = curBal;
        cachedAudioProgram_.outLevel = curOut;
    }
    dspEngine_.setActiveProgram(cachedAudioProgram_);
}

void AeroFX90AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    for (const auto metadata : midiMessages) {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn()) {
            lastMidiNoteReceived_.store(msg.getNoteNumber(), std::memory_order_relaxed);
            midiIndicatorCounter_.store(12, std::memory_order_relaxed);
            dspEngine_.handleMidiNoteOn(msg.getNoteNumber(), msg.getVelocity());
        } else if (msg.isProgramChange()) {
            int pgm = msg.getProgramChangeNumber();
            if (pgm >= 0 && pgm < aero::kTotalMemorySlots) {
                midiIndicatorCounter_.store(12, std::memory_order_relaxed);
                pendingMidiProgramChange_.store(pgm, std::memory_order_release);
                std::unique_lock<std::mutex> lock(stateMutex_, std::try_to_lock);
                if (lock.owns_lock()) {
                    activeSlotIndex_.store(pgm, std::memory_order_relaxed);
                    previewSlotIndex_.store(pgm, std::memory_order_relaxed);
                    workingProgram_ = memoryBank_[static_cast<size_t>(pgm)];
                    cachedAudioProgram_ = workingProgram_;
                    currentParamCursor_.store(0, std::memory_order_relaxed);
                    programModified_.store(false, std::memory_order_relaxed);
                    panelMode_.store(PanelDisplayMode::ProgramPlay, std::memory_order_relaxed);
                    currentTailSeconds_.store(computeTailLengthForProgram(workingProgram_), std::memory_order_relaxed);
                }
                triggerAsyncUpdate();
            }
        }
    }

    int curMidiCount = midiIndicatorCounter_.load(std::memory_order_relaxed);
    if (curMidiCount > 0) {
        midiIndicatorCounter_.store(curMidiCount - 1, std::memory_order_relaxed);
    }

    syncDspFromState();

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    if (totalNumInputChannels == 1 && totalNumOutputChannels >= 2) {
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
    }

    float* left  = buffer.getWritePointer(0);
    float* right = (totalNumOutputChannels >= 2) ? buffer.getWritePointer(1) : nullptr;

    dspEngine_.processBlock(left, right, numSamples);
}

// --- Front Panel Hardware Button Actions ---

void AeroFX90AudioProcessor::pressMemoryUp() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (panelMode_.load(std::memory_order_relaxed) == PanelDisplayMode::StoreTargetSelect) {
        int cur = storeTargetSlotIndex_.load(std::memory_order_relaxed);
        storeTargetSlotIndex_.store((cur + 1) % aero::kTotalMemorySlots, std::memory_order_relaxed);
        return;
    }
    int cur = previewSlotIndex_.load(std::memory_order_relaxed);
    previewSlotIndex_.store((cur + 1) % aero::kTotalMemorySlots, std::memory_order_relaxed);
    panelMode_.store(PanelDisplayMode::ProgramPlay, std::memory_order_relaxed);
}

void AeroFX90AudioProcessor::pressMemoryDown() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (panelMode_.load(std::memory_order_relaxed) == PanelDisplayMode::StoreTargetSelect) {
        int cur = storeTargetSlotIndex_.load(std::memory_order_relaxed);
        storeTargetSlotIndex_.store((cur + aero::kTotalMemorySlots - 1) % aero::kTotalMemorySlots, std::memory_order_relaxed);
        return;
    }
    int cur = previewSlotIndex_.load(std::memory_order_relaxed);
    previewSlotIndex_.store((cur + aero::kTotalMemorySlots - 1) % aero::kTotalMemorySlots, std::memory_order_relaxed);
    panelMode_.store(PanelDisplayMode::ProgramPlay, std::memory_order_relaxed);
}

void AeroFX90AudioProcessor::pressRecall() {
    recallSlotDirectly(previewSlotIndex_.load(std::memory_order_relaxed));
}

void AeroFX90AudioProcessor::recallSlotDirectly(int slotIndexZeroBased) {
    recallSlot(slotIndexZeroBased, true);
}

void AeroFX90AudioProcessor::recallSlot(int slotIndexZeroBased, bool applyProgramLevels) {
    float newBal = 50.0f;
    float newOut = 100.0f;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        int clamped = std::clamp(slotIndexZeroBased, 0, aero::kTotalMemorySlots - 1);
        activeSlotIndex_.store(clamped, std::memory_order_relaxed);
        previewSlotIndex_.store(clamped, std::memory_order_relaxed);
        workingProgram_ = memoryBank_[static_cast<size_t>(clamped)];
        currentParamCursor_.store(0, std::memory_order_relaxed);
        programModified_.store(false, std::memory_order_relaxed);
        panelMode_.store(PanelDisplayMode::ProgramPlay, std::memory_order_relaxed);
        if (!applyProgramLevels) {
            // Session restore: the host-restored APVTS values win over the slot's stored levels
            workingProgram_.balance  = rawBalance_  ? rawBalance_->load(std::memory_order_relaxed)  : 50.0f;
            workingProgram_.outLevel = rawOutLevel_ ? rawOutLevel_->load(std::memory_order_relaxed) : 100.0f;
        }
        newBal = workingProgram_.balance;
        newOut = workingProgram_.outLevel;
        cachedAudioProgram_ = workingProgram_;
        currentTailSeconds_.store(computeTailLengthForProgram(workingProgram_), std::memory_order_relaxed);
    }
    if (applyProgramLevels) {
        pushingToApvtsCount_.fetch_add(1, std::memory_order_acq_rel);
        if (balanceObj_)
            balanceObj_->setValueNotifyingHost(balanceObj_->convertTo0to1(newBal));
        if (outLevelObj_)
            outLevelObj_->setValueNotifyingHost(outLevelObj_->convertTo0to1(newOut));
        pushingToApvtsCount_.fetch_sub(1, std::memory_order_acq_rel);
    }
    pushWorkingParamsToApvts();
}

void AeroFX90AudioProcessor::pressStore() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (panelMode_.load(std::memory_order_relaxed) != PanelDisplayMode::StoreTargetSelect) {
        // First press enters Store Target Selection mode
        int active = activeSlotIndex_.load(std::memory_order_relaxed);
        storeTargetSlotIndex_.store((active >= aero::kNumRomPresets) ? active : 30, std::memory_order_relaxed);
        panelMode_.store(PanelDisplayMode::StoreTargetSelect, std::memory_order_relaxed);
        return;
    }
    // Second press attempts to write workingProgram_ into storeTargetSlotIndex_
    int target = storeTargetSlotIndex_.load(std::memory_order_relaxed);
    if (target < aero::kNumRomPresets) {
        // Authentic SPX90 error when trying to overwrite ROM presets #1~#30!
        panelMode_.store(PanelDisplayMode::StoreReadOnlyError, std::memory_order_relaxed);
        return;
    }
    aero::Spx90Program saved = workingProgram_;
    saved.memoryNumber = target + 1;
    saved.isRom = false;
    memoryBank_[static_cast<size_t>(target)] = saved;
    activeSlotIndex_.store(target, std::memory_order_relaxed);
    previewSlotIndex_.store(target, std::memory_order_relaxed);
    workingProgram_ = saved;
    cachedAudioProgram_ = saved;
    programModified_.store(false, std::memory_order_relaxed);
    panelMode_.store(PanelDisplayMode::StoreCompletedMsg, std::memory_order_relaxed);
}

void AeroFX90AudioProcessor::pressBypass() {
    if (bypassObj_ && rawBypass_) {
        bool cur = rawBypass_->load(std::memory_order_relaxed) >= 0.5f;
        bypassObj_->setValueNotifyingHost(cur ? 0.0f : 1.0f);
    }
}

void AeroFX90AudioProcessor::pressBalance() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto cur = panelMode_.load(std::memory_order_relaxed);
    panelMode_.store((cur == PanelDisplayMode::BalanceEdit)
        ? PanelDisplayMode::ProgramPlay
        : PanelDisplayMode::BalanceEdit, std::memory_order_relaxed);
}

void AeroFX90AudioProcessor::pressOutLevel() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto cur = panelMode_.load(std::memory_order_relaxed);
    panelMode_.store((cur == PanelDisplayMode::OutLevelEdit)
        ? PanelDisplayMode::ProgramPlay
        : PanelDisplayMode::OutLevelEdit, std::memory_order_relaxed);
}

int AeroFX90AudioProcessor::getActiveParamCount() const {
    const auto& metas = aero::Spx90PresetDatabase::getAlgorithmParamMeta(workingProgram_.algorithm);
    int count = 0;
    for (const auto& m : metas) {
        if (m.active) ++count;
    }
    return std::max(1, count);
}

void AeroFX90AudioProcessor::pressParam() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    int count = getActiveParamCount();
    if (panelMode_.load(std::memory_order_relaxed) != PanelDisplayMode::ParamSelect) {
        panelMode_.store(PanelDisplayMode::ParamSelect, std::memory_order_relaxed);
        int cur = currentParamCursor_.load(std::memory_order_relaxed);
        currentParamCursor_.store(std::clamp(cur, 0, count - 1), std::memory_order_relaxed);
    } else {
        int cur = currentParamCursor_.load(std::memory_order_relaxed);
        currentParamCursor_.store((cur + 1) % count, std::memory_order_relaxed);
    }
}

void AeroFX90AudioProcessor::pressUtility() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (panelMode_.load(std::memory_order_relaxed) != PanelDisplayMode::UtilityEdit) {
        panelMode_.store(PanelDisplayMode::UtilityEdit, std::memory_order_relaxed);
        currentUtilityCursor_.store(0, std::memory_order_relaxed);
    } else {
        int cur = currentUtilityCursor_.load(std::memory_order_relaxed);
        currentUtilityCursor_.store((cur + 1) % 3, std::memory_order_relaxed);
    }
}

void AeroFX90AudioProcessor::pressMidiTrigger() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    int note = lastMidiNoteReceived_.load(std::memory_order_relaxed);
    if (workingProgram_.algorithm == aero::AlgorithmType::FreezeA ||
        workingProgram_.algorithm == aero::AlgorithmType::FreezeB) {
        // Trigger manual sample capture
        workingProgram_.params[5] = 1.0f;
        dspEngine_.handleMidiNoteOn(note, 100);
        panelMode_.store(PanelDisplayMode::MidiEdit, std::memory_order_relaxed);
        return;
    }
    auto cur = panelMode_.load(std::memory_order_relaxed);
    panelMode_.store((cur == PanelDisplayMode::MidiEdit)
        ? PanelDisplayMode::ProgramPlay
        : PanelDisplayMode::MidiEdit, std::memory_order_relaxed);
    dspEngine_.handleMidiNoteOn(note, 100);
}

void AeroFX90AudioProcessor::pressValueUp(bool coarse) {
    float mult = coarse ? 5.0f : 1.0f;
    PanelDisplayMode mode;
    int paramIdx = 0;
    int utilIdx = 0;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        mode = panelMode_.load(std::memory_order_relaxed);
        paramIdx = currentParamCursor_.load(std::memory_order_relaxed);
        utilIdx = currentUtilityCursor_.load(std::memory_order_relaxed);
        if (mode == PanelDisplayMode::ProgramPlay) {
            mode = PanelDisplayMode::ParamSelect;
            panelMode_.store(mode, std::memory_order_relaxed);
        }
    }

    if (mode == PanelDisplayMode::BalanceEdit) {
        float cur = rawBalance_ ? rawBalance_->load(std::memory_order_relaxed) : 50.0f;
        float next = std::clamp(cur + 1.0f * mult, 0.0f, 100.0f);
        if (balanceObj_)
            balanceObj_->setValueNotifyingHost(balanceObj_->convertTo0to1(next));
        programModified_.store(true, std::memory_order_relaxed);
    } else if (mode == PanelDisplayMode::OutLevelEdit) {
        float cur = rawOutLevel_ ? rawOutLevel_->load(std::memory_order_relaxed) : 100.0f;
        float next = std::clamp(cur + 1.0f * mult, 0.0f, 100.0f);
        if (outLevelObj_)
            outLevelObj_->setValueNotifyingHost(outLevelObj_->convertTo0to1(next));
        programModified_.store(true, std::memory_order_relaxed);
    } else if (mode == PanelDisplayMode::UtilityEdit) {
        if (utilIdx == 0) {
            int cur = rawBitMode_ ? static_cast<int>(rawBitMode_->load(std::memory_order_relaxed)) : 2;
            int next = (cur + 1) % 3;
            if (bitModeObj_)
                bitModeObj_->setValueNotifyingHost(bitModeObj_->convertTo0to1(static_cast<float>(next)));
        } else if (utilIdx == 1) {
            int cur = rawRamModel_ ? static_cast<int>(rawRamModel_->load(std::memory_order_relaxed)) : 0;
            int next = 1 - cur;
            if (ramModelObj_)
                ramModelObj_->setValueNotifyingHost(ramModelObj_->convertTo0to1(static_cast<float>(next)));
        } else {
            float cur = rawClockRate_ ? rawClockRate_->load(std::memory_order_relaxed) : 31250.0f;
            float next = std::clamp(cur + 250.0f * mult, 15000.0f, 48000.0f);
            if (clockRateObj_)
                clockRateObj_->setValueNotifyingHost(clockRateObj_->convertTo0to1(next));
        }
    } else if (mode == PanelDisplayMode::StoreTargetSelect) {
        pressMemoryUp();
    } else if (mode == PanelDisplayMode::ParamSelect) {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            const auto& metas = aero::Spx90PresetDatabase::getAlgorithmParamMeta(workingProgram_.algorithm);
            const auto& m = metas[static_cast<size_t>(paramIdx)];
            if (m.active) {
                float cur = workingProgram_.params[static_cast<size_t>(paramIdx)];
                if (m.format == aero::ParamFormat::BaseKey && cur < 36.0f) cur = 59.0f;
                float next = std::clamp(cur + m.step * mult, m.minVal, m.maxVal);
                workingProgram_.params[static_cast<size_t>(paramIdx)] = next;
                programModified_.store(true, std::memory_order_relaxed);
            }
        }
        pushWorkingParamsToApvts();
    }
}

void AeroFX90AudioProcessor::pressValueDown(bool coarse) {
    float mult = coarse ? 5.0f : 1.0f;
    PanelDisplayMode mode;
    int paramIdx = 0;
    int utilIdx = 0;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        mode = panelMode_.load(std::memory_order_relaxed);
        paramIdx = currentParamCursor_.load(std::memory_order_relaxed);
        utilIdx = currentUtilityCursor_.load(std::memory_order_relaxed);
        if (mode == PanelDisplayMode::ProgramPlay) {
            mode = PanelDisplayMode::ParamSelect;
            panelMode_.store(mode, std::memory_order_relaxed);
        }
    }

    if (mode == PanelDisplayMode::BalanceEdit) {
        float cur = rawBalance_ ? rawBalance_->load(std::memory_order_relaxed) : 50.0f;
        float next = std::clamp(cur - 1.0f * mult, 0.0f, 100.0f);
        if (balanceObj_)
            balanceObj_->setValueNotifyingHost(balanceObj_->convertTo0to1(next));
        programModified_.store(true, std::memory_order_relaxed);
    } else if (mode == PanelDisplayMode::OutLevelEdit) {
        float cur = rawOutLevel_ ? rawOutLevel_->load(std::memory_order_relaxed) : 100.0f;
        float next = std::clamp(cur - 1.0f * mult, 0.0f, 100.0f);
        if (outLevelObj_)
            outLevelObj_->setValueNotifyingHost(outLevelObj_->convertTo0to1(next));
        programModified_.store(true, std::memory_order_relaxed);
    } else if (mode == PanelDisplayMode::UtilityEdit) {
        if (utilIdx == 0) {
            int cur = rawBitMode_ ? static_cast<int>(rawBitMode_->load(std::memory_order_relaxed)) : 2;
            int next = (cur + 2) % 3;
            if (bitModeObj_)
                bitModeObj_->setValueNotifyingHost(bitModeObj_->convertTo0to1(static_cast<float>(next)));
        } else if (utilIdx == 1) {
            int cur = rawRamModel_ ? static_cast<int>(rawRamModel_->load(std::memory_order_relaxed)) : 0;
            int next = 1 - cur;
            if (ramModelObj_)
                ramModelObj_->setValueNotifyingHost(ramModelObj_->convertTo0to1(static_cast<float>(next)));
        } else {
            float cur = rawClockRate_ ? rawClockRate_->load(std::memory_order_relaxed) : 31250.0f;
            float next = std::clamp(cur - 250.0f * mult, 15000.0f, 48000.0f);
            if (clockRateObj_)
                clockRateObj_->setValueNotifyingHost(clockRateObj_->convertTo0to1(next));
        }
    } else if (mode == PanelDisplayMode::StoreTargetSelect) {
        pressMemoryDown();
    } else if (mode == PanelDisplayMode::ParamSelect) {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            const auto& metas = aero::Spx90PresetDatabase::getAlgorithmParamMeta(workingProgram_.algorithm);
            const auto& m = metas[static_cast<size_t>(paramIdx)];
            if (m.active) {
                float cur = workingProgram_.params[static_cast<size_t>(paramIdx)];
                float next = std::clamp(cur - m.step * mult, m.minVal, m.maxVal);
                if (m.format == aero::ParamFormat::BaseKey && next < 36.0f) next = 0.0f;
                workingProgram_.params[static_cast<size_t>(paramIdx)] = next;
                programModified_.store(true, std::memory_order_relaxed);
            }
        }
        pushWorkingParamsToApvts();
    }
}

aero::Spx90Program AeroFX90AudioProcessor::getCurrentActiveProgramCopy() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return workingProgram_;
}

const std::array<aero::ParamMeta, aero::kMaxProgramParams>& AeroFX90AudioProcessor::getCurrentParamMetas() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return aero::Spx90PresetDatabase::getAlgorithmParamMeta(workingProgram_.algorithm);
}

void AeroFX90AudioProcessor::setActiveProgramParamValue(int paramIndex, float realValue) {
    struct ScopedPushGuard {
        std::atomic<int>& counter;
        explicit ScopedPushGuard(std::atomic<int>& c) : counter(c) {
            counter.fetch_add(1, std::memory_order_acq_rel);
        }
        ~ScopedPushGuard() {
            counter.fetch_sub(1, std::memory_order_acq_rel);
        }
    } guard(pushingToApvtsCount_);

    float normVal = 0.0f;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (paramIndex < 0 || paramIndex >= aero::kMaxProgramParams) return;
        const auto& metas = aero::Spx90PresetDatabase::getAlgorithmParamMeta(workingProgram_.algorithm);
        const auto& m = metas[static_cast<size_t>(paramIndex)];
        if (!m.active) return;
        float clamped = std::clamp(realValue, m.minVal, m.maxVal);
        workingProgram_.params[static_cast<size_t>(paramIndex)] = clamped;
        float span = std::max(1e-5f, m.maxVal - m.minVal);
        normVal = std::clamp((clamped - m.minVal) / span, 0.0f, 1.0f);
        lastPushedNormParams_[static_cast<size_t>(paramIndex)] = normVal;
        currentParamCursor_.store(paramIndex, std::memory_order_relaxed);
        programModified_.store(true, std::memory_order_relaxed);
        cachedAudioProgram_ = workingProgram_;
        currentTailSeconds_.store(computeTailLengthForProgram(workingProgram_), std::memory_order_relaxed);
    }
    if (auto* p = paramObjs_[static_cast<size_t>(paramIndex)]) {
        p->setValueNotifyingHost(normVal);
        if (rawParams_[static_cast<size_t>(paramIndex)]) {
            lastPushedNormParams_[static_cast<size_t>(paramIndex)] =
                rawParams_[static_cast<size_t>(paramIndex)]->load(std::memory_order_relaxed);
        }
    }
}

float AeroFX90AudioProcessor::getActiveProgramParamValue(int paramIndex) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (paramIndex < 0 || paramIndex >= aero::kMaxProgramParams) return 0.0f;
    return workingProgram_.params[static_cast<size_t>(paramIndex)];
}

void AeroFX90AudioProcessor::getLcdLines(std::string& outLine1, std::string& outLine2) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const auto ramModel = static_cast<aero::RamModel>(
        std::clamp(rawRamModel_ ? static_cast<int>(rawRamModel_->load(std::memory_order_relaxed)) : 0, 0, 1));
    const bool isBypassed = rawBypass_ && rawBypass_->load(std::memory_order_relaxed) >= 0.5f;
    const auto mode = panelMode_.load(std::memory_order_relaxed);
    const int activeSlot = activeSlotIndex_.load(std::memory_order_relaxed);
    const int previewSlot = previewSlotIndex_.load(std::memory_order_relaxed);
    const int paramCursor = currentParamCursor_.load(std::memory_order_relaxed);
    const int utilCursor = currentUtilityCursor_.load(std::memory_order_relaxed);
    const int storeSlot = storeTargetSlotIndex_.load(std::memory_order_relaxed);

    char l1[32]{};
    char l2[32]{};

    if (previewSlot != activeSlot && mode == PanelDisplayMode::ProgramPlay) {
        const auto& prevProg = memoryBank_[static_cast<size_t>(previewSlot)];
        std::snprintf(l1, sizeof(l1), " *%-14.14s", prevProg.name.c_str());
        std::snprintf(l2, sizeof(l2), " PRESS [RECALL] ");
        outLine1 = std::string(l1, 16);
        outLine2 = std::string(l2, 16);
        return;
    }

    if (workingProgram_.name.size() <= 12) {
        std::snprintf(l1, sizeof(l1), "  %-14.14s", workingProgram_.name.c_str());
    } else {
        std::snprintf(l1, sizeof(l1), " %-15.15s", workingProgram_.name.c_str());
    }

    switch (mode) {
        case PanelDisplayMode::ProgramPlay: {
            if (isBypassed) {
                std::snprintf(l2, sizeof(l2), "BYPASS   BAL=%2.0f%%", workingProgram_.balance);
            } else {
                const auto& metas = aero::Spx90PresetDatabase::getAlgorithmParamMeta(workingProgram_.algorithm);
                const auto& m = metas[static_cast<size_t>(paramCursor)];
                std::string pLine = aero::Spx90PresetDatabase::formatLcdParamLine(
                    m, workingProgram_.params[static_cast<size_t>(paramCursor)], ramModel);
                std::snprintf(l2, sizeof(l2), "%-16.16s", pLine.c_str());
            }
            break;
        }
        case PanelDisplayMode::ParamSelect: {
            const auto& metas = aero::Spx90PresetDatabase::getAlgorithmParamMeta(workingProgram_.algorithm);
            const auto& m = metas[static_cast<size_t>(paramCursor)];
            std::string pLine = aero::Spx90PresetDatabase::formatLcdParamLine(
                m, workingProgram_.params[static_cast<size_t>(paramCursor)], ramModel);
            std::snprintf(l2, sizeof(l2), "%-16.16s", pLine.c_str());
            break;
        }
        case PanelDisplayMode::BalanceEdit:
            std::snprintf(l2, sizeof(l2), "BALANCE =  %3.0f%% ", workingProgram_.balance);
            break;
        case PanelDisplayMode::OutLevelEdit:
            std::snprintf(l2, sizeof(l2), "OUT LVL =  %3.0f%% ", workingProgram_.outLevel);
            break;
        case PanelDisplayMode::UtilityEdit: {
            if (utilCursor == 0) {
                int bm = rawBitMode_ ? static_cast<int>(rawBitMode_->load(std::memory_order_relaxed)) : 2;
                const char* bNames[3] = {"12B LIN", "12B CMP", "16B PCM"};
                std::snprintf(l2, sizeof(l2), "DAC BIT =%7.7s", bNames[std::clamp(bm, 0, 2)]);
            } else if (utilCursor == 1) {
                std::snprintf(l2, sizeof(l2), "MEM MDL = %s",
                              ramModel == aero::RamModel::Spx90_MkII ? "SPX90II" : "SPX90-1");
            } else {
                float clk = (rawClockRate_ ? rawClockRate_->load(std::memory_order_relaxed) : 31250.0f) * 0.001f;
                std::snprintf(l2, sizeof(l2), "CLOCK   =%5.2fkHz", clk);
            }
            break;
        }
        case PanelDisplayMode::MidiEdit: {
            static const char* kNotes[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            int n = std::clamp(lastMidiNoteReceived_.load(std::memory_order_relaxed), 0, 127);
            std::snprintf(l2, sizeof(l2), "MIDI KEY=  %s%d  ", kNotes[n % 12], (n / 12) - 2);
            break;
        }
        case PanelDisplayMode::StoreTargetSelect:
            std::snprintf(l1, sizeof(l1), "STORE TO MEM #%02d", storeSlot + 1);
            std::snprintf(l2, sizeof(l2), "PRESS [STORE]   ");
            break;
        case PanelDisplayMode::StoreReadOnlyError:
            std::snprintf(l1, sizeof(l1), "#1~#30 READ ONLY");
            std::snprintf(l2, sizeof(l2), "USE RAM #31~#90 ");
            break;
        case PanelDisplayMode::StoreCompletedMsg:
            std::snprintf(l1, sizeof(l1), "MEMORY #%02d      ", activeSlot + 1);
            std::snprintf(l2, sizeof(l2), "** STORED! **   ");
            break;
    }

    outLine1 = std::string(l1, 16);
    outLine2 = std::string(l2, 16);
}

bool AeroFX90AudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* AeroFX90AudioProcessor::createEditor() {
    return new AeroFX90AudioProcessorEditor(*this);
}

namespace {

constexpr const char* kUserBankTag = "UserBank";
constexpr const char* kUserSlotTag = "Slot";

// Rebuild one user RAM slot from saved XML; rejects out-of-range slot/algorithm and sanitises values.
bool parseUserSlot(const juce::XmlElement& s, aero::Spx90Program& out) {
    const int idx = s.getIntAttribute("index", -1);
    if (idx < aero::kNumRomPresets || idx >= aero::kTotalMemorySlots)
        return false;

    const int algInt = s.getIntAttribute("alg", -1);
    if (algInt < 0 || algInt >= static_cast<int>(aero::AlgorithmType::Count))
        return false;

    const auto alg = static_cast<aero::AlgorithmType>(algInt);
    const auto& metas = aero::Spx90PresetDatabase::getAlgorithmParamMeta(alg);

    out = aero::Spx90Program{};
    out.memoryNumber = idx + 1;
    out.isRom = false;
    out.algorithm = alg;
    out.name = s.getStringAttribute("name").toUpperCase().substring(0, 15).toStdString();
    for (int i = 0; i < aero::kNumRomPresets; ++i) {
        if (aero::Spx90PresetDatabase::getRomPreset(i).algorithm == alg) {
            out.category = aero::Spx90PresetDatabase::getRomPreset(i).category;
            break;
        }
    }

    auto finiteOr = [](double v, double fallback) { return std::isfinite(v) ? v : fallback; };
    for (int k = 0; k < aero::kMaxProgramParams; ++k) {
        const auto& m = metas[static_cast<size_t>(k)];
        double v = finiteOr(s.getDoubleAttribute("p" + juce::String(k), m.defaultVal), m.defaultVal);
        v = m.active ? std::clamp(v, static_cast<double>(m.minVal), static_cast<double>(m.maxVal))
                     : std::clamp(v, -1.0e6, 1.0e6);
        out.params[static_cast<size_t>(k)] = static_cast<float>(v);
    }
    out.balance  = static_cast<float>(std::clamp(finiteOr(s.getDoubleAttribute("bal", 50.0), 50.0), 0.0, 100.0));
    out.outLevel = static_cast<float>(std::clamp(finiteOr(s.getDoubleAttribute("out", 100.0), 100.0), 0.0, 100.0));
    return true;
}

} // namespace

void AeroFX90AudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml) {
        while (auto* stale = xml->getChildByName(kUserBankTag))
            xml->removeChildElement(stale, true);

        std::lock_guard<std::mutex> lock(stateMutex_);
        xml->setAttribute("stateVersion", 2);
        xml->setAttribute("activeSlotIndex", activeSlotIndex_.load(std::memory_order_relaxed));
        for (int p = 0; p < aero::kMaxProgramParams; ++p) {
            xml->setAttribute("workParam" + juce::String(p), workingProgram_.params[static_cast<size_t>(p)]);
        }

        // User RAM #31..#90: names, algorithm, parameters and levels
        auto* bankXml = xml->createNewChildElement(kUserBankTag);
        for (int i = aero::kNumRomPresets; i < aero::kTotalMemorySlots; ++i) {
            const auto& prog = memoryBank_[static_cast<size_t>(i)];
            auto* s = bankXml->createNewChildElement(kUserSlotTag);
            s->setAttribute("index", i);
            s->setAttribute("name", juce::String(prog.name));
            s->setAttribute("alg", static_cast<int>(prog.algorithm));
            s->setAttribute("bal", prog.balance);
            s->setAttribute("out", prog.outLevel);
            for (int k = 0; k < aero::kMaxProgramParams; ++k)
                s->setAttribute("p" + juce::String(k), prog.params[static_cast<size_t>(k)]);
        }
        copyXmlToBinary(*xml, destData);
    }
}

void AeroFX90AudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState && xmlState->hasTagName(apvts.state.getType())) {
        // States saved before stateVersion 2 have no UserBank: keep the default RAM slots then.
        if (auto* bankXml = xmlState->getChildByName(kUserBankTag)) {
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                for (auto* s : bankXml->getChildWithTagNameIterator(kUserSlotTag)) {
                    aero::Spx90Program prog;
                    if (parseUserSlot(*s, prog))
                        memoryBank_[static_cast<size_t>(prog.memoryNumber - 1)] = prog;
                }
            }
            // Keep the bank out of the APVTS tree so it is not duplicated on every save
            xmlState->removeChildElement(bankXml, true);
        }

        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));

        const int slot = std::clamp(xmlState->getIntAttribute("activeSlotIndex", 14),
                                    0, aero::kTotalMemorySlots - 1);
        recallSlot(slot, false); // keep the Balance / Out Level the host just restored

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            const auto& metas = aero::Spx90PresetDatabase::getAlgorithmParamMeta(workingProgram_.algorithm);
            const auto& stored = memoryBank_[static_cast<size_t>(slot)];
            for (int p = 0; p < aero::kMaxProgramParams; ++p) {
                const auto attr = "workParam" + juce::String(p);
                if (!xmlState->hasAttribute(attr))
                    continue;
                const double raw = xmlState->getDoubleAttribute(attr);
                if (!std::isfinite(raw))
                    continue;
                const auto& m = metas[static_cast<size_t>(p)];
                float v = static_cast<float>(raw);
                if (m.active)
                    v = std::clamp(v, m.minVal, m.maxVal);
                workingProgram_.params[static_cast<size_t>(p)] = v;
                if (std::abs(v - stored.params[static_cast<size_t>(p)]) > 1e-6f)
                    programModified_.store(true, std::memory_order_relaxed);
            }
            cachedAudioProgram_ = workingProgram_;
            currentTailSeconds_.store(computeTailLengthForProgram(workingProgram_), std::memory_order_relaxed);
        }
        pushWorkingParamsToApvts();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AeroFX90AudioProcessor();
}
