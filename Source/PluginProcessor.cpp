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
    activeSlotIndex_ = 14; // #15 SYMPHONIC by default
    previewSlotIndex_ = activeSlotIndex_;
    workingProgram_ = memoryBank_[static_cast<size_t>(activeSlotIndex_)];
    pushWorkingParamsToApvts();
}

AeroFX90AudioProcessor::~AeroFX90AudioProcessor() = default;

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
double AeroFX90AudioProcessor::getTailLengthSeconds() const { return 5.0; }

int AeroFX90AudioProcessor::getNumPrograms() {
    return aero::kTotalMemorySlots;
}

int AeroFX90AudioProcessor::getCurrentProgram() {
    return activeSlotIndex_;
}

void AeroFX90AudioProcessor::setCurrentProgram(int index) {
    recallSlotDirectly(index);
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
    }
    for (int i = 0; i < aero::kMaxProgramParams; ++i) {
        if (auto* p = apvts.getParameter("param" + juce::String(i + 1))) {
            p->setValueNotifyingHost(normVals[static_cast<size_t>(i)]);
        }
    }
}

void AeroFX90AudioProcessor::syncDspFromState() {
    const float inKnob = apvts.getRawParameterValue("inputLevel")->load();
    const float inLinear = (inKnob / 7.5f); // 7.5 = unity (0 dB), 10.0 = +2.5 dB boost

    const int bitIdx = static_cast<int>(apvts.getRawParameterValue("bitMode")->load());
    const int ramIdx = static_cast<int>(apvts.getRawParameterValue("ramModel")->load());
    const float clockHz = apvts.getRawParameterValue("clockRate")->load();
    const float driveDb = apvts.getRawParameterValue("converterDrive")->load();
    const float noiseDb = apvts.getRawParameterValue("noiseFloor")->load();
    const bool hiFi = apvts.getRawParameterValue("hiFiBypass")->load() >= 0.5f;
    const bool monoIn = apvts.getRawParameterValue("monoInput")->load() >= 0.5f;
    const bool bypass = apvts.getRawParameterValue("bypass")->load() >= 0.5f;

    dspEngine_.setHardwareConfig(
        static_cast<aero::BitDepthMode>(std::clamp(bitIdx, 0, 2)),
        static_cast<aero::RamModel>(std::clamp(ramIdx, 0, 1)),
        clockHz, driveDb, noiseDb, hiFi, monoIn, bypass, inLinear);

    aero::MultiChainConfig mc;
    mc.enabled      = apvts.getRawParameterValue("multiChain")->load() >= 0.5f;
    mc.enableMod    = apvts.getRawParameterValue("chainMod")->load() >= 0.5f;
    mc.enablePitch  = apvts.getRawParameterValue("chainPitch")->load() >= 0.5f;
    mc.enableDelay  = apvts.getRawParameterValue("chainDelay")->load() >= 0.5f;
    mc.enableReverb = apvts.getRawParameterValue("chainReverb")->load() >= 0.5f;
    dspEngine_.setMultiChainConfig(mc);

    static const char* kParamIds[8] = {
        "param1", "param2", "param3", "param4", "param5", "param6", "param7", "param8"
    };
    float rawNorm[8]{};
    for (int i = 0; i < aero::kMaxProgramParams; ++i) {
        rawNorm[i] = apvts.getRawParameterValue(kParamIds[i])->load();
    }

    aero::Spx90Program progCopy;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
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
                currentParamCursor_ = i;
                programModified_ = true;
            }
        }
        workingProgram_.balance  = apvts.getRawParameterValue("balance")->load();
        workingProgram_.outLevel = apvts.getRawParameterValue("outLevel")->load();
        progCopy = workingProgram_;
    }
    dspEngine_.setActiveProgram(progCopy);
}

void AeroFX90AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    for (const auto metadata : midiMessages) {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn()) {
            lastMidiNoteReceived_ = msg.getNoteNumber();
            midiIndicatorCounter_.store(12, std::memory_order_relaxed);
            dspEngine_.handleMidiNoteOn(msg.getNoteNumber(), msg.getVelocity());
        } else if (msg.isProgramChange()) {
            int pgm = msg.getProgramChangeNumber();
            if (pgm >= 0 && pgm < aero::kTotalMemorySlots) {
                midiIndicatorCounter_.store(12, std::memory_order_relaxed);
                recallSlotDirectly(pgm);
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
    if (panelMode_ == PanelDisplayMode::StoreTargetSelect) {
        storeTargetSlotIndex_ = (storeTargetSlotIndex_ + 1) % aero::kTotalMemorySlots;
        return;
    }
    previewSlotIndex_ = (previewSlotIndex_ + 1) % aero::kTotalMemorySlots;
    panelMode_ = PanelDisplayMode::ProgramPlay;
}

void AeroFX90AudioProcessor::pressMemoryDown() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (panelMode_ == PanelDisplayMode::StoreTargetSelect) {
        storeTargetSlotIndex_ = (storeTargetSlotIndex_ + aero::kTotalMemorySlots - 1) % aero::kTotalMemorySlots;
        return;
    }
    previewSlotIndex_ = (previewSlotIndex_ + aero::kTotalMemorySlots - 1) % aero::kTotalMemorySlots;
    panelMode_ = PanelDisplayMode::ProgramPlay;
}

void AeroFX90AudioProcessor::pressRecall() {
    recallSlotDirectly(previewSlotIndex_);
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
        activeSlotIndex_ = clamped;
        previewSlotIndex_ = clamped;
        workingProgram_ = memoryBank_[static_cast<size_t>(clamped)];
        currentParamCursor_ = 0;
        programModified_ = false;
        panelMode_ = PanelDisplayMode::ProgramPlay;
        if (!applyProgramLevels) {
            // Session restore: the host-restored APVTS values win over the slot's stored levels
            workingProgram_.balance  = apvts.getRawParameterValue("balance")->load();
            workingProgram_.outLevel = apvts.getRawParameterValue("outLevel")->load();
        }
        newBal = workingProgram_.balance;
        newOut = workingProgram_.outLevel;
    }
    if (applyProgramLevels) {
        if (auto* pBal = apvts.getParameter("balance"))
            pBal->setValueNotifyingHost(pBal->convertTo0to1(newBal));
        if (auto* pOut = apvts.getParameter("outLevel"))
            pOut->setValueNotifyingHost(pOut->convertTo0to1(newOut));
    }
    pushWorkingParamsToApvts();
}

void AeroFX90AudioProcessor::pressStore() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (panelMode_ != PanelDisplayMode::StoreTargetSelect) {
        // First press enters Store Target Selection mode
        storeTargetSlotIndex_ = (activeSlotIndex_ >= aero::kNumRomPresets) ? activeSlotIndex_ : 30;
        panelMode_ = PanelDisplayMode::StoreTargetSelect;
        return;
    }
    // Second press attempts to write workingProgram_ into storeTargetSlotIndex_
    if (storeTargetSlotIndex_ < aero::kNumRomPresets) {
        // Authentic SPX90 error when trying to overwrite ROM presets #1~#30!
        panelMode_ = PanelDisplayMode::StoreReadOnlyError;
        return;
    }
    aero::Spx90Program saved = workingProgram_;
    saved.memoryNumber = storeTargetSlotIndex_ + 1;
    saved.isRom = false;
    memoryBank_[static_cast<size_t>(storeTargetSlotIndex_)] = saved;
    activeSlotIndex_ = storeTargetSlotIndex_;
    previewSlotIndex_ = storeTargetSlotIndex_;
    workingProgram_ = saved;
    programModified_ = false;
    panelMode_ = PanelDisplayMode::StoreCompletedMsg;
}

void AeroFX90AudioProcessor::pressBypass() {
    if (auto* p = apvts.getParameter("bypass")) {
        bool cur = apvts.getRawParameterValue("bypass")->load() >= 0.5f;
        p->setValueNotifyingHost(cur ? 0.0f : 1.0f);
    }
}

void AeroFX90AudioProcessor::pressBalance() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    panelMode_ = (panelMode_ == PanelDisplayMode::BalanceEdit)
        ? PanelDisplayMode::ProgramPlay
        : PanelDisplayMode::BalanceEdit;
}

void AeroFX90AudioProcessor::pressOutLevel() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    panelMode_ = (panelMode_ == PanelDisplayMode::OutLevelEdit)
        ? PanelDisplayMode::ProgramPlay
        : PanelDisplayMode::OutLevelEdit;
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
    if (panelMode_ != PanelDisplayMode::ParamSelect) {
        panelMode_ = PanelDisplayMode::ParamSelect;
        currentParamCursor_ = std::clamp(currentParamCursor_, 0, count - 1);
    } else {
        currentParamCursor_ = (currentParamCursor_ + 1) % count;
    }
}

void AeroFX90AudioProcessor::pressUtility() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (panelMode_ != PanelDisplayMode::UtilityEdit) {
        panelMode_ = PanelDisplayMode::UtilityEdit;
        currentUtilityCursor_ = 0;
    } else {
        currentUtilityCursor_ = (currentUtilityCursor_ + 1) % 3;
    }
}

void AeroFX90AudioProcessor::pressMidiTrigger() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (workingProgram_.algorithm == aero::AlgorithmType::FreezeA ||
        workingProgram_.algorithm == aero::AlgorithmType::FreezeB) {
        // Trigger manual sample capture
        workingProgram_.params[5] = 1.0f;
        dspEngine_.handleMidiNoteOn(lastMidiNoteReceived_, 100);
        panelMode_ = PanelDisplayMode::MidiEdit;
        return;
    }
    panelMode_ = (panelMode_ == PanelDisplayMode::MidiEdit)
        ? PanelDisplayMode::ProgramPlay
        : PanelDisplayMode::MidiEdit;
    dspEngine_.handleMidiNoteOn(lastMidiNoteReceived_, 100);
}

void AeroFX90AudioProcessor::pressValueUp(bool coarse) {
    float mult = coarse ? 5.0f : 1.0f;
    PanelDisplayMode mode;
    int paramIdx = 0;
    int utilIdx = 0;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        mode = panelMode_;
        paramIdx = currentParamCursor_;
        utilIdx = currentUtilityCursor_;
        if (mode == PanelDisplayMode::ProgramPlay) {
            mode = panelMode_ = PanelDisplayMode::ParamSelect;
        }
    }

    if (mode == PanelDisplayMode::BalanceEdit) {
        float cur = apvts.getRawParameterValue("balance")->load();
        float next = std::clamp(cur + 1.0f * mult, 0.0f, 100.0f);
        if (auto* p = apvts.getParameter("balance"))
            p->setValueNotifyingHost(p->convertTo0to1(next));
        programModified_ = true;
    } else if (mode == PanelDisplayMode::OutLevelEdit) {
        float cur = apvts.getRawParameterValue("outLevel")->load();
        float next = std::clamp(cur + 1.0f * mult, 0.0f, 100.0f);
        if (auto* p = apvts.getParameter("outLevel"))
            p->setValueNotifyingHost(p->convertTo0to1(next));
        programModified_ = true;
    } else if (mode == PanelDisplayMode::UtilityEdit) {
        if (utilIdx == 0) {
            int cur = static_cast<int>(apvts.getRawParameterValue("bitMode")->load());
            int next = (cur + 1) % 3;
            if (auto* p = apvts.getParameter("bitMode"))
                p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(next)));
        } else if (utilIdx == 1) {
            int cur = static_cast<int>(apvts.getRawParameterValue("ramModel")->load());
            int next = 1 - cur;
            if (auto* p = apvts.getParameter("ramModel"))
                p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(next)));
        } else {
            float cur = apvts.getRawParameterValue("clockRate")->load();
            float next = std::clamp(cur + 250.0f * mult, 15000.0f, 48000.0f);
            if (auto* p = apvts.getParameter("clockRate"))
                p->setValueNotifyingHost(p->convertTo0to1(next));
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
                programModified_ = true;
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
        mode = panelMode_;
        paramIdx = currentParamCursor_;
        utilIdx = currentUtilityCursor_;
        if (mode == PanelDisplayMode::ProgramPlay) {
            mode = panelMode_ = PanelDisplayMode::ParamSelect;
        }
    }

    if (mode == PanelDisplayMode::BalanceEdit) {
        float cur = apvts.getRawParameterValue("balance")->load();
        float next = std::clamp(cur - 1.0f * mult, 0.0f, 100.0f);
        if (auto* p = apvts.getParameter("balance"))
            p->setValueNotifyingHost(p->convertTo0to1(next));
        programModified_ = true;
    } else if (mode == PanelDisplayMode::OutLevelEdit) {
        float cur = apvts.getRawParameterValue("outLevel")->load();
        float next = std::clamp(cur - 1.0f * mult, 0.0f, 100.0f);
        if (auto* p = apvts.getParameter("outLevel"))
            p->setValueNotifyingHost(p->convertTo0to1(next));
        programModified_ = true;
    } else if (mode == PanelDisplayMode::UtilityEdit) {
        if (utilIdx == 0) {
            int cur = static_cast<int>(apvts.getRawParameterValue("bitMode")->load());
            int next = (cur + 2) % 3;
            if (auto* p = apvts.getParameter("bitMode"))
                p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(next)));
        } else if (utilIdx == 1) {
            int cur = static_cast<int>(apvts.getRawParameterValue("ramModel")->load());
            int next = 1 - cur;
            if (auto* p = apvts.getParameter("ramModel"))
                p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(next)));
        } else {
            float cur = apvts.getRawParameterValue("clockRate")->load();
            float next = std::clamp(cur - 250.0f * mult, 15000.0f, 48000.0f);
            if (auto* p = apvts.getParameter("clockRate"))
                p->setValueNotifyingHost(p->convertTo0to1(next));
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
                programModified_ = true;
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
        currentParamCursor_ = paramIndex;
        programModified_ = true;
    }
    if (auto* p = apvts.getParameter("param" + juce::String(paramIndex + 1))) {
        p->setValueNotifyingHost(normVal);
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
        std::clamp(static_cast<int>(apvts.getRawParameterValue("ramModel")->load()), 0, 1));
    const bool isBypassed = apvts.getRawParameterValue("bypass")->load() >= 0.5f;

    char l1[32]{};
    char l2[32]{};

    if (previewSlotIndex_ != activeSlotIndex_ && panelMode_ == PanelDisplayMode::ProgramPlay) {
        const auto& prevProg = memoryBank_[static_cast<size_t>(previewSlotIndex_)];
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

    switch (panelMode_) {
        case PanelDisplayMode::ProgramPlay: {
            if (isBypassed) {
                std::snprintf(l2, sizeof(l2), "BYPASS   BAL=%2.0f%%", workingProgram_.balance);
            } else {
                const auto& metas = aero::Spx90PresetDatabase::getAlgorithmParamMeta(workingProgram_.algorithm);
                const auto& m = metas[static_cast<size_t>(currentParamCursor_)];
                std::string pLine = aero::Spx90PresetDatabase::formatLcdParamLine(
                    m, workingProgram_.params[static_cast<size_t>(currentParamCursor_)], ramModel);
                std::snprintf(l2, sizeof(l2), "%-16.16s", pLine.c_str());
            }
            break;
        }
        case PanelDisplayMode::ParamSelect: {
            const auto& metas = aero::Spx90PresetDatabase::getAlgorithmParamMeta(workingProgram_.algorithm);
            const auto& m = metas[static_cast<size_t>(currentParamCursor_)];
            std::string pLine = aero::Spx90PresetDatabase::formatLcdParamLine(
                m, workingProgram_.params[static_cast<size_t>(currentParamCursor_)], ramModel);
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
            if (currentUtilityCursor_ == 0) {
                int bm = static_cast<int>(apvts.getRawParameterValue("bitMode")->load());
                const char* bNames[3] = {"12B LIN", "12B CMP", "16B PCM"};
                std::snprintf(l2, sizeof(l2), "DAC BIT =%7.7s", bNames[std::clamp(bm, 0, 2)]);
            } else if (currentUtilityCursor_ == 1) {
                std::snprintf(l2, sizeof(l2), "MEM MDL = %s",
                              ramModel == aero::RamModel::Spx90_MkII ? "SPX90II" : "SPX90-1");
            } else {
                float clk = apvts.getRawParameterValue("clockRate")->load() * 0.001f;
                std::snprintf(l2, sizeof(l2), "CLOCK   =%5.2fkHz", clk);
            }
            break;
        }
        case PanelDisplayMode::MidiEdit: {
            static const char* kNotes[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            int n = std::clamp(lastMidiNoteReceived_, 0, 127);
            std::snprintf(l2, sizeof(l2), "MIDI KEY=  %s%d  ", kNotes[n % 12], (n / 12) - 2);
            break;
        }
        case PanelDisplayMode::StoreTargetSelect:
            std::snprintf(l1, sizeof(l1), "STORE TO MEM #%02d", storeTargetSlotIndex_ + 1);
            std::snprintf(l2, sizeof(l2), "PRESS [STORE]   ");
            break;
        case PanelDisplayMode::StoreReadOnlyError:
            std::snprintf(l1, sizeof(l1), "#1~#30 READ ONLY");
            std::snprintf(l2, sizeof(l2), "USE RAM #31~#90 ");
            break;
        case PanelDisplayMode::StoreCompletedMsg:
            std::snprintf(l1, sizeof(l1), "MEMORY #%02d      ", activeSlotIndex_ + 1);
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
        xml->setAttribute("activeSlotIndex", activeSlotIndex_);
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
                    programModified_ = true;
            }
        }
        pushWorkingParamsToApvts();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AeroFX90AudioProcessor();
}
