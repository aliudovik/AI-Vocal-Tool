#pragma once

#include <JuceHeader.h>
#include <functional>

//==============================================================================
// NeonTheme: central colour palette for the app
//==============================================================================

struct NeonTheme
{
    juce::Colour background;         // main window background
    juce::Colour panel;              // panels / cards
    juce::Colour controlBackground;  // buttons, sliders background
    juce::Colour controlOutline;     // outlines, separators

    juce::Colour accentCyan;         // primary accent
    juce::Colour accentPink;         // secondary accent
    juce::Colour accentPurple;       // tertiary accent

    juce::Colour textPrimary;        // main text
    juce::Colour textSecondary;      // secondary text / hints

    juce::Colour glowSoft;           // soft neon glow

    static NeonTheme createDefault();
};

//==============================================================================
// NeonLookAndFeel: dark + neon look for buttons, sliders, fonts
//==============================================================================

class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel();
    ~NeonLookAndFeel() override = default;

    const NeonTheme& getTheme() const noexcept { return theme; }
    void setTheme(const NeonTheme& newTheme);

    // Typography helpers
    juce::Font getUiFont(float size, bool bold = false) const;
    juce::Font getNumericFont(float size) const;

    // LookAndFeel overrides
    void drawButtonBackground(juce::Graphics& g,
        juce::Button& button,
        const juce::Colour& backgroundColour,
        bool isMouseOverButton,
        bool isButtonDown) override;

    void drawButtonText(juce::Graphics& g,
        juce::TextButton& button,
        bool isMouseOverButton,
        bool isButtonDown) override;

    void drawLinearSlider(juce::Graphics& g,
        int x, int y, int width, int height,
        float sliderPos,
        float minSliderPos,
        float maxSliderPos,
        const juce::Slider::SliderStyle style,
        juce::Slider& slider) override;

    void drawRotarySlider(juce::Graphics& g,
        int x, int y, int width, int height,
        float sliderPosProportional,
        float rotaryStartAngle,
        float rotaryEndAngle,
        juce::Slider& slider) override;



    juce::Font getLabelFont(juce::Label& label) override;
    juce::Font getTextButtonFont(juce::TextButton& button,
        int buttonHeight) override;
    juce::Font getComboBoxFont(juce::ComboBox& box) override;
    juce::Font getPopupMenuFont() override;


    // Scrollbar styling
    int getDefaultScrollbarWidth() override;
    void drawScrollbar(juce::Graphics& g,
        juce::ScrollBar& scrollbar,
        int x, int y,
        int width, int height,
        bool isVertical,
        int thumbStartPosition,
        int thumbSize,
        bool isMouseOver,
        bool isMouseDown) override;


private:
    NeonTheme theme;
};

//==============================================================================
// Helper components
//==============================================================================

// Simple TextButton that assumes we’re using NeonLookAndFeel
class NeonButton : public juce::TextButton
{
public:
    explicit NeonButton(const juce::String& buttonText = {})
        : juce::TextButton(buttonText)
    {
        setClickingTogglesState(false);
        setWantsKeyboardFocus(false);
    }
};

// Accuracy <-> Emotion horizontal slider (0–100)
class AccuracyEmotionSlider : public juce::Slider
{
public:
    AccuracyEmotionSlider()
    {
        setSliderStyle(juce::Slider::LinearHorizontal);
        setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 18);
        setRange(0.0, 100.0, 1.0);
        setValue(50.0);
    }
};

// Crossfade rotary knob (0–100)
class CrossfadeKnob : public juce::Slider
{
public:
    CrossfadeKnob()
    {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 18);
        setRange(0.0, 100.0, 1.0);
        setValue(50.0);
    }
};

// Single take lane inside the scrollable takes view
class TakeLaneComponent : public juce::Component,
    private juce::Button::Listener
{
public:
    TakeLaneComponent(const juce::String& takeName, int takeIndex);

    // State flags
    void setSelected(bool shouldBeSelected);
    void setSoloed(bool shouldBeSoloed);
    void setPlayheadTime(double seconds);           // global time in seconds
    void setTimeRange(double startSec, double endSec); // [start, end] visible range
    void setCallbacks(std::function<void(int)> onSelect,
        std::function<void(int)> onSolo);

    void setWaveformSource(const juce::AudioSampleBuffer* buffer,
        int startSample,
        int numSamples);

    int  getTakeIndex() const noexcept { return index; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void buttonClicked(juce::Button* b) override;
    void refreshButtonStates();

    juce::Label  nameLabel;
    NeonButton   selectButton{ "Select" };
    NeonButton   soloButton{ "Solo" };

    int    index = -1;
    bool   isSelected = false;
    bool   isSoloed = false;
    double currentPlayheadTime = 0.0;
    double timeStartSec = 0.0;
    double timeEndSec = 1.0;

    const juce::AudioSampleBuffer* waveformBuffer = nullptr;
    int    waveformStartSample = 0;
    int    waveformNumSamples = 0;

    std::function<void(int)> selectCallback;
    std::function<void(int)> soloCallback;
};

//==============================================================================
// NeonProgressBar: used in the comping pop-up window
//==============================================================================

class NeonProgressBar : public juce::Component,
    private juce::Timer
{
public:
    NeonProgressBar() = default;

    void startComping()
    {
        backendFinished = false;
        progress01 = 0.0;
        elapsedSeconds = 0.0;
        startTimer(40); // ~25fps
        repaint();
    }

    void setBackendFinished()
    {
        backendFinished = true;
        progress01 = 1.0;
        stopTimer();
        repaint();
    }

    double getProgressPercent() const noexcept { return progress01 * 100.0; }

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    double progress01 = 0.0;      // 0.0–1.0
    bool   backendFinished = false;
    double elapsedSeconds = 0.0;

    static constexpr double maxDurationSeconds = 90.0;
};




#pragma once
