#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/Spx90Types.h"
#include "DSP/Spx90PresetDatabase.h"
#include "DSP/Spx90DspEngine.h"
#include <array>
#include <mutex>

class AeroFX90AudioProcessor : public juce::AudioProcessor {
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
    int getActiveSlotIndex() const { return activeSlotIndex_; }
    int getPreviewSlotIndex() const { return previewSlotIndex_; }
    bool isPreviewPending() const { return previewSlotIndex_ != activeSlotIndex_; }
    bool isProgramModified() const { return programModified_; }
    bool isMidiActivityLit() const { return midiIndicatorCounter_.load(std::memory_order_relaxed) > 0; }

    aero::Spx90Program getCurrentActiveProgramCopy() const;
    const std::array<aero::ParamMeta, aero::kMaxProgramParams>& getCurrentParamMetas() const;
    void setActiveProgramParamValue(int paramIndex, float realValue);
    float getActiveProgramParamValue(int paramIndex) const;

    // Format the two 16-character lines for the 16x2 backlit LCD
    void getLcdLines(std::string& outLine1, std::string& outLine2) const;

    float getPeakMeterLevel() const { return dspEngine_.getPeakInputLevel(); }
    bool getOverflowLed() const { return dspEngine_.getOverflowLed(); }

    PanelDisplayMode getPanelMode() const { return panelMode_; }
    int getSelectedParamIndex() const { return currentParamCursor_; }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void syncDspFromState();
    void pushWorkingParamsToApvts();
    // applyProgramLevels=false keeps the current Balance / Out Level (used when restoring a session)
    void recallSlot(int slotIndexZeroBased, bool applyProgramLevels);
    int getActiveParamCount() const;

    mutable std::mutex stateMutex_;
    std::array<aero::Spx90Program, aero::kTotalMemorySlots> memoryBank_;
    aero::Spx90Program workingProgram_;
    std::array<float, aero::kMaxProgramParams> lastPushedNormParams_{};

    int activeSlotIndex_ = 14;  // Starts on #15 SYMPHONIC (index 14)
    int previewSlotIndex_ = 14;
    int storeTargetSlotIndex_ = 30; // Default first user RAM slot (#31)
    int currentParamCursor_ = 0;
    int currentUtilityCursor_ = 0;
    bool programModified_ = false;
    PanelDisplayMode panelMode_ = PanelDisplayMode::ProgramPlay;

    std::atomic<int> midiIndicatorCounter_{0};
    int lastMidiNoteReceived_ = 60;

    aero::Spx90DspEngine dspEngine_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AeroFX90AudioProcessor)
};
