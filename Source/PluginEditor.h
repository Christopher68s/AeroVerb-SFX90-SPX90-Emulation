#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include <array>

class AeroHardwareLookAndFeel : public juce::LookAndFeel_V4 {
public:
    AeroHardwareLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;
};

class AeroFX90AudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer {
public:
    explicit AeroFX90AudioProcessorEditor(AeroFX90AudioProcessor&);
    ~AeroFX90AudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void toggleModernView();
    void syncProgramKnobsFromProcessor();

    void drawRackFaceplate(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawAeroVerbSfx90Badge(juce::Graphics& g, juce::Rectangle<float> badgeArea);
    void drawSevenSegmentDisplay(juce::Graphics& g, juce::Rectangle<float> area, int number,
                                 bool flashOff, bool powerOn);
    void drawSlantedSevenSegmentDigit(juce::Graphics& g, juce::Rectangle<float> digitRect,
                                      int digit, bool lit, bool blankIfOff);
    void drawDotMatrixLcd(juce::Graphics& g, juce::Rectangle<float> lcdBounds,
                          const std::string& line1, const std::string& line2, bool powerOn);
    void drawLedMeterLadder(juce::Graphics& g, juce::Rectangle<float> meterBounds,
                            float peakLevel, bool overflow, bool powerOn);
    void drawQuarterInchJack(juce::Graphics& g, juce::Point<float> center, const juce::String& label,
                             float lineStartX);
    void drawManufacturerBadge(juce::Graphics& g, juce::Rectangle<float> area);
    void drawModernDrawerBackground(juce::Graphics& g, juce::Rectangle<int> drawerBounds);

    AeroFX90AudioProcessor& audioProcessor;
    AeroHardwareLookAndFeel hardwareLnf_;

    bool showModernView_ = false;
    float drawerAnimProgress_ = 0.0f; // 0.0 = closed (260px), 1.0 = open (660px)
    bool unitPowerOn_ = true;
    int blinkCounter_ = 0;
    int lastSyncedSlot_ = -1;
    aero::AlgorithmType lastSyncedAlg_ = aero::AlgorithmType::Count;

    // --- 1U Hardware Rack Controls (Matching SPX90 Front Panel Reference) ---
    juce::TextButton powerBtn_{""};
    juce::Slider inputLevelSlider_;

    // Cluster 1: PARAMETER | UP/DOWN | BALANCE
    juce::TextButton paramBtn_{"PARAM-\nETER"};
    juce::TextButton valUpBtn_{"UP"};
    juce::TextButton valDownBtn_{"DOWN"};
    juce::TextButton balanceBtn_{"BALANCE"};

    // Cluster 2: STORE | UP/DOWN | RECALL
    juce::TextButton storeBtn_{"STORE"};
    juce::TextButton memUpBtn_{"UP"};
    juce::TextButton memDownBtn_{"DOWN"};
    juce::TextButton recallBtn_{"RECALL"};

    // Cluster 3: UTILITY | FOOT TRIGGER / BYPASS
    juce::TextButton utilityBtn_{"UTILITY"};
    juce::TextButton midiTrigBtn_{"FOOT\nTRIGGER"};
    juce::TextButton bypassBtn_{"BYPASS"};

    // Drawer Toggle
    juce::TextButton modernViewToggleBtn_{"STUDIO VIEW ▼"};

    // --- Modern Studio View Controls ---
    juce::ComboBox presetSelector_;
    std::array<juce::Slider, aero::kMaxProgramParams> progParamSliders_;
    std::array<juce::Label, aero::kMaxProgramParams> progParamLabels_;

    juce::Slider modernBalanceSlider_;
    juce::Label modernBalanceLabel_;
    juce::Slider modernOutLevelSlider_;
    juce::Label modernOutLevelLabel_;

    juce::ComboBox bitDepthCombo_;
    juce::ComboBox ramModelCombo_;
    juce::Slider clockRateSlider_;
    juce::Label clockRateLabel_;
    juce::Slider converterDriveSlider_;
    juce::Label converterDriveLabel_;
    juce::Slider noiseFloorSlider_;
    juce::Label noiseFloorLabel_;
    juce::ToggleButton hiFiBypassToggle_{"HI-FI FILTER BYPASS"};

    // Multi-Chain Controls
    juce::ToggleButton multiChainToggle_{"ENABLE MULTI-CHAIN"};
    juce::ToggleButton chainModToggle_{"MOD (SYMPHONIC)"};
    juce::ToggleButton chainPitchToggle_{"PITCH (DETUNE C)"};
    juce::ToggleButton chainDelayToggle_{"DELAY (ECHO)"};
    juce::ToggleButton chainReverbToggle_{"REVERB (HALL)"};

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> inputLevelAtt_;
    std::unique_ptr<SliderAttachment> balanceAtt_;
    std::unique_ptr<SliderAttachment> outLevelAtt_;
    std::unique_ptr<ComboBoxAttachment> bitDepthAtt_;
    std::unique_ptr<ComboBoxAttachment> ramModelAtt_;
    std::unique_ptr<SliderAttachment> clockRateAtt_;
    std::unique_ptr<SliderAttachment> converterDriveAtt_;
    std::unique_ptr<SliderAttachment> noiseFloorAtt_;
    std::unique_ptr<ButtonAttachment> hiFiBypassAtt_;
    std::unique_ptr<ButtonAttachment> multiChainAtt_;
    std::unique_ptr<ButtonAttachment> chainModAtt_;
    std::unique_ptr<ButtonAttachment> chainPitchAtt_;
    std::unique_ptr<ButtonAttachment> chainDelayAtt_;
    std::unique_ptr<ButtonAttachment> chainReverbAtt_;

    // Cached layout areas for painting
    juce::Rectangle<int> rackArea_;
    juce::Rectangle<float> displayWindowArea_;
    juce::Rectangle<float> badgeArea_;
    juce::Rectangle<float> meterArea_;
    juce::Rectangle<float> sevenSegArea_;
    juce::Rectangle<float> lcdArea_;
    juce::Point<float> topJackCenter_;
    juce::Point<float> botJackCenter_;
    juce::Rectangle<float> manufacturerBadgeArea_;
    juce::Rectangle<int> modernArea_;

    juce::Typeface::Ptr juraRegularTypeface_;
    juce::Typeface::Ptr juraSemiBoldTypeface_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AeroFX90AudioProcessorEditor)
};
