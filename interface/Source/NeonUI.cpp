#include "NeonUI.h"

namespace
{
    void drawMonoBufferSegment(juce::Graphics& g,
        const juce::AudioSampleBuffer* buffer,
        int startSample,
        int numSamples,
        const juce::Rectangle<int>& area,
        juce::Colour colour)
    {
        if (buffer == nullptr
            || buffer->getNumChannels() == 0
            || numSamples <= 1
            || area.getWidth() <= 1)
            return;

        const int totalSamples = buffer->getNumSamples();
        startSample = juce::jlimit(0, totalSamples, startSample);
        numSamples = juce::jmin(numSamples, totalSamples - startSample);
        if (numSamples <= 1)
            return;

        auto* data = buffer->getReadPointer(0);

        const int   x0 = area.getX();
        const int   w = area.getWidth();
        const float top = (float)area.getY();
        const float h = (float)area.getHeight();
        const float midY = top + h * 0.5f;
        const float amp = h * 0.5f;

        juce::Path p;

        for (int x = 0; x < w; ++x)
        {
            const float proportion = (float)x / (float)(w - 1);
            const int   sampleIdx = startSample + (int)(proportion * (numSamples - 1));
            const int   clamped = juce::jlimit(0, totalSamples - 1, sampleIdx);
            const float sampleVal = data[clamped];
            const float y = midY - sampleVal * amp;

            if (x == 0)
                p.startNewSubPath((float)x0, y);
            else
                p.lineTo((float)(x0 + x), y);
        }

        g.setColour(colour);
        g.strokePath(p, juce::PathStrokeType(1.2f));
    }
}


//==============================================================================
// NeonTheme
//==============================================================================

NeonTheme NeonTheme::createDefault()
{
    NeonTheme t;

    // Very dark base
    t.background = juce::Colour::fromRGB(8, 10, 18);
    t.panel = juce::Colour::fromRGB(18, 22, 35);
    t.controlBackground = juce::Colour::fromRGB(26, 31, 48);
    t.controlOutline = juce::Colour::fromRGB(60, 70, 95);

    // Neon accents
    t.accentCyan = juce::Colour::fromRGB(0, 230, 255);   // main
    t.accentPink = juce::Colour::fromRGB(255, 80, 170);  // alt
    t.accentPurple = juce::Colour::fromRGB(150, 90, 255);  // alt

    t.textPrimary = juce::Colours::white.withAlpha(0.95f);
    t.textSecondary = juce::Colours::lightgrey.withAlpha(0.8f);

    // Soft glow for shadows
    t.glowSoft = t.accentCyan.withAlpha(0.35f);

    return t;
}

//==============================================================================
// NeonLookAndFeel
//==============================================================================

NeonLookAndFeel::NeonLookAndFeel()
    : theme(NeonTheme::createDefault())
{
    using namespace juce;

    // Global colours
    setColour(ResizableWindow::backgroundColourId, theme.background);

    setColour(Label::textColourId, theme.textPrimary);
    setColour(Label::backgroundColourId, Colours::transparentBlack);

    setColour(TextButton::buttonColourId, theme.controlBackground);
    setColour(TextButton::textColourOffId, theme.textPrimary);
    setColour(TextButton::textColourOnId, theme.textPrimary);

    setColour(Slider::backgroundColourId, theme.controlBackground);
    setColour(Slider::trackColourId, theme.accentCyan);
    setColour(Slider::thumbColourId, theme.accentCyan);
    setColour(Slider::textBoxTextColourId, theme.textPrimary);
    setColour(Slider::textBoxOutlineColourId, theme.controlOutline);
}

void NeonLookAndFeel::setTheme(const NeonTheme& newTheme)
{
    theme = newTheme;
}

//==============================================================================
// Typography
//==============================================================================

juce::Font NeonLookAndFeel::getUiFont(float size, bool bold) const
{
    // “Plugin-ish” UI font; if not found, JUCE will fall back appropriately.
    juce::Font f("Fira Sans", size, bold ? juce::Font::bold : juce::Font::plain);
    return f;
}

juce::Font NeonLookAndFeel::getNumericFont(float size) const
{
    // Slightly more “technical” numeric font.
    juce::Font f("Fira Mono", size, juce::Font::plain);
    return f;
}

juce::Font NeonLookAndFeel::getLabelFont(juce::Label& label)
{
    auto baseSize = juce::jmax(12.0f, label.getFont().getHeight());
    return getUiFont(baseSize, false);
}

juce::Font NeonLookAndFeel::getTextButtonFont(juce::TextButton&,
    int buttonHeight)
{
    auto size = juce::jmin(16.0f, buttonHeight * 0.55f);
    return getUiFont(size, true);
}

juce::Font NeonLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    auto baseSize = juce::jmax(12.0f, box.getHeight() * 0.5f);
    return getUiFont(baseSize, false);
}

juce::Font NeonLookAndFeel::getPopupMenuFont()
{
    return getUiFont(14.0f, false);
}

//==============================================================================
// Buttons
//==============================================================================

void NeonLookAndFeel::drawButtonBackground(juce::Graphics& g,
    juce::Button& button,
    const juce::Colour& backgroundColour,
    bool isMouseOverButton,
    bool isButtonDown)
{
    juce::ignoreUnused(backgroundColour);

    auto bounds = button.getLocalBounds().toFloat();

    const float cornerRadius = juce::jmin(bounds.getHeight() * 0.5f, 12.0f);
    const bool  enabled = button.isEnabled();

    // Base fill
    auto base = theme.controlBackground;
    if (!enabled)
        base = base.darker(0.4f);

    // Accent for hover/pressed
    auto accent = theme.accentCyan;
    if (!enabled)
        accent = accent.withAlpha(0.25f);

    auto fill = base;

    if (isButtonDown)
        fill = accent.interpolatedWith(base, 0.2f);
    else if (isMouseOverButton)
        fill = accent.interpolatedWith(base, 0.4f);

    // --- Decide which corners are rounded (special case for Recording/Comped tabs)
    bool roundTL = true, roundTR = true, roundBL = true, roundBR = true;
    const juce::String text = button.getButtonText();

    if (text == "Recording")
    {
        // Left tab: square inner (right) corners
        roundTR = false;
        roundBR = false;
    }
    else if (text == "Comped")
    {
        // Right tab: square inner (left) corners
        roundTL = false;
        roundBL = false;
    }

    // Inner rounded rect
    juce::Path buttonShape;
    buttonShape.addRoundedRectangle(bounds.getX() + 1.0f,
        bounds.getY() + 1.0f,
        bounds.getWidth() - 2.0f,
        bounds.getHeight() - 2.0f,
        cornerRadius, cornerRadius,
        roundTL, roundTR, roundBL, roundBR);

    g.setColour(fill);
    g.fillPath(buttonShape);

    // Outline
    auto outlineCol = theme.controlOutline.withAlpha(enabled ? 0.9f : 0.4f);
    g.setColour(outlineCol);
    g.strokePath(buttonShape, juce::PathStrokeType(1.2f));

    // “Neon glow” – now kept strictly inside the button shape
    if ((isMouseOverButton || isButtonDown) && enabled)
    {
        auto glow = theme.glowSoft;

        juce::Path glowShape;
        glowShape.addRoundedRectangle(bounds.getX() + 1.5f,
            bounds.getY() + 1.5f,
            bounds.getWidth() - 3.0f,
            bounds.getHeight() - 3.0f,
            cornerRadius, cornerRadius,
            roundTL, roundTR, roundBL, roundBR);

        g.setColour(glow.withAlpha(0.6f));
        g.fillPath(glowShape);
    }
}


void NeonLookAndFeel::drawButtonText(juce::Graphics& g,
    juce::TextButton& button,
    bool isMouseOverButton,
    bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    auto text = button.getButtonText();

    auto font = getTextButtonFont(button, (int)bounds.getHeight());
    g.setFont(font);

    auto enabled = button.isEnabled();
    auto col = theme.textPrimary;

    if (!enabled)
        col = col.withAlpha(0.4f);
    else if (isButtonDown)
        col = col.withBrightness(1.1f);
    else if (isMouseOverButton)
        col = col.withBrightness(1.05f);

    g.setColour(col);

    g.drawFittedText(text,
        button.getLocalBounds().reduced(4),
        juce::Justification::centred,
        1);
}




//==============================================================================
// Sliders
//==============================================================================

void NeonLookAndFeel::drawLinearSlider(juce::Graphics& g,
    int x, int y, int width, int height,
    float sliderPos,
    float minSliderPos,
    float maxSliderPos,
    const juce::Slider::SliderStyle style,
    juce::Slider& slider)
{
    juce::ignoreUnused(maxSliderPos);

    auto bounds = juce::Rectangle<float>((float)x, (float)y,
        (float)width, (float)height).reduced(2.0f);

    const bool isHorizontal =
        (style == juce::Slider::LinearHorizontal ||
            style == juce::Slider::LinearBar ||
            style == juce::Slider::LinearBarVertical) ||
        (bounds.getWidth() >= bounds.getHeight());

    auto enabled = slider.isEnabled();

    auto trackColour = theme.controlBackground;
    auto fillColour = theme.accentCyan;
    auto thumbColour = theme.accentCyan;

    if (!enabled)
    {
        trackColour = trackColour.darker(0.4f);
        fillColour = fillColour.withAlpha(0.3f);
        thumbColour = thumbColour.withAlpha(0.4f);
    }

    const float thickness = juce::jmax(4.0f, juce::jmin(bounds.getHeight() * 0.3f, 8.0f));
    juce::Rectangle<float> track;

    if (isHorizontal)
    {
        const float cy = bounds.getCentreY();
        track = { bounds.getX(), cy - thickness * 0.5f,
                  bounds.getWidth(), thickness };
    }
    else
    {
        const float cx = bounds.getCentreX();
        track = { cx - thickness * 0.5f, bounds.getY(),
                  thickness, bounds.getHeight() };
    }

    // Track background
    g.setColour(trackColour);
    g.fillRoundedRectangle(track, thickness * 0.5f);

    // Filled portion (from min to current)
    if (isHorizontal)
    {
        const float minPos = (float)minSliderPos;
        const float curPos = sliderPos;

        auto fill = track;
        fill.setRight(curPos);

        if (curPos > minPos)
        {
            fill.setX(minPos);
            g.setColour(fillColour);
            g.fillRoundedRectangle(fill, thickness * 0.5f);
        }
    }
    else
    {
        const float maxPos = (float)minSliderPos; // for vertical, JUCE uses different mapping,
        // but we can keep it simple visually.
        const float curPos = sliderPos;

        auto fill = track;
        fill.setY(curPos);
        fill.setHeight(track.getBottom() - curPos);

        g.setColour(fillColour);
        g.fillRoundedRectangle(fill, thickness * 0.5f);
    }

    // Thumb
    const float thumbRadius = juce::jmax(6.0f, thickness * 0.85f);
    juce::Point<float> thumbCentre;

    if (isHorizontal)
        thumbCentre = { sliderPos, track.getCentreY() };
    else
        thumbCentre = { track.getCentreX(), sliderPos };

    g.setColour(thumbColour);
    g.fillEllipse(thumbCentre.x - thumbRadius * 0.5f,
        thumbCentre.y - thumbRadius * 0.5f,
        thumbRadius,
        thumbRadius);

    if (enabled)
    {
        g.setColour(theme.glowSoft.withAlpha(0.4f));
        g.drawEllipse(thumbCentre.x - thumbRadius,
            thumbCentre.y - thumbRadius,
            thumbRadius * 2.0f,
            thumbRadius * 2.0f,
            1.6f);
    }
}

void NeonLookAndFeel::drawRotarySlider(juce::Graphics& g,
    int x, int y, int width, int height,
    float sliderPosProportional,
    float rotaryStartAngle,
    float rotaryEndAngle,
    juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float>((float)x, (float)y,
        (float)width, (float)height);

    // Leave a little padding around the knob
    bounds = bounds.reduced(6.0f);

    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();

    // Rotate the whole knob range by -90 degrees (−π/2)
    const float offset = -juce::MathConstants<float>::halfPi;
    const float startAngle = rotaryStartAngle + offset;
    const float endAngle = rotaryEndAngle + offset;

    const float angleRange = endAngle - startAngle;
    const float angle = startAngle + sliderPosProportional * angleRange;


    const bool enabled = slider.isEnabled();

    // Colours tuned for a soft, “3D plastic” knob look
    auto panelColour = theme.panel; // background behind knob
    auto outerRing = panelColour.darker(0.4f);
    auto knobBaseLight = panelColour.brighter(0.7f);
    auto knobBaseDark = panelColour.darker(0.3f);
    auto indicatorCol = juce::Colours::white;
    auto tickCol = outerRing.brighter(0.2f);

    if (!enabled)
    {
        outerRing = outerRing.withAlpha(0.5f);
        knobBaseLight = knobBaseLight.withAlpha(0.7f);
        knobBaseDark = knobBaseDark.withAlpha(0.7f);
        indicatorCol = indicatorCol.withAlpha(0.6f);
        tickCol = tickCol.withAlpha(0.5f);
    }

    // Drop shadow (soft, below knob)
    juce::Rectangle<float> shadowBounds(cx - radius * 0.9f,
        cy - radius * 0.3f,
        radius * 1.8f,
        radius * 1.2f);
    g.setColour(panelColour.darker(1.2f).withAlpha(0.55f));
    g.fillEllipse(shadowBounds);

    // Outer ring
    const float outerR = radius * 0.95f;
    juce::Rectangle<float> outerBounds(cx - outerR,
        cy - outerR,
        outerR * 2.0f,
        outerR * 2.0f);
    g.setColour(outerRing);
    g.drawEllipse(outerBounds, 2.0f);

    // Inner knob body with vertical gradient (light at top, darker at bottom)
    const float knobR = radius * 0.75f;
    juce::Rectangle<float> knobBounds(cx - knobR,
        cy - knobR,
        knobR * 2.0f,
        knobR * 2.0f);

    juce::ColourGradient knobGrad(knobBaseLight, cx, knobBounds.getY(),
        knobBaseDark, cx, knobBounds.getBottom(),
        false);
    g.setGradientFill(knobGrad);
    g.fillEllipse(knobBounds);

    // Inner subtle ring to give more depth
    g.setColour(panelColour.withAlpha(0.25f));
    g.drawEllipse(knobBounds.reduced(2.0f), 1.4f);

    // Static top tick (always at 12 o’clock)
    {
        const float tickOuter = outerR + 5.0f;
        const float tickInner = outerR - 3.0f;
        const float topAngle = juce::MathConstants<float>::pi * -0.5f; // straight up

        const float tx1 = cx + tickOuter * std::cos(topAngle);
        const float ty1 = cy + tickOuter * std::sin(topAngle);
        const float tx2 = cx + tickInner * std::cos(topAngle);
        const float ty2 = cy + tickInner * std::sin(topAngle);

        g.setColour(tickCol);
        g.drawLine(tx1, ty1, tx2, ty2, 2.0f);
    }

    // Min/Max ticks (bottom left / bottom right – like Accuracy/Emotion / Short/Long)
    {
        const float tickOuter = outerR + 2.0f;
        const float tickInner = outerR - 6.0f;

        auto drawTickAt = [&](float a)
            {
                const float x1 = cx + tickOuter * std::cos(a);
                const float y1 = cy + tickOuter * std::sin(a);
                const float x2 = cx + tickInner * std::cos(a);
                const float y2 = cy + tickInner * std::sin(a);

                g.drawLine(x1, y1, x2, y2, 2.0f);
            };

        g.setColour(tickCol);
        drawTickAt(startAngle);
        drawTickAt(endAngle);
    }

    // Value indicator line on the knob face (white line like in your reference)
    {
        const float indicatorLenInner = knobR * 0.15f;
        const float indicatorLenOuter = knobR * 0.9f;

        const float ix1 = cx + indicatorLenInner * std::cos(angle);
        const float iy1 = cy + indicatorLenInner * std::sin(angle);
        const float ix2 = cx + indicatorLenOuter * std::cos(angle);
        const float iy2 = cy + indicatorLenOuter * std::sin(angle);

        g.setColour(indicatorCol);
        g.drawLine(ix1, iy1, ix2, iy2, 2.4f);
    }

    // Small centre highlight
    g.setColour(knobBaseLight.withAlpha(0.6f));
    g.fillEllipse(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);

    // --- Numeric value under the knob (like the "50" in your mockup) ---
    auto valueString = slider.getTextFromValue(slider.getValue());

    auto font = getNumericFont(14.0f);
    g.setFont(font);
    g.setColour(theme.textSecondary.withAlpha(enabled ? 0.9f : 0.5f));

    auto textBounds = bounds.withY(bounds.getBottom() - 20.0f)
        .withHeight(18.0f);

    g.drawFittedText(valueString,
        textBounds.toNearestInt(),
        juce::Justification::centred,
        1);
}

//==============================================================================
// TakeLaneComponent
//==============================================================================

TakeLaneComponent::TakeLaneComponent(const juce::String& takeName, int takeIndex)
    : index(takeIndex)
{
    nameLabel.setText(takeName, juce::dontSendNotification);
    nameLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(nameLabel);
    addAndMakeVisible(selectButton);
    addAndMakeVisible(soloButton);

    // These act as toggles, but the real logic is in the callbacks
    selectButton.setClickingTogglesState(true);
    soloButton.setClickingTogglesState(true);

    selectButton.addListener(this);
    soloButton.addListener(this);

    setInterceptsMouseClicks(true, true);
}

void TakeLaneComponent::setSelected(bool shouldBeSelected)
{
    if (isSelected == shouldBeSelected)
        return;

    isSelected = shouldBeSelected;
    refreshButtonStates();
    repaint();
}

void TakeLaneComponent::setSoloed(bool shouldBeSoloed)
{
    if (isSoloed == shouldBeSoloed)
        return;

    isSoloed = shouldBeSoloed;
    refreshButtonStates();
    repaint();
}

void TakeLaneComponent::setPlayheadTime(double seconds)
{
    currentPlayheadTime = seconds;

    // Only repaint if this lane is visually active
    if (isSelected || isSoloed)
        repaint();
}

void TakeLaneComponent::setTimeRange(double startSec, double endSec)
{
    timeStartSec = startSec;
    timeEndSec = endSec;
    repaint();
}

void TakeLaneComponent::setWaveformSource(const juce::AudioSampleBuffer* buffer,
    int startSample,
    int numSamples)
{
    waveformBuffer = buffer;
    waveformStartSample = startSample;
    waveformNumSamples = numSamples;
    repaint();
}


void TakeLaneComponent::setCallbacks(std::function<void(int)> onSelect,
    std::function<void(int)> onSolo)
{
    selectCallback = std::move(onSelect);
    soloCallback = std::move(onSolo);
}

void TakeLaneComponent::refreshButtonStates()
{
    selectButton.setToggleState(isSelected, juce::dontSendNotification);
    soloButton.setToggleState(isSoloed, juce::dontSendNotification);

    // Slight visual hint by changing text
    selectButton.setButtonText(isSelected ? "Selected" : "Select");
    soloButton.setButtonText(isSoloed ? "Soloed" : "Solo");
}

void TakeLaneComponent::resized()
{
    auto bounds = getLocalBounds();

    auto labelArea = bounds.removeFromLeft(110);
    auto controlsArea = bounds.removeFromRight(140);

    nameLabel.setBounds(labelArea.reduced(8, 4));

    auto selectArea = controlsArea.removeFromLeft(controlsArea.getWidth() / 2);
    selectButton.setBounds(selectArea.reduced(6, 6));
    soloButton.setBounds(controlsArea.reduced(6, 6));
}

void TakeLaneComponent::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    const NeonTheme* tPtr = nullptr;
    if (auto* neon = dynamic_cast<NeonLookAndFeel*>(&getLookAndFeel()))
        tPtr = &neon->getTheme();

    auto panelCol = tPtr ? tPtr->panel : juce::Colours::darkgrey.darker(0.6f);
    auto outlineCol = tPtr ? tPtr->controlOutline : juce::Colours::grey;
    auto selectCol = tPtr ? tPtr->accentCyan : juce::Colours::cyan;
    auto soloCol = tPtr ? tPtr->accentPink : juce::Colours::orange;
    auto textCol = tPtr ? tPtr->textSecondary : juce::Colours::white;

    // Base background
    g.setColour(panelCol);
    g.fillRoundedRectangle(r, 4.0f);

    // Slight darker band for waveform area
    auto waveBoundsInt = getLocalBounds();
    auto labelArea = waveBoundsInt.removeFromLeft(110);
    juce::ignoreUnused(labelArea);
    auto controlsArea = waveBoundsInt.removeFromRight(140);
    juce::ignoreUnused(controlsArea);
    auto waveArea = waveBoundsInt.reduced(6, 8);

    g.setColour(panelCol.darker(0.5f));
    g.fillRect(waveArea);

    g.setColour(panelCol.brighter(0.25f));
    g.drawRect(waveArea);

    // Draw the actual waveform for this take if we have one.
    if (waveformBuffer != nullptr && waveformNumSamples > 0)
    {
        drawMonoBufferSegment(g,
            waveformBuffer,
            waveformStartSample,
            waveformNumSamples,
            waveArea,
            panelCol.brighter(0.8f));
    }
    else
    {
        // Fallback: simple stripes if no audio assigned
        g.setColour(panelCol.brighter(0.4f));
        const int stripeStep = 8;
        for (int x = waveArea.getX(); x < waveArea.getRight(); x += stripeStep)
        {
            g.drawLine((float)x, (float)waveArea.getY(),
                (float)x, (float)waveArea.getBottom(), 0.4f);
        }
    }

    // Selection / solo highlights
    if (isSoloed)
    {
        g.setColour(soloCol.withAlpha(0.12f));
        g.fillRoundedRectangle(r, 4.0f);
    }

    if (isSelected)
    {
        g.setColour(selectCol.withAlpha(0.9f));
        g.drawRoundedRectangle(r.expanded(0.5f), 4.0f, 1.5f);
    }

    // Playhead line (only when this lane is active)
    if ((isSelected || isSoloed) && timeEndSec > timeStartSec)
    {
        double tNorm = (currentPlayheadTime - timeStartSec) / (timeEndSec - timeStartSec);
        tNorm = juce::jlimit(0.0, 1.0, tNorm);

        const int x = waveArea.getX() + juce::roundToInt(tNorm * (double)waveArea.getWidth());

        g.setColour(selectCol.withAlpha(0.95f));
        g.drawLine((float)x,
            (float)waveArea.getY(),
            (float)x,
            (float)waveArea.getBottom(),
            2.0f);
    }

    // Subtle separator at the bottom
    g.setColour(outlineCol.withAlpha(0.4f));
    g.drawLine(r.getX(), r.getBottom(), r.getRight(), r.getBottom(), 1.0f);

    g.setColour(textCol);
}

void TakeLaneComponent::buttonClicked(juce::Button* b)
{
    if (b == &selectButton)
    {
        if (selectCallback)
            selectCallback(index);
    }
    else if (b == &soloButton)
    {
        if (soloCallback)
            soloCallback(index);
    }
}

int NeonLookAndFeel::getDefaultScrollbarWidth()
{
    // Slim bar
    return 8;
}

void NeonLookAndFeel::drawScrollbar(juce::Graphics& g,
    juce::ScrollBar& scrollbar,
    int x, int y,
    int width, int height,
    bool isVertical,
    int thumbStartPosition,
    int thumbSize,
    bool isMouseOver,
    bool isMouseDown)
{
    juce::ignoreUnused(scrollbar);

    auto track = juce::Rectangle<int>(x, y, width, height);
    auto thumb = track;

    if (isVertical)
        thumb = thumb.withY(thumbStartPosition).withHeight(thumbSize);
    else
        thumb = thumb.withX(thumbStartPosition).withWidth(thumbSize);

    auto trackColour = theme.controlBackground.darker(0.7f);
    auto thumbColour = theme.accentCyan.withAlpha(0.6f);

    if (isMouseDown)
        thumbColour = thumbColour.brighter(0.3f);
    else if (isMouseOver)
        thumbColour = thumbColour.brighter(0.15f);

    // Track
    g.setColour(trackColour);
    g.fillRoundedRectangle(track.toFloat(), 3.0f);

    // Thumb
    if (thumbSize > 0)
    {
        g.setColour(thumbColour);
        g.fillRoundedRectangle(thumb.toFloat().reduced(1.0f), 3.0f);

        g.setColour(theme.glowSoft.withAlpha(0.35f));
        g.drawRoundedRectangle(thumb.toFloat().reduced(0.5f), 3.0f, 1.2f);
    }
}

//==============================================================================
// NeonProgressBar
//==============================================================================

void NeonProgressBar::timerCallback()
{
    const double dt = getTimerInterval() * 0.001; // ms -> seconds
    elapsedSeconds += dt;

    if (!backendFinished)
    {
        const double clampedElapsed = juce::jlimit(0.0, maxDurationSeconds, elapsedSeconds);
        const double windowFraction = (maxDurationSeconds > 0.0)
            ? (clampedElapsed / maxDurationSeconds)
            : 1.0;
        const double targetProgress01 = 0.95 * windowFraction;

        progress01 = juce::jlimit(0.0, 0.95, targetProgress01);
    }
    else
    {
        progress01 = 1.0;
        stopTimer();
    }

    repaint();
}

void NeonProgressBar::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    auto* neonLF = dynamic_cast<NeonLookAndFeel*>(&getLookAndFeel());
    const NeonTheme* t = neonLF ? &neonLF->getTheme() : nullptr;

    auto bg = t ? t->panel : juce::Colours::black;
    auto trackCol = t ? t->controlBackground : juce::Colours::darkgrey;
    auto outlineCol = t ? t->controlOutline : juce::Colours::grey;
    auto textCol = t ? t->textPrimary : juce::Colours::white;

    juce::Colour fill1 = t ? t->accentCyan : juce::Colours::cyan;
    juce::Colour fill2 = t ? t->accentPink : juce::Colours::magenta;
    juce::Colour fill3 = t ? t->accentPurple : juce::Colours::purple;
    juce::Colour glow = t ? t->glowSoft : juce::Colours::cyan.withAlpha(0.4f);

    // Background of the whole control
    g.setColour(bg);
    g.fillRoundedRectangle(bounds, 8.0f);

    // Top text area + bottom track area
    auto textArea = bounds.removeFromTop(bounds.getHeight() * 0.45f).reduced(4.0f);
    auto trackArea = bounds.reduced(8.0f, 6.0f);
    const float radius = trackArea.getHeight() * 0.5f;

    // Track
    g.setColour(trackCol.darker(0.4f));
    g.fillRoundedRectangle(trackArea, radius);

    // --- NEON PILL HANDLE ------------------------------------------------------
    const float prog = juce::jlimit(0.0f, 1.0f, (float)progress01); // 0..1

    if (prog > 0.0f)
    {
        juce::Graphics::ScopedSaveState clipper(g);
        g.reduceClipRegion(trackArea.toNearestInt());

        const float trackHeight = trackArea.getHeight();
        const float pillDiameter = trackHeight;        // width == height
        const float trackX = trackArea.getX();
        const float trackWidth = trackArea.getWidth();

        // Map 0..1 -> [trackX, trackRight - pillDiameter]
        const float leftEdge = trackX + prog * (trackWidth - pillDiameter);

        juce::Rectangle<float> pillBounds(leftEdge,
            trackArea.getY(),
            pillDiameter,
            trackHeight);

        juce::ColourGradient grad(fill1, pillBounds.getX(), pillBounds.getCentreY(),
            fill3, pillBounds.getRight(), pillBounds.getCentreY(),
            false);
        grad.addColour(0.5, fill2);

        g.setGradientFill(grad);
        g.fillRoundedRectangle(pillBounds, radius);

        // Glow kept inside the track
        g.setColour(glow.withAlpha(0.7f));
        g.drawRoundedRectangle(pillBounds.reduced(0.5f), radius - 0.5f, 1.6f);
    }

    // Track outline
    g.setColour(outlineCol.withAlpha(0.9f));
    g.drawRoundedRectangle(trackArea, radius, 1.2f);

    // Label text ---------------------------------------------------------------
    const int pct = juce::roundToInt(progress01 * 100.0);
    juce::String label;

    if (backendFinished && progress01 >= 0.999)
        label = "Done – 100%";
    else if (pct <= 0)
        label = "Comping ready";
    else
        label = "Comping " + juce::String(pct) + "%";

    juce::Font font = neonLF
        ? neonLF->getUiFont(14.0f, true)
        : juce::Font(14.0f, juce::Font::bold);

    g.setFont(font);
    g.setColour(textCol);
    g.drawFittedText(label,
        textArea.toNearestInt(),
        juce::Justification::centred,
        1);
}








