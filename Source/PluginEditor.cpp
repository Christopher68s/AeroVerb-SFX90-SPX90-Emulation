#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UI/JuraFontData.h"
#include <cmath>
#include <algorithm>
#include <cctype>

namespace {

// 5x7 dot-matrix character bitmap (7 rows, each 5 bits wide: bit 4 = left col, bit 0 = right col)
struct Glyph5x7 {
    uint8_t rows[7];
};

Glyph5x7 get5x7Glyph(char ch) {
    switch (ch) {
        case 'A': return {{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
        case 'B': return {{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}};
        case 'C': return {{0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}};
        case 'D': return {{0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}};
        case 'E': return {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}};
        case 'F': return {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}};
        case 'G': return {{0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}};
        case 'H': return {{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
        case 'I': return {{0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}};
        case 'J': return {{0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}};
        case 'K': return {{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}};
        case 'L': return {{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}};
        case 'M': return {{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}};
        case 'N': return {{0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}};
        case 'O': return {{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
        case 'P': return {{0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}};
        case 'Q': return {{0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}};
        case 'R': return {{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}};
        case 'S': return {{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}};
        case 'T': return {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}};
        case 'U': return {{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
        case 'V': return {{0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}};
        case 'W': return {{0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}};
        case 'X': return {{0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}};
        case 'Y': return {{0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}};
        case 'Z': return {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}};

        // Lowercase units ('s', 'm', 'k', 'z', 'c', 'd', 'h')
        case 's': return {{0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E}};
        case 'm': return {{0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11}};
        case 'k': return {{0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}};
        case 'z': return {{0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F}};
        case 'c': return {{0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E}};
        case 'd': return {{0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F}};
        case 'h': return {{0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11}};

        // Digits 0..9
        case '0': return {{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}};
        case '1': return {{0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}};
        case '2': return {{0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F}};
        case '3': return {{0x1F, 0x02, 0x04, 0x06, 0x01, 0x11, 0x0E}};
        case '4': return {{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}};
        case '5': return {{0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}};
        case '6': return {{0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}};
        case '7': return {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}};
        case '8': return {{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}};
        case '9': return {{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};

        // Punctuation & symbols
        case '=': return {{0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}};
        case '-': return {{0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}};
        case '+': return {{0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}};
        case '.': return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}};
        case ',': return {{0x00, 0x00, 0x00, 0x00, 0x06, 0x04, 0x08}};
        case '%': return {{0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}};
        case '/': return {{0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}};
        case '*': return {{0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}};
        case '#': return {{0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A, 0x00}};
        case '~': return {{0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00}};
        case '<': return {{0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}};
        case '>': return {{0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}};
        case '[': return {{0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}};
        case ']': return {{0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}};
        case '!': return {{0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}};
        case ' ':
        default:
            if (ch >= 'a' && ch <= 'z') {
                return get5x7Glyph(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            }
            return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    }
}

} // namespace

// ==============================================================================

AeroHardwareLookAndFeel::AeroHardwareLookAndFeel() {
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffa8ff20));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff081404));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff244214));
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0d1216));
    setColour(juce::ComboBox::textColourId, juce::Colour(0xffecf2f6));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2c3842));
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff10151a));
    setColour(juce::PopupMenu::textColourId, juce::Colour(0xffe2eaf0));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff00c8be));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xff080c10));
}

void AeroHardwareLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                               float sliderPos, float rotaryStartAngle,
                                               float rotaryEndAngle, juce::Slider& slider) {
    const float radius = static_cast<float>(juce::jmin(width / 2, height / 2)) - 8.0f;
    const float centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const float rx = centreX - radius;
    const float ry = centreY - radius;
    const float rw = radius * 2.0f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Subtle glowing retro-fi track & active arc on Studio View knobs
    const bool isStudioKnob = (slider.getTextBoxPosition() != juce::Slider::NoTextBox);
    if (isStudioKnob && slider.isEnabled()) {
        juce::Path trackArc;
        trackArc.addCentredArc(centreX, centreY, radius + 4.0f, radius + 4.0f,
                               0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xff1c242c));
        g.strokePath(trackArc, juce::PathStrokeType(2.5f));

        juce::Path valArc;
        valArc.addCentredArc(centreX, centreY, radius + 4.0f, radius + 4.0f,
                             0.0f, rotaryStartAngle, angle, true);
        g.setColour(juce::Colour(0xff00e2d6).withAlpha(0.35f));
        g.strokePath(valArc, juce::PathStrokeType(5.0f));
        g.setColour(juce::Colour(0xff00e2d6));
        g.strokePath(valArc, juce::PathStrokeType(2.2f));
    }

    // 11 white radial tick marks around perimeter (matching SPX90 INPUT knob)
    g.setColour(isStudioKnob ? juce::Colour(0xff8fa0b0) : juce::Colour(0xffe8ecf0));
    for (int i = 0; i <= 10; ++i) {
        float t = static_cast<float>(i) / 10.0f;
        float a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle) - juce::MathConstants<float>::halfPi;
        float r1 = radius + (isStudioKnob ? 6.0f : 2.5f);
        float r2 = radius + (isStudioKnob ? 9.0f : 6.5f);
        g.drawLine(centreX + std::cos(a) * r1, centreY + std::sin(a) * r1,
                   centreX + std::cos(a) * r2, centreY + std::sin(a) * r2, 1.5f);
    }

    // Deep glossy drop shadow
    g.setColour(juce::Colours::black.withAlpha(0.82f));
    g.fillEllipse(rx + 2.0f, ry + 3.5f, rw, rw);

    // Anodized satin-gloss black outer skirt with top specular highlight
    juce::ColourGradient outerGrad(juce::Colour(0xff3e434c), centreX - radius * 0.6f, centreY - radius,
                                   juce::Colour(0xff0e1014), centreX + radius * 0.6f, centreY + radius, false);
    outerGrad.addColour(0.45, juce::Colour(0xff22262c));
    g.setGradientFill(outerGrad);
    g.fillEllipse(rx, ry, rw, rw);

    // Metallic rim highlight
    g.setColour(juce::Colour(0xff5a626e).withAlpha(0.75f));
    g.drawEllipse(rx, ry, rw, rw, 1.2f);

    // Glossy inner cap with specular dome reflection
    const float innerR = radius * 0.76f;
    juce::ColourGradient capGrad(juce::Colour(0xff323740), centreX, centreY - innerR,
                                 juce::Colour(0xff121418), centreX, centreY + innerR, false);
    capGrad.addColour(0.35, juce::Colour(0xff1e2228));
    g.setGradientFill(capGrad);
    g.fillEllipse(centreX - innerR, centreY - innerR, innerR * 2.0f, innerR * 2.0f);

    // Top glossy crescent reflection on knob cap
    juce::Path glossArc;
    glossArc.addArc(centreX - innerR * 0.82f, centreY - innerR * 0.85f,
                    innerR * 1.64f, innerR * 1.1f, -0.9f, 0.9f, true);
    g.setColour(juce::Colours::white.withAlpha(0.14f));
    g.strokePath(glossArc, juce::PathStrokeType(1.8f));

    g.setColour(juce::Colour(0xff0a0c0e));
    g.drawEllipse(centreX - innerR, centreY - innerR, innerR * 2.0f, innerR * 2.0f, 1.0f);

    // Crisp pointer line near outer edge
    juce::Path pointer;
    const float pLen = radius * 0.44f;
    const float pThick = 2.5f;
    pointer.addRoundedRectangle(-pThick * 0.5f, -radius + 2.0f, pThick, pLen, 1.0f);
    g.setColour(slider.isEnabled() ? juce::Colours::white : juce::Colour(0xff555a60));
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centreX, centreY));
}

void AeroHardwareLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                   const juce::Colour& /*backgroundColour*/,
                                                   bool isHighlighted, bool isDown) {
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);

    // Deep recessed well around hardware keycap
    g.setColour(juce::Colour(0xff050608));
    g.fillRoundedRectangle(bounds.expanded(1.5f), 2.5f);

    // Satin-gloss sculpted keycap gradient
    juce::Colour topCol = juce::Colour(0xff383c44);
    juce::Colour midCol = juce::Colour(0xff23262b);
    juce::Colour botCol = juce::Colour(0xff16181c);

    if (isHighlighted) {
        topCol = topCol.brighter(0.10f);
        midCol = midCol.brighter(0.06f);
    }
    if (isDown) {
        std::swap(topCol, botCol);
        bounds.translate(0.0f, 1.0f);
    }

    juce::ColourGradient btnGrad(topCol, bounds.getX(), bounds.getY(),
                                 botCol, bounds.getX(), bounds.getBottom(), false);
    btnGrad.addColour(0.32, midCol);
    g.setGradientFill(btnGrad);
    g.fillRoundedRectangle(bounds, 2.0f);

    // Glossy top-half sheen overlay (gives modern glossy hardware keycap feel)
    if (!isDown) {
        auto glossBand = bounds.withHeight(bounds.getHeight() * 0.42f).reduced(1.5f, 1.0f);
        juce::ColourGradient glossGrad(juce::Colours::white.withAlpha(0.14f), glossBand.getX(), glossBand.getY(),
                                       juce::Colours::white.withAlpha(0.01f), glossBand.getX(), glossBand.getBottom(), false);
        g.setGradientFill(glossGrad);
        g.fillRoundedRectangle(glossBand, 1.5f);
    }

    // Crisp top specular edge
    g.setColour(juce::Colours::white.withAlpha(isDown ? 0.05f : 0.22f));
    g.drawHorizontalLine(static_cast<int>(bounds.getY() + 1.0f), bounds.getX() + 2.0f, bounds.getRight() - 2.0f);

    // Keycap border
    g.setColour(juce::Colour(0xff454a54));
    g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
}

void AeroHardwareLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                             bool /*isHighlighted*/, bool isDown) {
    auto b = button.getLocalBounds().toFloat();
    if (isDown) b.translate(0.0f, 1.0f);

    const juce::String txt = button.getButtonText();

    // Large hollow white triangle arrows for UP / DOWN buttons
    if (txt == "UP" || txt == "DOWN") {
        juce::Path tri;
        float cx = b.getCentreX();
        float cy = b.getCentreY();
        float w = std::min(b.getWidth(), b.getHeight()) * 0.48f;
        float h = w * 1.10f;
        if (txt == "UP") {
            tri.addTriangle(cx, cy - h * 0.5f, cx - w * 0.55f, cy + h * 0.5f, cx + w * 0.55f, cy + h * 0.5f);
        } else {
            tri.addTriangle(cx - w * 0.55f, cy - h * 0.5f, cx + w * 0.55f, cy - h * 0.5f, cx, cy + h * 0.5f);
        }
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.strokePath(tri, juce::PathStrokeType(2.2f), juce::AffineTransform::translation(0.0f, 1.0f));
        g.setColour(juce::Colour(0xfff0f4f8));
        g.strokePath(tri, juce::PathStrokeType(1.7f));
        return;
    }

    // Horizontal Ruby-Red LED lens bar on PARAMETER and BYPASS buttons
    const bool hasRedLedBar = txt.startsWith("PARAM") || (txt == "BYPASS");
    if (hasRedLedBar) {
        juce::Rectangle<float> ledRect(b.getCentreX() - 10.5f, b.getY() + 6.0f, 21.0f, 5.0f);
        bool lit = button.getToggleState();

        // Recessed dark bezel around LED lens
        g.setColour(juce::Colour(0xff090404));
        g.fillRoundedRectangle(ledRect.expanded(1.0f), 1.5f);

        if (lit) {
            // Outer red halo bloom
            g.setColour(juce::Colour(0xffff1a1a).withAlpha(0.42f));
            g.fillRoundedRectangle(ledRect.expanded(3.5f), 3.0f);
            g.setColour(juce::Colour(0xffff2e24));
            g.fillRoundedRectangle(ledRect, 1.0f);
            // White hot core highlight
            g.setColour(juce::Colour(0xffffb0a8).withAlpha(0.75f));
            g.fillRect(ledRect.reduced(3.0f, 1.6f));
        } else {
            g.setColour(juce::Colour(0xff4a1012));
            g.fillRoundedRectangle(ledRect, 1.0f);
            g.setColour(juce::Colours::white.withAlpha(0.15f));
            g.drawHorizontalLine(static_cast<int>(ledRect.getY() + 1.0f), ledRect.getX() + 2.0f, ledRect.getRight() - 2.0f);
        }
        b.removeFromTop(12.0f);
    }

    auto font = juce::Font(juce::FontOptions(10.0f, juce::Font::bold));
    g.setFont(font);
    g.setColour(txt.contains("STUDIO VIEW") ? juce::Colour(0xff00e2d6) : juce::Colour(0xfff0f4f8));
    g.drawFittedText(txt, b.toNearestInt().reduced(3, 2), juce::Justification::centred, 2);
}

// ==============================================================================

AeroFX90AudioProcessorEditor::AeroFX90AudioProcessorEditor(AeroFX90AudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
    setLookAndFeel(&hardwareLnf_);

    juraRegularTypeface_ = juce::Typeface::createSystemTypefaceFor(
        aero::fonts::kJuraRegularTtf, sizeof(aero::fonts::kJuraRegularTtf));
    juraSemiBoldTypeface_ = juce::Typeface::createSystemTypefaceFor(
        aero::fonts::kJuraSemiBoldTtf, sizeof(aero::fonts::kJuraSemiBoldTtf));

    powerBtn_.setClickingTogglesState(true);
    powerBtn_.setToggleState(true, juce::dontSendNotification);
    powerBtn_.onClick = [this]() {
        unitPowerOn_ = powerBtn_.getToggleState();
        if (auto* byp = audioProcessor.apvts.getParameter("bypass")) {
            byp->setValueNotifyingHost(unitPowerOn_ ? 0.0f : 1.0f);
        }
        repaint();
    };
    addAndMakeVisible(powerBtn_);

    inputLevelSlider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    inputLevelSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(inputLevelSlider_);
    inputLevelAtt_ = std::make_unique<SliderAttachment>(audioProcessor.apvts, "inputLevel", inputLevelSlider_);

    paramBtn_.onClick = [this]() { if (unitPowerOn_) { audioProcessor.pressParam(); repaint(); } };
    valUpBtn_.onClick = [this]() {
        if (!unitPowerOn_) return;
        bool shift = juce::ModifierKeys::currentModifiers.isShiftDown();
        audioProcessor.pressValueUp(shift);
        syncProgramKnobsFromProcessor();
        repaint();
    };
    valDownBtn_.onClick = [this]() {
        if (!unitPowerOn_) return;
        bool shift = juce::ModifierKeys::currentModifiers.isShiftDown();
        audioProcessor.pressValueDown(shift);
        syncProgramKnobsFromProcessor();
        repaint();
    };
    balanceBtn_.onClick = [this]() { if (unitPowerOn_) { audioProcessor.pressBalance(); repaint(); } };

    storeBtn_.onClick = [this]() { if (unitPowerOn_) { audioProcessor.pressStore(); repaint(); } };
    memUpBtn_.onClick = [this]() { if (unitPowerOn_) { audioProcessor.pressMemoryUp(); repaint(); } };
    memDownBtn_.onClick = [this]() { if (unitPowerOn_) { audioProcessor.pressMemoryDown(); repaint(); } };
    recallBtn_.onClick = [this]() {
        if (!unitPowerOn_) return;
        audioProcessor.pressRecall();
        syncProgramKnobsFromProcessor();
        repaint();
    };

    utilityBtn_.onClick = [this]() { if (unitPowerOn_) { audioProcessor.pressUtility(); repaint(); } };
    midiTrigBtn_.onClick = [this]() {
        if (!unitPowerOn_) return;
        audioProcessor.pressMidiTrigger();
        syncProgramKnobsFromProcessor();
        repaint();
    };
    bypassBtn_.onClick = [this]() { if (unitPowerOn_) { audioProcessor.pressBypass(); repaint(); } };

    for (auto* b : {&paramBtn_, &valUpBtn_, &valDownBtn_, &balanceBtn_,
                    &storeBtn_, &memUpBtn_, &memDownBtn_, &recallBtn_,
                    &utilityBtn_, &midiTrigBtn_, &bypassBtn_}) {
        addAndMakeVisible(*b);
    }

    modernViewToggleBtn_.setButtonText(juce::CharPointer_UTF8("STUDIO VIEW \xe2\x96\xbc"));
    modernViewToggleBtn_.onClick = [this]() { toggleModernView(); };
    addAndMakeVisible(modernViewToggleBtn_);

    // --- Studio View Controls ---
    for (int i = 0; i < aero::kTotalMemorySlots; ++i) {
        presetSelector_.addItem(audioProcessor.getProgramName(i), i + 1);
    }
    presetSelector_.setSelectedId(audioProcessor.getActiveSlotIndex() + 1, juce::dontSendNotification);
    presetSelector_.onChange = [this]() {
        int idx = presetSelector_.getSelectedId() - 1;
        if (idx >= 0 && idx < aero::kTotalMemorySlots) {
            audioProcessor.recallSlotDirectly(idx);
            syncProgramKnobsFromProcessor();
            repaint();
        }
    };
    addAndMakeVisible(presetSelector_);

    for (int i = 0; i < aero::kMaxProgramParams; ++i) {
        auto& s = progParamSliders_[static_cast<size_t>(i)];
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 18);
        s.onValueChange = [this, i]() {
            audioProcessor.setActiveProgramParamValue(
                i, static_cast<float>(progParamSliders_[static_cast<size_t>(i)].getValue()));
            repaint();
        };
        addAndMakeVisible(s);

        auto& lbl = progParamLabels_[static_cast<size_t>(i)];
        lbl.setJustificationType(juce::Justification::centred);
        lbl.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
        lbl.setColour(juce::Label::textColourId, juce::Colour(0xffdce6f0));
        addAndMakeVisible(lbl);
    }

    for (auto* sl : {&modernBalanceSlider_, &modernOutLevelSlider_}) {
        sl->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        sl->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
        sl->setTextValueSuffix(" %");
        addAndMakeVisible(*sl);
    }
    balanceAtt_ = std::make_unique<SliderAttachment>(audioProcessor.apvts, "balance", modernBalanceSlider_);
    outLevelAtt_ = std::make_unique<SliderAttachment>(audioProcessor.apvts, "outLevel", modernOutLevelSlider_);

    modernBalanceLabel_.setText("MIX BALANCE", juce::dontSendNotification);
    modernBalanceLabel_.setJustificationType(juce::Justification::centred);
    modernBalanceLabel_.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
    modernBalanceLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff00e2d6));
    addAndMakeVisible(modernBalanceLabel_);

    modernOutLevelLabel_.setText("OUTPUT LEVEL", juce::dontSendNotification);
    modernOutLevelLabel_.setJustificationType(juce::Justification::centred);
    modernOutLevelLabel_.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
    modernOutLevelLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff00e2d6));
    addAndMakeVisible(modernOutLevelLabel_);

    bitDepthCombo_.addItemList({"12-Bit Linear", "12-Bit Companded", "16-Bit Linear PCM"}, 1);
    addAndMakeVisible(bitDepthCombo_);
    bitDepthAtt_ = std::make_unique<ComboBoxAttachment>(audioProcessor.apvts, "bitMode", bitDepthCombo_);

    ramModelCombo_.addItemList({"SPX90 MkI (500 ms DRAM)", "SPX90II MkII (2000 ms DRAM)"}, 1);
    addAndMakeVisible(ramModelCombo_);
    ramModelAtt_ = std::make_unique<ComboBoxAttachment>(audioProcessor.apvts, "ramModel", ramModelCombo_);

    for (auto* sl : {&clockRateSlider_, &converterDriveSlider_, &noiseFloorSlider_}) {
        sl->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        sl->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
        addAndMakeVisible(*sl);
    }
    clockRateSlider_.setTextValueSuffix(" Hz");
    converterDriveSlider_.setTextValueSuffix(" dB");
    noiseFloorSlider_.setTextValueSuffix(" dB");

    clockRateAtt_ = std::make_unique<SliderAttachment>(audioProcessor.apvts, "clockRate", clockRateSlider_);
    converterDriveAtt_ = std::make_unique<SliderAttachment>(audioProcessor.apvts, "converterDrive", converterDriveSlider_);
    noiseFloorAtt_ = std::make_unique<SliderAttachment>(audioProcessor.apvts, "noiseFloor", noiseFloorSlider_);

    clockRateLabel_.setText("DSP CLOCK", juce::dontSendNotification);
    converterDriveLabel_.setText("ADC DRIVE", juce::dontSendNotification);
    noiseFloorLabel_.setText("NOISE FLOOR", juce::dontSendNotification);
    for (auto* l : {&clockRateLabel_, &converterDriveLabel_, &noiseFloorLabel_}) {
        l->setJustificationType(juce::Justification::centred);
        l->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        l->setColour(juce::Label::textColourId, juce::Colour(0xffb4d2dc));
        addAndMakeVisible(*l);
    }

    for (auto* tb : {&hiFiBypassToggle_, &multiChainToggle_, &chainModToggle_,
                     &chainPitchToggle_, &chainDelayToggle_, &chainReverbToggle_}) {
        tb->setColour(juce::ToggleButton::textColourId, juce::Colour(0xffe2eaf2));
        tb->setColour(juce::ToggleButton::tickColourId, juce::Colour(0xffa6ff1a));
        addAndMakeVisible(*tb);
    }

    hiFiBypassAtt_  = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "hiFiBypass", hiFiBypassToggle_);
    multiChainAtt_  = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "multiChain", multiChainToggle_);
    chainModAtt_    = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "chainMod", chainModToggle_);
    chainPitchAtt_  = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "chainPitch", chainPitchToggle_);
    chainDelayAtt_  = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "chainDelay", chainDelayToggle_);
    chainReverbAtt_ = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "chainReverb", chainReverbToggle_);

    setResizable(false, false);
    setSize(1760, 192);

    syncProgramKnobsFromProcessor();
    paramBtn_.setToggleState(true, juce::dontSendNotification);
    startTimerHz(60);

    // Optional automated UI snapshot export for visual inspection
    auto snapDir = juce::SystemStats::getEnvironmentVariable("AEROFX90_SNAPSHOT_DIR", "");
    if (snapDir.isNotEmpty()) {
        juce::File outDir(snapDir);
        outDir.createDirectory();

        // 1. Collapsed Ultra-Sleek 1U Rack View (1760 x 192)
        {
            juce::PNGImageFormat png;
            auto imgRack = createComponentSnapshot(getLocalBounds(), true, 1.5f);
            auto f = outDir.getChildFile("aero_fx90_rack_closed.png");
            f.deleteFile();
            juce::FileOutputStream fos(f);
            if (fos.openedOk()) png.writeImageToStream(imgRack, fos);
        }

        // 2. Central Display Window Close-Up (2x scale)
        {
            juce::PNGImageFormat png;
            auto imgDisp = createComponentSnapshot(juce::Rectangle<int>(254, 18, 728, 156), true, 2.0f);
            auto f = outDir.getChildFile("aero_fx90_display_closeup.png");
            f.deleteFile();
            juce::FileOutputStream fos(f);
            if (fos.openedOk()) png.writeImageToStream(imgDisp, fos);
        }

        // 3. Expanded Studio View (1760 x 582)
        {
            showModernView_ = true;
            drawerAnimProgress_ = 1.0f;
            modernViewToggleBtn_.setButtonText(juce::CharPointer_UTF8("STUDIO VIEW \xe2\x96\xb2"));
            setSize(1760, 582);

            juce::PNGImageFormat png;
            auto imgStudio = createComponentSnapshot(getLocalBounds(), true, 1.5f);
            auto f = outDir.getChildFile("aero_fx90_studio_open.png");
            f.deleteFile();
            juce::FileOutputStream fos(f);
            if (fos.openedOk()) png.writeImageToStream(imgStudio, fos);

            showModernView_ = false;
            drawerAnimProgress_ = 0.0f;
            modernViewToggleBtn_.setButtonText(juce::CharPointer_UTF8("STUDIO VIEW \xe2\x96\xbc"));
            setSize(1760, 192);
        }

        if (auto* app = juce::JUCEApplicationBase::getInstance()) {
            app->systemRequestedQuit();
        }
    }
}

AeroFX90AudioProcessorEditor::~AeroFX90AudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void AeroFX90AudioProcessorEditor::toggleModernView() {
    showModernView_ = !showModernView_;
    modernViewToggleBtn_.setButtonText(
        juce::CharPointer_UTF8(showModernView_ ? "STUDIO VIEW \xe2\x96\xb2" : "STUDIO VIEW \xe2\x96\xbc"));
}

void AeroFX90AudioProcessorEditor::syncProgramKnobsFromProcessor() {
    const auto prog = audioProcessor.getCurrentActiveProgramCopy();
    const auto& metas = audioProcessor.getCurrentParamMetas();
    const auto ramModel = static_cast<aero::RamModel>(
        std::clamp(static_cast<int>(audioProcessor.apvts.getRawParameterValue("ramModel")->load()), 0, 1));

    presetSelector_.setSelectedId(audioProcessor.getActiveSlotIndex() + 1, juce::dontSendNotification);

    for (int i = 0; i < aero::kMaxProgramParams; ++i) {
        const auto& m = metas[static_cast<size_t>(i)];
        auto& sl = progParamSliders_[static_cast<size_t>(i)];
        auto& lb = progParamLabels_[static_cast<size_t>(i)];

        if (m.active) {
            sl.setEnabled(true);
            sl.setAlpha(1.0f);
            sl.setRange(m.minVal, m.maxVal, m.step);
            sl.setValue(prog.params[static_cast<size_t>(i)], juce::dontSendNotification);
            sl.textFromValueFunction = [m, ramModel](double val) {
                return juce::String(aero::Spx90PresetDatabase::formatParamValue(static_cast<float>(val), m, ramModel));
            };
            sl.updateText();
            lb.setText(m.name, juce::dontSendNotification);
            lb.setAlpha(1.0f);
        } else {
            sl.setEnabled(false);
            sl.setAlpha(0.25f);
            sl.textFromValueFunction = [](double) { return juce::String("---"); };
            sl.updateText();
            lb.setText("---", juce::dontSendNotification);
            lb.setAlpha(0.35f);
        }
    }
    lastSyncedSlot_ = audioProcessor.getActiveSlotIndex();
    lastSyncedAlg_ = prog.algorithm;
}

void AeroFX90AudioProcessorEditor::timerCallback() {
    ++blinkCounter_;

    const float targetAnim = showModernView_ ? 1.0f : 0.0f;
    if (std::abs(drawerAnimProgress_ - targetAnim) > 0.001f) {
        const float step = 0.034f; // ~0.48s graceful glide at 60Hz
        if (drawerAnimProgress_ < targetAnim)
            drawerAnimProgress_ = std::min(1.0f, drawerAnimProgress_ + step);
        else
            drawerAnimProgress_ = std::max(0.0f, drawerAnimProgress_ - step);

        const float t = drawerAnimProgress_;
        const float eased = t * t * (3.0f - 2.0f * t);
        const int targetH = 192 + static_cast<int>(std::round(eased * 390.0f));
        if (getHeight() != targetH) {
            setSize(getWidth(), targetH);
        }
    }

    bool isBypass = audioProcessor.apvts.getRawParameterValue("bypass")->load() >= 0.5f;
    bypassBtn_.setToggleState(unitPowerOn_ && isBypass, juce::dontSendNotification);

    auto mode = audioProcessor.getPanelMode();
    paramBtn_.setToggleState(unitPowerOn_ &&
        (mode == AeroFX90AudioProcessor::PanelDisplayMode::ParamSelect ||
         mode == AeroFX90AudioProcessor::PanelDisplayMode::ProgramPlay),
        juce::dontSendNotification);

    const auto prog = audioProcessor.getCurrentActiveProgramCopy();
    if (audioProcessor.getActiveSlotIndex() != lastSyncedSlot_ || prog.algorithm != lastSyncedAlg_) {
        syncProgramKnobsFromProcessor();
    } else {
        for (int i = 0; i < aero::kMaxProgramParams; ++i) {
            if (!progParamSliders_[static_cast<size_t>(i)].isMouseButtonDown()) {
                progParamSliders_[static_cast<size_t>(i)].setValue(
                    prog.params[static_cast<size_t>(i)], juce::dontSendNotification);
                progParamSliders_[static_cast<size_t>(i)].updateText();
            }
        }
    }

    repaint();
}

void AeroFX90AudioProcessorEditor::resized() {
    const int totalW = getWidth();
    constexpr int kRackHeight = 192;
    constexpr int kDrawerFullHeight = 390;

    rackArea_ = juce::Rectangle<int>(0, 0, totalW, kRackHeight);
    modernArea_ = juce::Rectangle<int>(0, kRackHeight, totalW, kDrawerFullHeight);

    const int cy = rackArea_.getCentreY();

    // 1. Far Left: POWER Switch & INPUT Knob (Spaced across the sleek 1U left bay)
    powerBtn_.setBounds(rackArea_.getX() + 84, cy - 21, 40, 42);
    inputLevelSlider_.setBounds(rackArea_.getX() + 168, cy - 31, 62, 62);

    // 2. Center Recessed Gloss-Black Display Window (Wide Ultra-Sleek 1U Proportions)
    const int winX = rackArea_.getX() + 266;
    const int winH = 136;
    const int winY = cy - winH / 2;
    const int winW = 700;
    displayWindowArea_ = juce::Rectangle<float>(static_cast<float>(winX), static_cast<float>(winY),
                                                static_cast<float>(winW), static_cast<float>(winH));

    badgeArea_ = juce::Rectangle<float>(displayWindowArea_.getX() + 18.0f,
                                        displayWindowArea_.getY() + 24.0f,
                                        134.0f, 84.0f);

    meterArea_ = juce::Rectangle<float>(displayWindowArea_.getX() + 164.0f,
                                        displayWindowArea_.getY() + 14.0f,
                                        46.0f, 108.0f);

    sevenSegArea_ = juce::Rectangle<float>(displayWindowArea_.getX() + 220.0f,
                                           displayWindowArea_.getY() + 14.0f,
                                           98.0f, 108.0f);

    // Wide 16x2 Dot-Matrix LCD (~4.7 : 1 aspect ratio matching original hardware LCD module)
    lcdArea_ = juce::Rectangle<float>(displayWindowArea_.getX() + 330.0f,
                                      displayWindowArea_.getY() + 31.0f,
                                      352.0f, 74.0f);

    // 3. Right Staggered Cross Button Clusters (Exact SPX90 Layout)
    const int bw = 46;
    const int bhCenter = 50;
    const int bhStack = 48;
    const int gap = 2;

    // Cluster 1: [PARAMETER] | [UP / DOWN] | [BALANCE]
    const int c1X = winX + winW + 26;
    paramBtn_.setBounds(c1X, cy - bhCenter / 2, bw, bhCenter);
    valUpBtn_.setBounds(c1X + bw + gap, cy - bhStack - 1, bw, bhStack);
    valDownBtn_.setBounds(c1X + bw + gap, cy + 1, bw, bhStack);
    balanceBtn_.setBounds(c1X + (bw + gap) * 2, cy - bhCenter / 2, bw, bhCenter);

    // Cluster 2: [STORE] | [UP / DOWN] | [RECALL]
    const int c2X = c1X + (bw + gap) * 3 + 16;
    storeBtn_.setBounds(c2X, cy - bhCenter / 2, bw, bhCenter);
    memUpBtn_.setBounds(c2X + bw + gap, cy - bhStack - 1, bw, bhStack);
    memDownBtn_.setBounds(c2X + bw + gap, cy + 1, bw, bhStack);
    recallBtn_.setBounds(c2X + (bw + gap) * 2, cy - bhCenter / 2, bw, bhCenter);

    // Cluster 3: [UTILITY] | [FOOT TRIGGER / BYPASS]
    const int c3X = c2X + (bw + gap) * 3 + 16;
    utilityBtn_.setBounds(c3X, cy - bhCenter / 2, bw, bhCenter);
    midiTrigBtn_.setBounds(c3X + bw + gap, cy - bhStack - 1, bw + 6, bhStack);
    bypassBtn_.setBounds(c3X + bw + gap, cy + 1, bw + 6, bhStack);

    // 4. 1/4" Phone Jacks (MEMORY TRIGGER & BYPASS)
    const float jackX = static_cast<float>(c3X + bw + gap + bw + 6 + 38);
    topJackCenter_ = {jackX, static_cast<float>(midiTrigBtn_.getBounds().getCentreY())};
    botJackCenter_ = {jackX, static_cast<float>(bypassBtn_.getBounds().getCentreY())};

    // 5. Far-Right Manufacturer Lockup (.mfr) & Studio View Drawer Button down at bottom
    manufacturerBadgeArea_ = juce::Rectangle<float>(jackX + 76.0f, static_cast<float>(cy - 22), 180.0f, 32.0f);
    modernViewToggleBtn_.setBounds(static_cast<int>(manufacturerBadgeArea_.getCentreX() - 65.0f),
                                   kRackHeight - 30, 130, 22);

    // --- Studio Expansion Console View Layout ---
    const bool drawerVisible = (getHeight() > 194 || showModernView_ || drawerAnimProgress_ > 0.001f);
    for (auto* c : {static_cast<juce::Component*>(&presetSelector_),
                    static_cast<juce::Component*>(&modernBalanceSlider_),
                    static_cast<juce::Component*>(&modernBalanceLabel_),
                    static_cast<juce::Component*>(&modernOutLevelSlider_),
                    static_cast<juce::Component*>(&modernOutLevelLabel_),
                    static_cast<juce::Component*>(&bitDepthCombo_),
                    static_cast<juce::Component*>(&ramModelCombo_),
                    static_cast<juce::Component*>(&clockRateSlider_),
                    static_cast<juce::Component*>(&clockRateLabel_),
                    static_cast<juce::Component*>(&converterDriveSlider_),
                    static_cast<juce::Component*>(&converterDriveLabel_),
                    static_cast<juce::Component*>(&noiseFloorSlider_),
                    static_cast<juce::Component*>(&noiseFloorLabel_),
                    static_cast<juce::Component*>(&hiFiBypassToggle_),
                    static_cast<juce::Component*>(&multiChainToggle_),
                    static_cast<juce::Component*>(&chainModToggle_),
                    static_cast<juce::Component*>(&chainPitchToggle_),
                    static_cast<juce::Component*>(&chainDelayToggle_),
                    static_cast<juce::Component*>(&chainReverbToggle_)}) {
        c->setVisible(drawerVisible);
    }
    for (int i = 0; i < aero::kMaxProgramParams; ++i) {
        progParamSliders_[static_cast<size_t>(i)].setVisible(drawerVisible);
        progParamLabels_[static_cast<size_t>(i)].setVisible(drawerVisible);
    }

    auto modPad = modernArea_.reduced(24, 14);

    auto headerRow = modPad.removeFromTop(36);
    presetSelector_.setBounds(headerRow.getX() + 145, headerRow.getY() + 4, 290, 28);
    bitDepthCombo_.setBounds(headerRow.getX() + 585, headerRow.getY() + 4, 215, 28);
    ramModelCombo_.setBounds(headerRow.getX() + 945, headerRow.getY() + 4, 260, 28);

    modPad.removeFromTop(10);

    auto knobRow = modPad.removeFromTop(154);
    const int numCols = 10;
    const int colW = knobRow.getWidth() / numCols;

    for (int i = 0; i < aero::kMaxProgramParams; ++i) {
        auto cell = knobRow.removeFromLeft(colW).reduced(4, 4);
        progParamLabels_[static_cast<size_t>(i)].setBounds(cell.removeFromTop(20));
        progParamSliders_[static_cast<size_t>(i)].setBounds(cell);
    }

    auto balCell = knobRow.removeFromLeft(colW).reduced(4, 4);
    modernBalanceLabel_.setBounds(balCell.removeFromTop(20));
    modernBalanceSlider_.setBounds(balCell);

    auto outCell = knobRow.removeFromLeft(colW).reduced(4, 4);
    modernOutLevelLabel_.setBounds(outCell.removeFromTop(20));
    modernOutLevelSlider_.setBounds(outCell);

    modPad.removeFromTop(10);

    auto bottomRow = modPad;
    auto convBox = bottomRow.removeFromLeft(620).reduced(12, 6);
    convBox.removeFromTop(22);

    int cKnobW = 130;
    auto clkCell = convBox.removeFromLeft(cKnobW);
    clockRateLabel_.setBounds(clkCell.removeFromTop(18));
    clockRateSlider_.setBounds(clkCell);

    auto drvCell = convBox.removeFromLeft(cKnobW);
    converterDriveLabel_.setBounds(drvCell.removeFromTop(18));
    converterDriveSlider_.setBounds(drvCell);

    auto nseCell = convBox.removeFromLeft(cKnobW);
    noiseFloorLabel_.setBounds(nseCell.removeFromTop(18));
    noiseFloorSlider_.setBounds(nseCell);

    hiFiBypassToggle_.setBounds(convBox.getX() + 16, convBox.getY() + 38, 175, 28);

    bottomRow.removeFromLeft(16);
    auto chainBox = bottomRow.reduced(14, 6);
    chainBox.removeFromTop(22);

    multiChainToggle_.setBounds(chainBox.getX() + 10, chainBox.getY() + 4, 210, 26);

    int toggleY = chainBox.getY() + 38;
    int toggleW = (chainBox.getWidth() - 20) / 4;
    chainModToggle_.setBounds(chainBox.getX() + 10 + toggleW * 0, toggleY, toggleW - 6, 28);
    chainPitchToggle_.setBounds(chainBox.getX() + 10 + toggleW * 1, toggleY, toggleW - 6, 28);
    chainDelayToggle_.setBounds(chainBox.getX() + 10 + toggleW * 2, toggleY, toggleW - 6, 28);
    chainReverbToggle_.setBounds(chainBox.getX() + 10 + toggleW * 3, toggleY, toggleW - 6, 28);
}

void AeroFX90AudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff08090c));
    if (getHeight() > 194) {
        drawModernDrawerBackground(g, modernArea_);
        // Deep 3D shadow cast by the 1U rack unit onto the sliding Studio View drawer
        juce::ColourGradient lipShadow(juce::Colours::black.withAlpha(0.85f), 0.0f, 192.0f,
                                       juce::Colours::transparentBlack, 0.0f, 210.0f, false);
        g.setGradientFill(lipShadow);
        g.fillRect(0, 192, getWidth(), 18);
    }
    drawRackFaceplate(g, rackArea_);
}

void AeroFX90AudioProcessorEditor::drawRackFaceplate(juce::Graphics& g, juce::Rectangle<int> bounds) {
    auto fBounds = bounds.toFloat();

    // Rich Satin-Gloss Black Anodized 1U Rack Chassis with vertical sheen
    juce::ColourGradient chassisGrad(juce::Colour(0xff26252c), fBounds.getX(), fBounds.getY(),
                                     juce::Colour(0xff0e0d12), fBounds.getX(), fBounds.getBottom(), false);
    chassisGrad.addColour(0.14, juce::Colour(0xff2c2b33)); // Upper specular roll
    chassisGrad.addColour(0.50, juce::Colour(0xff18171d));
    chassisGrad.addColour(0.88, juce::Colour(0xff121116));
    g.setGradientFill(chassisGrad);
    g.fillRect(fBounds);

    // Subtle horizontal radial highlight across center of faceplate
    juce::ColourGradient centerSheen(juce::Colours::white.withAlpha(0.0f), fBounds.getX(), fBounds.getCentreY(),
                                     juce::Colours::white.withAlpha(0.0f), fBounds.getRight(), fBounds.getCentreY(), false);
    centerSheen.addColour(0.35, juce::Colours::white.withAlpha(0.035f));
    centerSheen.addColour(0.65, juce::Colours::white.withAlpha(0.025f));
    g.setGradientFill(centerSheen);
    g.fillRect(fBounds);

    // Fine anodized brushed metal grain
    g.setColour(juce::Colours::white.withAlpha(0.014f));
    for (int y = bounds.getY(); y < bounds.getBottom(); y += 2) {
        g.drawHorizontalLine(y, fBounds.getX(), fBounds.getRight());
    }

    // Top specular chrome/anodized bevel edge & bottom shadow edge
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.drawHorizontalLine(bounds.getY() + 1, fBounds.getX(), fBounds.getRight());
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.drawHorizontalLine(bounds.getBottom() - 1, fBounds.getX(), fBounds.getRight());

    // Left & Right 1U Rack Ears with beveled mounting cutouts
    auto drawRackCutouts = [&g](float xCenter, float topY, float botY) {
        for (float ry : {topY + 13.0f, botY - 35.0f}) {
            juce::Rectangle<float> hole(xCenter - 17.0f, ry, 34.0f, 22.0f);
            g.setColour(juce::Colour(0xff050507));
            g.fillRoundedRectangle(hole.expanded(1.5f), 12.0f);
            juce::ColourGradient slotGrad(juce::Colour(0xfff4f6f8), hole.getX(), hole.getY(),
                                          juce::Colour(0xffcfd4dc), hole.getX(), hole.getBottom(), false);
            g.setGradientFill(slotGrad);
            g.fillRoundedRectangle(hole, 11.0f);
            g.setColour(juce::Colour(0xff4a4e58));
            g.drawRoundedRectangle(hole, 11.0f, 1.5f);
        }
    };
    drawRackCutouts(fBounds.getX() + 26.0f, fBounds.getY(), fBounds.getBottom());
    drawRackCutouts(fBounds.getRight() - 26.0f, fBounds.getY(), fBounds.getBottom());

    // POWER silkscreen legends (POWER above, ON/OFF legend below)
    g.setColour(juce::Colour(0xffedf2f7));
    g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
    g.drawText("POWER", powerBtn_.getX() - 8, powerBtn_.getY() - 17,
               powerBtn_.getWidth() + 16, 13, juce::Justification::centred);

    int pwrLegY = powerBtn_.getBottom() + 6;
    g.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
    g.drawRect(powerBtn_.getX() - 4, pwrLegY + 4, 7, 4, 1);
    g.drawText("ON/", powerBtn_.getX() + 5, pwrLegY, 24, 12, juce::Justification::centredLeft);
    g.fillRect(powerBtn_.getX() + 27, pwrLegY + 2, 6, 6);
    g.drawText("OFF", powerBtn_.getX() + 35, pwrLegY, 24, 12, juce::Justification::centredLeft);

    // INPUT silkscreen legend above & 0 / 10 below knob
    g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
    g.drawText("INPUT", inputLevelSlider_.getX(), inputLevelSlider_.getY() - 18,
               inputLevelSlider_.getWidth(), 13, juce::Justification::centred);
    g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    g.drawText("0", inputLevelSlider_.getX() + 6, inputLevelSlider_.getBottom() - 2, 16, 12, juce::Justification::centred);
    g.drawText("10", inputLevelSlider_.getRight() - 22, inputLevelSlider_.getBottom() - 2, 20, 12, juce::Justification::centred);

    // =========================================================================
    // Center Recessed Glossy Smoked-Acrylic Display Window
    // =========================================================================
    // Outer chamfered bezel shadow & metallic rim
    g.setColour(juce::Colour(0xff020304));
    g.fillRoundedRectangle(displayWindowArea_.expanded(3.0f), 4.0f);

    juce::ColourGradient glassBg(juce::Colour(0xff0b0d12), displayWindowArea_.getX(), displayWindowArea_.getY(),
                                 juce::Colour(0xff030406), displayWindowArea_.getX(), displayWindowArea_.getBottom(), false);
    glassBg.addColour(0.5, juce::Colour(0xff06080b));
    g.setGradientFill(glassBg);
    g.fillRoundedRectangle(displayWindowArea_, 3.0f);

    // 1. Inside Window Left: AeroVerb / SFX90 Badge
    drawAeroVerbSfx90Badge(g, badgeArea_);

    // 2. Inside Window: 7-Bar Horizontal LED Meter Ladder (0, -3, -6, -10, -15, -20, -30)
    drawLedMeterLadder(g, meterArea_, audioProcessor.getPeakMeterLevel(),
                       audioProcessor.getOverflowLed(), unitPowerOn_);

    // 3. Inside Window: Slanted Red 7-Segment MEMORY Readout
    const bool isPending = audioProcessor.isPreviewPending();
    const bool flashOff = isPending && ((blinkCounter_ / 12) % 2 == 1);
    const int displaySlotNum = (isPending ? audioProcessor.getPreviewSlotIndex()
                                          : audioProcessor.getActiveSlotIndex()) + 1;
    drawSevenSegmentDisplay(g, sevenSegArea_, displaySlotNum, flashOff, unitPowerOn_);

    // 4. Inside Window Right: True 5x7 Pixel Bitmap Dot-Matrix LCD
    std::string line1, line2;
    audioProcessor.getLcdLines(line1, line2);
    drawDotMatrixLcd(g, lcdArea_, line1, line2, unitPowerOn_);

    // 5. Smoked-Acrylic Specular Glass Reflection Overlay across the Display Window
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(displayWindowArea_.toNearestInt());

        // Top glossy acrylic reflection band + diagonal light sweep
        juce::Path glassSweep;
        glassSweep.startNewSubPath(displayWindowArea_.getX(), displayWindowArea_.getY());
        glassSweep.lineTo(displayWindowArea_.getRight(), displayWindowArea_.getY());
        glassSweep.lineTo(displayWindowArea_.getRight() - 95.0f, displayWindowArea_.getY() + 52.0f);
        glassSweep.lineTo(displayWindowArea_.getX(), displayWindowArea_.getY() + 26.0f);
        glassSweep.closeSubPath();

        juce::ColourGradient sweepGrad(juce::Colours::white.withAlpha(0.085f),
                                       displayWindowArea_.getX(), displayWindowArea_.getY(),
                                       juce::Colours::white.withAlpha(0.0f),
                                       displayWindowArea_.getX(), displayWindowArea_.getY() + 54.0f, false);
        g.setGradientFill(sweepGrad);
        g.fillPath(glassSweep);

        // Diagonal subtle glass streak on left and right
        juce::Path streak;
        float sx = displayWindowArea_.getX() + 185.0f;
        streak.startNewSubPath(sx, displayWindowArea_.getY());
        streak.lineTo(sx + 38.0f, displayWindowArea_.getY());
        streak.lineTo(sx - 24.0f, displayWindowArea_.getBottom());
        streak.lineTo(sx - 62.0f, displayWindowArea_.getBottom());
        streak.closeSubPath();
        g.setColour(juce::Colours::white.withAlpha(0.03f));
        g.fillPath(streak);
    }

    // Inner specular rim on smoked-glass window
    g.setColour(juce::Colours::white.withAlpha(0.16f));
    g.drawHorizontalLine(static_cast<int>(displayWindowArea_.getY() + 1.0f),
                         displayWindowArea_.getX() + 2.0f, displayWindowArea_.getRight() - 2.0f);
    g.setColour(juce::Colour(0xff383d46));
    g.drawRoundedRectangle(displayWindowArea_, 3.0f, 1.5f);

    // 6. Far Right 1/4" Phone Jacks (MEMORY TRIGGER & BYPASS)
    float lineStartX = static_cast<float>(midiTrigBtn_.getRight() + 4);
    drawQuarterInchJack(g, topJackCenter_, "MEMORY\nTRIGGER", lineStartX);
    drawQuarterInchJack(g, botJackCenter_, "BYPASS", lineStartX);

    // 7. Far Right Recessed Metallic & Smoked-Enamel "Digital Symphony Instruments" Hardware Badge
    drawManufacturerBadge(g, manufacturerBadgeArea_);
}

void AeroFX90AudioProcessorEditor::drawManufacturerBadge(juce::Graphics& g, juce::Rectangle<float> area) {
    // Exact reproduction of the .mfr HTML/SVG/CSS lockup:
    // - Color: #d6d0c2
    // - Filter: drop-shadow(0 1px 0 rgba(255,255,255,.16)) drop-shadow(0 -1px 0 rgba(0,0,0,.7))
    // - .dsmark SVG: 32x32 (viewBox 0 0 64 64), opacity 0.5
    // - .rule: 1x28px, opacity 0.35
    // - .txt b: 600 10.5px 'Jura', letter-spacing 0.2em, margin-right -0.2em ("DIGITAL SYMPHONY")
    // - .txt i: 400 8.5px 'Jura', 11 spans ("I".."S") justify-content: space-between, opacity 0.85
    const juce::Colour baseCol(0xffd6d0c2);
    const float mfrX = area.getX();
    const float mfrY = area.getY();
    const float s = 32.0f / 64.0f; // 0.5f scale from viewBox 0 0 64 64 to 32x32px

    // 1. SVG .dsmark (width: 32px; height: 32px; opacity: 0.5)
    auto drawDsMarkPass = [&](float dx, float dy, juce::Colour col) {
        g.setColour(col);
        const float ox = mfrX + dx;
        const float oy = mfrY + dy;

        // <rect x="5" y="9" width="54" height="46" rx="6" stroke-width="2.4"/>
        juce::Path rectPath;
        rectPath.addRoundedRectangle(ox + 5.0f * s, oy + 9.0f * s, 54.0f * s, 46.0f * s, 6.0f * s);
        g.strokePath(rectPath, juce::PathStrokeType(2.4f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // <path d="M19 9V55M32 9V55M45 9V55" stroke-width="2"/>
        juce::Path linesPath;
        linesPath.startNewSubPath(ox + 19.0f * s, oy + 34.0f * s);
        linesPath.lineTo(ox + 19.0f * s, oy + 55.0f * s);
        linesPath.startNewSubPath(ox + 32.0f * s, oy + 9.0f * s);
        linesPath.lineTo(ox + 32.0f * s, oy + 55.0f * s);
        linesPath.startNewSubPath(ox + 45.0f * s, oy + 34.0f * s);
        linesPath.lineTo(ox + 45.0f * s, oy + 55.0f * s);
        g.strokePath(linesPath, juce::PathStrokeType(2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // <path d="M15.5 9V34H22.5V9Z M41.5 9V34H48.5V9Z" fill="currentColor" stroke="none"/>
        juce::Path blackKeys;
        blackKeys.addRectangle(ox + 15.5f * s, oy + 9.0f * s, 7.0f * s, 25.0f * s);
        blackKeys.addRectangle(ox + 41.5f * s, oy + 9.0f * s, 7.0f * s, 25.0f * s);
        g.fillPath(blackKeys);
    };

    constexpr float kIconAlpha = 0.50f;
    drawDsMarkPass(0.0f, -1.0f, juce::Colours::black.withAlpha(0.70f * kIconAlpha));
    drawDsMarkPass(0.0f,  1.0f, juce::Colours::white.withAlpha(0.16f * kIconAlpha));
    drawDsMarkPass(0.0f,  0.0f, baseCol.withAlpha(kIconAlpha));

    // 2. Vertical Rule .rule (gap: 11px; width: 1px; height: 28px; opacity: 0.35)
    const float ruleX = mfrX + 32.0f + 11.0f;
    const float ruleY = mfrY + (32.0f - 28.0f) * 0.5f;
    constexpr float kRuleAlpha = 0.35f;
    g.setColour(juce::Colours::black.withAlpha(0.70f * kRuleAlpha));
    g.fillRect(ruleX, ruleY - 1.0f, 1.0f, 28.0f);
    g.setColour(juce::Colours::white.withAlpha(0.16f * kRuleAlpha));
    g.fillRect(ruleX, ruleY + 1.0f, 1.0f, 28.0f);
    g.setColour(baseCol.withAlpha(kRuleAlpha));
    g.fillRect(ruleX, ruleY, 1.0f, 28.0f);

    // 3. Text Block .txt (gap: 11px; flex-direction: column; gap: 3px; text-transform: uppercase)
    const float txtX = ruleX + 1.0f + 11.0f;
    juce::Font fontB = juraSemiBoldTypeface_ != nullptr
        ? juce::Font(juce::FontOptions(juraSemiBoldTypeface_).withHeight(10.5f * 1.19f))
        : juce::Font(juce::FontOptions(10.5f * 1.19f, juce::Font::bold));
    juce::Font fontI = juraRegularTypeface_ != nullptr
        ? juce::Font(juce::FontOptions(juraRegularTypeface_).withHeight(8.5f * 1.19f))
        : juce::Font(juce::FontOptions(8.5f * 1.19f, juce::Font::plain));

    const float line1BaselineY = mfrY + 13.0f;
    const float line2BaselineY = mfrY + 24.5f;

    // Line 1: <b>DIGITAL SYMPHONY</b> (font: 600 10.5px 'Jura'; letter-spacing: .2em; margin-right: -.2em)
    const char* line1Str = "DIGITAL SYMPHONY";
    constexpr int kLine1Len = 16;
    constexpr float kLetterSpacingB = 0.2f * 10.5f; // 2.1px

    juce::GlyphArrangement gaB;
    float curX = txtX;
    for (int i = 0; i < kLine1Len; ++i) {
        juce::String chStr = juce::String::charToString(static_cast<juce::juce_wchar>(line1Str[i]));
        gaB.addLineOfText(fontB, chStr, curX, line1BaselineY);
        curX += fontB.getStringWidthFloat(chStr) + ((i < kLine1Len - 1) ? kLetterSpacingB : 0.0f);
    }

    auto bBounds = gaB.getBoundingBox(0, -1, true);
    const float exactLeftEdge = bBounds.getX();
    const float exactRightEdge = bBounds.getRight();

    // Line 2: <i><span>I</span>...<span>S</span></i> (font: 400 8.5px 'Jura'; justify-content: space-between; opacity: .85)
    const char* line2Str = "INSTRUMENTS";
    constexpr int kLine2Len = 11;
    std::array<float, kLine2Len> spanAdv{};
    float sumSpanAdv = 0.0f;
    for (int i = 0; i < kLine2Len; ++i) {
        juce::String chStr = juce::String::charToString(static_cast<juce::juce_wchar>(line2Str[i]));
        spanAdv[static_cast<size_t>(i)] = fontI.getStringWidthFloat(chStr);
        sumSpanAdv += spanAdv[static_cast<size_t>(i)];
    }

    juce::GlyphArrangement gaFirst, gaLast;
    gaFirst.addLineOfText(fontI, "I", 0.0f, line2BaselineY);
    gaLast.addLineOfText(fontI, "S", 0.0f, line2BaselineY);
    const float firstLeftInk = gaFirst.getBoundingBox(0, -1, true).getX();
    const float lastRightInk = gaLast.getBoundingBox(0, -1, true).getRight();

    const float startSpanX = exactLeftEdge - firstLeftInk;
    const float endSpanX = exactRightEdge - lastRightInk + spanAdv[kLine2Len - 1];
    const float flexContainerW = endSpanX - startSpanX;
    const float spaceBetweenGap = (flexContainerW - sumSpanAdv) / static_cast<float>(kLine2Len - 1);

    juce::GlyphArrangement gaI;
    float spanX = startSpanX;
    for (int i = 0; i < kLine2Len; ++i) {
        juce::String chStr = juce::String::charToString(static_cast<juce::juce_wchar>(line2Str[i]));
        gaI.addLineOfText(fontI, chStr, spanX, line2BaselineY);
        spanX += spanAdv[static_cast<size_t>(i)] + spaceBetweenGap;
    }

    juce::Path pathB, pathI;
    gaB.createPath(pathB);
    gaI.createPath(pathI);

    auto drawEngravedPath = [&](const juce::Path& p, float elemAlpha) {
        g.setColour(juce::Colours::black.withAlpha(0.70f * elemAlpha));
        g.fillPath(p, juce::AffineTransform::translation(0.0f, -1.0f));
        g.setColour(juce::Colours::white.withAlpha(0.16f * elemAlpha));
        g.fillPath(p, juce::AffineTransform::translation(0.0f, 1.0f));
        g.setColour(baseCol.withAlpha(elemAlpha));
        g.fillPath(p);
    };

    drawEngravedPath(pathB, 1.00f);
    drawEngravedPath(pathI, 0.85f);
}

void AeroFX90AudioProcessorEditor::drawAeroVerbSfx90Badge(juce::Graphics& g, juce::Rectangle<float> area) {
    // Top bold condensed white "AeroVerb" with subtle drop shadow
    g.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)).withHorizontalScale(0.88f));
    g.setColour(juce::Colours::black.withAlpha(0.7f));
    g.drawText("AeroVerb", static_cast<int>(area.getX() + 1), static_cast<int>(area.getY() + 7),
               static_cast<int>(area.getWidth()), 26, juce::Justification::centred);
    g.setColour(juce::Colour(0xfff4f8fc));
    g.drawText("AeroVerb", static_cast<int>(area.getX()), static_cast<int>(area.getY() + 6),
               static_cast<int>(area.getWidth()), 26, juce::Justification::centred);

    // Bottom cyan-teal double-line outlined geometric "SFX90" with subtle retro-fi neon bloom
    juce::Rectangle<float> sfxRect(area.getX(), area.getY() + 34.0f, area.getWidth(), 36.0f);
    juce::GlyphArrangement ga;
    auto sfxFont = juce::Font(juce::FontOptions(27.0f, juce::Font::bold)).withHorizontalScale(1.06f);
    ga.addFittedText(sfxFont, "SFX90", sfxRect.getX(), sfxRect.getY(),
                     sfxRect.getWidth(), sfxRect.getHeight(), juce::Justification::centred, 1);

    juce::Path textPath;
    ga.createPath(textPath);

    // Soft cyan-teal outer glow
    g.setColour(juce::Colour(0xff00d8ce).withAlpha(0.22f));
    g.strokePath(textPath, juce::PathStrokeType(4.5f));

    // Double-line retro geometric stroke
    g.setColour(juce::Colour(0xff00c8be));
    g.strokePath(textPath, juce::PathStrokeType(2.3f));
    g.setColour(juce::Colour(0xff05070a));
    g.fillPath(textPath);
    g.setColour(juce::Colour(0xff40f2e8));
    g.strokePath(textPath, juce::PathStrokeType(0.9f));
}

void AeroFX90AudioProcessorEditor::drawSevenSegmentDisplay(juce::Graphics& g,
                                                           juce::Rectangle<float> area,
                                                           int number,
                                                           bool flashOff,
                                                           bool powerOn) {
    int clamped = std::clamp(number, 1, 99);
    int tens = clamped / 10;
    int ones = clamped % 10;

    auto digitsArea = area.withTrimmedTop(12.0f).withTrimmedBottom(26.0f);
    float halfW = digitsArea.getWidth() * 0.5f;

    auto leftDigitRect  = digitsArea.removeFromLeft(halfW).reduced(5.0f, 4.0f);
    auto rightDigitRect = digitsArea.reduced(5.0f, 4.0f);

    drawSlantedSevenSegmentDigit(g, leftDigitRect, tens, powerOn && !flashOff && (tens > 0), true);
    drawSlantedSevenSegmentDigit(g, rightDigitRect, ones, powerOn && !flashOff, false);

    g.setColour(juce::Colour(0xffe6ecf2));
    g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
    g.drawText("MEMORY", static_cast<int>(area.getX() + 2), static_cast<int>(area.getBottom() - 16),
               64, 12, juce::Justification::centredLeft);
}

void AeroFX90AudioProcessorEditor::drawSlantedSevenSegmentDigit(juce::Graphics& g,
                                                                juce::Rectangle<float> r,
                                                                int digit,
                                                                bool lit,
                                                                bool /*blankIfOff*/) {
    static constexpr uint8_t kSegTable[10] = {
        0b0111111, // 0
        0b0000110, // 1
        0b1011011, // 2
        0b1001111, // 3
        0b1100110, // 4
        0b1101101, // 5
        0b1111101, // 6
        0b0000111, // 7
        0b1111111, // 8
        0b1101111  // 9
    };
    uint8_t mask = (lit && digit >= 0 && digit <= 9) ? kSegTable[digit] : 0;

    const float x = r.getX();
    const float y = r.getY();
    const float w = r.getWidth();
    const float h = r.getHeight();
    const float t = 4.0f;

    std::array<juce::Rectangle<float>, 7> segs = {{
        {x + t,         y,              w - 2 * t, t},
        {x + w - t,     y + t,          t,         h * 0.5f - t},
        {x + w - t,     y + h * 0.5f,   t,         h * 0.5f - t},
        {x + t,         y + h - t,      w - 2 * t, t},
        {x,             y + h * 0.5f,   t,         h * 0.5f - t},
        {x,             y + t,          t,         h * 0.5f - t},
        {x + t,         y + h * 0.5f - t * 0.5f, w - 2 * t, t}
    }};

    auto slant = juce::AffineTransform::translation(-r.getCentreX(), -r.getCentreY())
                     .sheared(-0.12f, 0.0f)
                     .translated(r.getCentreX(), r.getCentreY());

    for (size_t i = 0; i < 7; ++i) {
        bool isOn = (mask & (1u << i)) != 0;
        juce::Path p;
        p.addRoundedRectangle(segs[i], 1.2f);
        p.applyTransform(slant);

        if (isOn) {
            // Soft red LED bloom behind lit segment
            g.setColour(juce::Colour(0xffff1818).withAlpha(0.38f));
            g.strokePath(p, juce::PathStrokeType(3.5f));
            g.setColour(juce::Colour(0xffff2b1e));
            g.fillPath(p);
            g.setColour(juce::Colour(0xffff9a90).withAlpha(0.65f));
            g.strokePath(p, juce::PathStrokeType(0.7f));
        } else {
            g.setColour(juce::Colour(0xff1a0709));
            g.fillPath(p);
        }
    }
}

void AeroFX90AudioProcessorEditor::drawDotMatrixLcd(juce::Graphics& g,
                                                    juce::Rectangle<float> lcdBounds,
                                                    const std::string& line1,
                                                    const std::string& line2,
                                                    bool powerOn) {
    // Recessed dark bezel around the LCD glass module
    g.setColour(juce::Colour(0xff040802));
    g.fillRoundedRectangle(lcdBounds.expanded(2.0f), 3.0f);

    // Rich negative olive-black backlit LCD screen gradient (with subtle center backlight glow)
    if (powerOn) {
        juce::ColourGradient lcdBg(juce::Colour(0xff0b1c04), lcdBounds.getX(), lcdBounds.getY(),
                                   juce::Colour(0xff091703), lcdBounds.getRight(), lcdBounds.getBottom(), false);
        lcdBg.addColour(0.55, juce::Colour(0xff122806));
        g.setGradientFill(lcdBg);
    } else {
        g.setColour(juce::Colour(0xff121512));
    }
    g.fillRoundedRectangle(lcdBounds, 2.5f);

    auto inner = lcdBounds.reduced(8.0f, 6.0f);
    const float cellW = inner.getWidth() / 16.0f;
    const float rowH  = inner.getHeight() * 0.5f;

    const juce::Colour unlitDotCol  = powerOn ? juce::Colour(0xff1f3d0c) : juce::Colour(0xff1a1d1a);
    const juce::Colour cursorRowCol = powerOn ? juce::Colour(0xff2d5413) : juce::Colour(0xff1a1d1a);
    const juce::Colour litDotCol    = juce::Colour(0xffadff1f);
    const juce::Colour litCoreCol   = juce::Colour(0xffd8ff7a);
    const juce::Colour litGlowCol   = juce::Colour(0xff84ff00).withAlpha(0.32f);

    // Enforce square dot pitch so every pixel in the 5x7 matrix is a crisp square
    const float dotPitch = std::min((cellW * 0.85f) / 5.0f, (rowH * 0.88f) / 8.4f);
    const float charAreaW = dotPitch * 5.0f;
    const float charAreaH = dotPitch * 8.2f;
    const float dotSize   = std::max(1.4f, dotPitch * 0.76f);

    auto renderRow = [&](const std::string& text, int rowIdx) {
        const float baseY = inner.getY() + rowIdx * rowH + (rowH - charAreaH) * 0.5f;
        for (int col = 0; col < 16; ++col) {
            const float baseX = inner.getX() + col * cellW + (cellW - charAreaW) * 0.5f;
            char ch = (col < static_cast<int>(text.size())) ? text[static_cast<size_t>(col)] : ' ';
            Glyph5x7 glyph = get5x7Glyph(ch);

            for (int r = 0; r < 7; ++r) {
                uint8_t rowBits = glyph.rows[r];
                float py = baseY + r * dotPitch;
                for (int dx = 0; dx < 5; ++dx) {
                    float px = baseX + dx * dotPitch;
                    bool isLit = powerOn && ((rowBits & (1u << (4 - dx))) != 0);
                    juce::Rectangle<float> dotRect(px, py, dotSize, dotSize);
                    if (isLit) {
                        g.setColour(litGlowCol);
                        g.fillRect(dotRect.expanded(0.75f));
                        g.setColour(litDotCol);
                        g.fillRect(dotRect);
                        g.setColour(litCoreCol);
                        g.fillRect(dotRect.reduced(0.45f));
                    } else {
                        g.setColour(unlitDotCol);
                        g.fillRect(dotRect);
                    }
                }
            }

            // 8th cursor underline dot row beneath each character cell
            float cursorY = baseY + 7.35f * dotPitch;
            g.setColour(cursorRowCol);
            for (int dx = 0; dx < 5; ++dx) {
                float px = baseX + dx * dotPitch;
                g.fillRect(px, cursorY, dotSize, dotSize * 0.70f);
            }
        }
    };

    renderRow(line1, 0);
    renderRow(line2, 1);

    // Glossy glass reflection overlay on the LCD module itself (matching close-up photo sheen!)
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(lcdBounds.toNearestInt());
        juce::Path lcdGloss;
        lcdGloss.startNewSubPath(lcdBounds.getRight() - 95.0f, lcdBounds.getY());
        lcdGloss.lineTo(lcdBounds.getRight(), lcdBounds.getY());
        lcdGloss.lineTo(lcdBounds.getRight(), lcdBounds.getBottom());
        lcdGloss.lineTo(lcdBounds.getRight() - 130.0f, lcdBounds.getBottom());
        lcdGloss.closeSubPath();

        juce::ColourGradient glossGrad(juce::Colours::white.withAlpha(0.075f),
                                       lcdBounds.getRight() - 110.0f, lcdBounds.getY(),
                                       juce::Colours::white.withAlpha(0.01f),
                                       lcdBounds.getRight(), lcdBounds.getBottom(), false);
        g.setGradientFill(glossGrad);
        g.fillPath(lcdGloss);
    }

    // Inner bevel shadow & outer metallic glass rim
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.drawRoundedRectangle(lcdBounds.reduced(0.8f), 2.5f, 1.5f);
    g.setColour(juce::Colour(0xff344e22));
    g.drawRoundedRectangle(lcdBounds, 2.5f, 1.2f);
}

void AeroFX90AudioProcessorEditor::drawLedMeterLadder(juce::Graphics& g,
                                                      juce::Rectangle<float> meterBounds,
                                                      float peakLevel,
                                                      bool overflow,
                                                      bool powerOn) {
    static const char* kLabels[7] = {"0", "-3", "-6", "-10", "-15", "-20", "-30"};
    static constexpr float kThresholds[7] = {0.92f, 0.70f, 0.50f, 0.31f, 0.17f, 0.09f, 0.02f};

    const float rowH = meterBounds.getHeight() / 7.0f;
    g.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));

    for (int i = 0; i < 7; ++i) {
        float y = meterBounds.getY() + i * rowH;
        bool isLit = powerOn && ((i == 0) ? (overflow || peakLevel >= kThresholds[0])
                                          : (peakLevel >= kThresholds[i]));

        juce::Colour onCol = (i == 0) ? juce::Colour(0xffff2626)
                           : (i == 1) ? juce::Colour(0xffffa822)
                                      : juce::Colour(0xff18f028);
        juce::Colour offCol = juce::Colour(0xff383b40);

        juce::Rectangle<float> barRect(meterBounds.getX(), y + 3.0f, 16.0f, rowH - 6.0f);
        if (isLit) {
            g.setColour(onCol.withAlpha(0.35f));
            g.fillRoundedRectangle(barRect.expanded(2.0f), 1.5f);
            g.setColour(onCol);
            g.fillRect(barRect);
            g.setColour(juce::Colours::white.withAlpha(0.45f));
            g.fillRect(barRect.reduced(3.0f, 2.0f));
        } else {
            g.setColour(offCol);
            g.fillRect(barRect);
        }

        g.setColour(juce::Colour(0xffe4eaf0));
        g.drawText(kLabels[i], static_cast<int>(meterBounds.getX() + 19.0f), static_cast<int>(y),
                   24, static_cast<int>(rowH), juce::Justification::centredLeft);
    }
}

void AeroFX90AudioProcessorEditor::drawQuarterInchJack(juce::Graphics& g,
                                                       juce::Point<float> center,
                                                       const juce::String& label,
                                                       float lineStartX) {
    g.setColour(juce::Colour(0xffe0e4e8));
    g.drawLine(lineStartX, center.y, center.x - 16.0f, center.y, 1.3f);

    const float outerR = 12.5f;
    // Drop shadow
    g.setColour(juce::Colours::black.withAlpha(0.7f));
    g.fillEllipse(center.x - outerR + 1.0f, center.y - outerR + 2.0f, outerR * 2.0f, outerR * 2.0f);

    // Glossy chrome bushing gradient
    juce::ColourGradient chromeGrad(juce::Colour(0xffe8eef4), center.x - outerR, center.y - outerR,
                                    juce::Colour(0xff606870), center.x + outerR, center.y + outerR, false);
    chromeGrad.addColour(0.5, juce::Colour(0xffa4acb6));
    g.setGradientFill(chromeGrad);
    g.fillEllipse(center.x - outerR, center.y - outerR, outerR * 2.0f, outerR * 2.0f);

    g.setColour(juce::Colour(0xfff0f4f8));
    g.drawEllipse(center.x - outerR, center.y - outerR, outerR * 2.0f, outerR * 2.0f, 1.2f);

    const float innerR = 7.5f;
    g.setColour(juce::Colour(0xff060709));
    g.fillEllipse(center.x - innerR, center.y - innerR, innerR * 2.0f, innerR * 2.0f);

    g.setColour(juce::Colour(0xffe8ecf0));
    g.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
    g.drawFittedText(label, static_cast<int>(center.x + outerR + 6.0f),
                     static_cast<int>(center.y - 12.0f), 52, 24,
                     juce::Justification::centredLeft, 2);
}

void AeroFX90AudioProcessorEditor::drawModernDrawerBackground(juce::Graphics& g,
                                                              juce::Rectangle<int> drawerBounds) {
    auto b = drawerBounds.toFloat();

    // Cohesive Studio Retro-Fi Satin-Gloss Anodized Console Background
    juce::ColourGradient bgGrad(juce::Colour(0xff171820), b.getX(), b.getY(),
                                juce::Colour(0xff0b0c10), b.getX(), b.getBottom(), false);
    bgGrad.addColour(0.5, juce::Colour(0xff111218));
    g.setGradientFill(bgGrad);
    g.fillRect(b);

    // Fine brushed metal lines matching 1U rack
    g.setColour(juce::Colours::white.withAlpha(0.01f));
    for (int y = drawerBounds.getY(); y < drawerBounds.getBottom(); y += 2) {
        g.drawHorizontalLine(y, b.getX(), b.getRight());
    }

    auto pad = drawerBounds.reduced(22, 12).toFloat();

    // Helper to draw a glossy smoked-acrylic retro-fi module bay
    auto drawGlossyBay = [&g](juce::Rectangle<float> rect) {
        g.setColour(juce::Colour(0xff040507));
        g.fillRoundedRectangle(rect.expanded(1.5f), 6.0f);

        juce::ColourGradient bayGrad(juce::Colour(0xff1b1f28), rect.getX(), rect.getY(),
                                     juce::Colour(0xff0d1015), rect.getX(), rect.getBottom(), false);
        bayGrad.addColour(0.4, juce::Colour(0xff13171e));
        g.setGradientFill(bayGrad);
        g.fillRoundedRectangle(rect, 5.0f);

        // Top glossy sheen band
        auto topSheen = rect.withHeight(rect.getHeight() * 0.32f).reduced(1.0f);
        juce::ColourGradient sheenGrad(juce::Colours::white.withAlpha(0.06f), topSheen.getX(), topSheen.getY(),
                                       juce::Colours::white.withAlpha(0.0f), topSheen.getX(), topSheen.getBottom(), false);
        g.setGradientFill(sheenGrad);
        g.fillRoundedRectangle(topSheen, 4.0f);

        // Cyan-tinted specular rim
        g.setColour(juce::Colour(0xff00c8be).withAlpha(0.28f));
        g.drawRoundedRectangle(rect, 5.0f, 1.2f);
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.drawHorizontalLine(static_cast<int>(rect.getY() + 1.0f), rect.getX() + 4.0f, rect.getRight() - 4.0f);
    };

    // 1. Top Header Bar Bay
    auto headerRect = pad.removeFromTop(38.0f);
    drawGlossyBay(headerRect);

    g.setColour(juce::Colour(0xff00e2d6));
    g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
    g.drawText("PROGRAM SELECT:", static_cast<int>(headerRect.getX() + 14), static_cast<int>(headerRect.getY()),
               130, static_cast<int>(headerRect.getHeight()), juce::Justification::centredLeft);
    g.drawText("DAC BIT DEPTH:", static_cast<int>(headerRect.getX() + 465), static_cast<int>(headerRect.getY()),
               115, static_cast<int>(headerRect.getHeight()), juce::Justification::centredLeft);
    g.drawText("DRAM BANK:", static_cast<int>(headerRect.getX() + 835), static_cast<int>(headerRect.getY()),
               105, static_cast<int>(headerRect.getHeight()), juce::Justification::centredLeft);

    pad.removeFromTop(10.0f);

    // 2. Middle Parameter Rotary Bank Bay
    auto midRect = pad.removeFromTop(156.0f);
    drawGlossyBay(midRect);

    pad.removeFromTop(10.0f);

    // 3. Bottom Split Bays: Converter Lab & Multi-Chain Matrix
    auto botLeft = pad.removeFromLeft(624.0f);
    pad.removeFromLeft(14.0f);
    auto botRight = pad;

    drawGlossyBay(botLeft);
    drawGlossyBay(botRight);

    g.setColour(juce::Colour(0xff00e2d6));
    g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
    g.drawText("31.25 kHz CONVERTER & CLOCK CHARACTER LAB",
               static_cast<int>(botLeft.getX() + 16), static_cast<int>(botLeft.getY() + 6),
               380, 18, juce::Justification::centredLeft);

    g.drawText("MODERN MULTI-CHAIN ROUTING MATRIX (MOD -> PITCH -> DELAY -> REVERB)",
               static_cast<int>(botRight.getX() + 16), static_cast<int>(botRight.getY() + 6),
               520, 18, juce::Justification::centredLeft);
}
