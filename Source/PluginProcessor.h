#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/Spx90Types.h"
#include "DSP/Spx90PresetDatabase.h"
#include "DSP/Spx90DspEngine.h"
#include <array>
#include <atomic>
#include <mutex>

class AeroFX90AudioProcessor : public juce::AudioProcessor,
                               private juce::AsyncUpdater {
public:
    enum class PanelDisplayMode {
        ProgramPlay = 0,
        ParamSelect,
        BalanceEdit,
        OutLevelEdit,
        UtilityEdit,
        MidiEdit,
        StoreTargetSelect,
        StoreReadOnlyError,
        StoreCompletedMsg
    };

    AeroFX90AudioProcessor();
    ~AeroFX90AudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- SPX90 Hardware Front-Panel Operations ---
    void pressMemoryUp();
    void pressMemoryDown();
    void pressRecall();
    void pressStore();
    void pressBypass();
    void pressBalance();
    void pressOutLevel();
    void pressParam();
    void pressUtility();
    void pressMidiTrigger();
    void pressValueUp(bool coarse = false);
    void pressValueDown(bool coarse = false);

    // Direct access for Modern View & GUI
    void recallSlotDirectly(int slotIndexZeroBased);
    int getActiveSlotIndex() const { return activeSlotIndex_.load(std::memory_order_relaxed); }
    int getPreviewSlotIndex() const { return previewSlotIndex_.load(std::memory_order_relaxed); }
    bool isPreviewPending() const {
        return previewSlotIndex_.load(std::memory_order_relaxed) != activeSlotIndex_.load(std::memory_order_relaxed);
    }
    bool isProgramModified() const { return programModified_.load(std::memory_order_relaxed); }
    bool isMidiActivityLit() const { return midiIndicatorCounter_.load(std::memory_order_relaxed) > 0; }

    aero::Spx90Program getCurrentActiveProgramCopy() const;
    const std::array<aero::ParamMeta, aero::kMaxProgramParams>& getCurrentParamMetas() const;
    void setActiveProgramParamValue(int paramIndex, float realValue);
    float getActiveProgramParamValue(int paramIndex) const;

    // Format the two 16-character lines for the 16x2 backlit LCD
    void getLcdLines(std::string& outLine1, std::string& outLine2) const;

    float getPeakMeterLevel() const { return dspEngine_.getPeakInputLevel(); }
    bool getOverflowLed() const { return dspEngine_.getOverflowLed(); }

    PanelDisplayMode getPanelMode() const { return panelMode_.load(std::memory_order_relaxed); }
    int getSelectedParamIndex() const { return currentParamCursor_.load(std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void handleAsyncUpdate() override;
    void syncDspFromState();
    void pushWorkingParamsToApvts();
    // applyProgramLevels=false keeps the current Balance / Out Level (used when restoring a session)
    void recallSlot(int slotIndexZeroBased, bool applyProgramLevels);
    int getActiveParamCount() const;
    static double computeTailLengthForProgram(const aero::Spx90Program& prog);

    mutable std::mutex stateMutex_;
    std::array<aero::Spx90Program, aero::kTotalMemorySlots> memoryBank_;
    aero::Spx90Program workingProgram_;
    aero::Spx90Program cachedAudioProgram_;
    std::array<float, aero::kMaxProgramParams> lastPushedNormParams_{};

    std::atomic<int> activeSlotIndex_{14};  // Starts on #15 SYMPHONIC (index 14)
    std::atomic<int> previewSlotIndex_{14};
    std::atomic<int> storeTargetSlotIndex_{30}; // Default first user RAM slot (#31)
    std::atomic<int> currentParamCursor_{0};
    std::atomic<int> currentUtilityCursor_{0};
    std::atomic<bool> programModified_{false};
    std::atomic<PanelDisplayMode> panelMode_{PanelDisplayMode::ProgramPlay};
    std::atomic<double> currentTailSeconds_{5.0};

    std::atomic<int> midiIndicatorCounter_{0};
    std::atomic<int> lastMidiNoteReceived_{60};
    std::atomic<int> pendingMidiProgramChange_{-1};
    std::atomic<int> pushingToApvtsCount_{0};

    // Cached APVTS parameter pointers for lock-free audio thread access
    std::atomic<float>* rawInputLevel_     = nullptr;
    std::atomic<float>* rawBalance_        = nullptr;
    std::atomic<float>* rawOutLevel_       = nullptr;
    std::atomic<float>* rawBypass_         = nullptr;
    std::atomic<float>* rawMonoInput_      = nullptr;
    std::atomic<float>* rawBitMode_        = nullptr;
    std::atomic<float>* rawRamModel_       = nullptr;
    std::atomic<float>* rawClockRate_      = nullptr;
    std::atomic<float>* rawConverterDrive_ = nullptr;
    std::atomic<float>* rawNoiseFloor_     = nullptr;
    std::atomic<float>* rawHiFiBypass_     = nullptr;
    std::atomic<float>* rawMultiChain_     = nullptr;
    std::atomic<float>* rawChainMod_       = nullptr;
    std::atomic<float>* rawChainPitch_     = nullptr;
    std::atomic<float>* rawChainDelay_     = nullptr;
    std::atomic<float>* rawChainReverb_    = nullptr;
    std::array<std::atomic<float>*, aero::kMaxProgramParams> rawParams_{};
    std::array<juce::RangedAudioParameter*, aero::kMaxProgramParams> paramObjs_{};
    juce::RangedAudioParameter* balanceObj_   = nullptr;
    juce::RangedAudioParameter* outLevelObj_  = nullptr;
    juce::RangedAudioParameter* bypassObj_    = nullptr;
    juce::RangedAudioParameter* bitModeObj_   = nullptr;
    juce::RangedAudioParameter* ramModelObj_  = nullptr;
    juce::RangedAudioParameter* clockRateObj_ = nullptr;

    aero::Spx90DspEngine dspEngine_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AeroFX90AudioProcessor)
};
