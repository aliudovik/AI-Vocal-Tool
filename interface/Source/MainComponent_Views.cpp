// MainComponent_Views.cpp
#include "MainComponent.h"

//==============================================================================
// Local waveform helper
//==============================================================================

namespace
{
    void drawMonoBufferAsWaveform(juce::Graphics& g,
        const juce::AudioSampleBuffer& buffer,
        int startSample,
        int numSamples,
        const juce::Rectangle<int>& area)
    {
        if (buffer.getNumChannels() == 0 || numSamples <= 1 || area.getWidth() <= 1)
            return;

        const int totalSamples = buffer.getNumSamples();
        startSample = juce::jlimit(0, totalSamples, startSample);
        numSamples = juce::jmin(numSamples, totalSamples - startSample);

        if (numSamples <= 1)
            return;

        auto* data = buffer.getReadPointer(0);

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
            const int   sampleIndex = startSample + (int)(proportion * (numSamples - 1));
            const int   clamped = juce::jlimit(0, totalSamples - 1, sampleIndex);
            const float s = data[clamped];
            const float y = midY - s * amp;

            if (x == 0)
                p.startNewSubPath((float)x0, y);
            else
                p.lineTo((float)(x0 + x), y);
        }

        g.strokePath(p, juce::PathStrokeType(1.2f));
    }
}

//==============================================================================

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    if (viewMode == ViewMode::Recording)
        paintRecordingView(g);
    else
        paintCompReviewView(g);
}

void MainComponent::paintRecordingView(juce::Graphics& g)
{
    g.setColour(juce::Colours::darkgrey.darker(0.5f));
    g.fillRect(instrumentalLabelBounds);
    g.fillRect(instrumentalWaveformBounds);

    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawText("Instrumental",
        instrumentalLabelBounds.reduced(4),
        juce::Justification::centredLeft,
        true);

    const double totalLength = thumbnail.getTotalLength();

    if (totalLength > 0.0)
    {
        auto innerBounds = instrumentalWaveformBounds.reduced(2);

        g.setColour(juce::Colours::darkgrey.brighter(0.3f));
        thumbnail.drawChannel(g,
            innerBounds,
            0.0,
            totalLength,
            0,
            1.0f);

        if (hasValidLoop())
        {
            const double startProp = juce::jlimit(0.0, 1.0, loopStartSec / totalLength);
            const double endProp = juce::jlimit(0.0, 1.0, loopEndSec / totalLength);

            const int totalW = innerBounds.getWidth();

            const int loopX = innerBounds.getX()
                + juce::roundToInt(startProp * (double)totalW);
            const int loopW = juce::jmax(1,
                juce::roundToInt((endProp - startProp) * (double)totalW));

            juce::Rectangle<int> loopRect(loopX,
                innerBounds.getY(),
                loopW,
                innerBounds.getHeight());

            g.setColour(juce::Colours::lightgreen);
            thumbnail.drawChannel(g,
                loopRect,
                loopStartSec,
                loopEndSec,
                0,
                1.0f);
        }

        const double current = transportSource.getCurrentPosition();
        if (current >= 0.0 && totalLength > 0.0)
        {
            const double proportion =
                juce::jlimit(0.0, 1.0, current / totalLength);

            const int x = instrumentalWaveformBounds.getX()
                + juce::roundToInt(proportion
                    * (double)instrumentalWaveformBounds.getWidth());

            g.setColour(juce::Colours::yellow);
            g.drawLine((float)x,
                (float)instrumentalWaveformBounds.getY(),
                (float)x,
                (float)instrumentalWaveformBounds.getBottom(),
                2.0f);
        }

        if (hasValidLoop())
        {
            const int   xStart = timeToX(loopStartSec);
            const int   xEnd = timeToX(loopEndSec);

            const float topY = (float)instrumentalWaveformBounds.getY();
            const float bottomY = (float)instrumentalWaveformBounds.getBottom();

            g.setColour(juce::Colours::red);

            g.drawLine((float)xStart, topY, (float)xStart, bottomY, 2.0f);
            g.drawLine((float)xEnd, topY, (float)xEnd, bottomY, 2.0f);

            const float arrowHeight = 10.0f;
            const float arrowHalfW = 6.0f;

            juce::Path startArrow;
            startArrow.addTriangle((float)xStart, topY,
                (float)xStart - arrowHalfW, topY - arrowHeight,
                (float)xStart + arrowHalfW, topY - arrowHeight);
            g.fillPath(startArrow);

            juce::Path endArrow;
            endArrow.addTriangle((float)xEnd, topY,
                (float)xEnd - arrowHalfW, topY - arrowHeight,
                (float)xEnd + arrowHalfW, topY - arrowHeight);
            g.fillPath(endArrow);
        }
    }
    else
    {
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawFittedText("Click 'IMPORT' to load a WAV file",
            instrumentalWaveformBounds.reduced(10),
            juce::Justification::centred,
            2);
    }

    
}

//==============================================================================

void MainComponent::paintCompReviewView(juce::Graphics& g)
{
    const double instrumentalLength = thumbnail.getTotalLength();

    g.setColour(juce::Colours::darkgrey.darker(0.5f));
    g.fillRect(instrumentalLabelBounds);
    g.fillRect(instrumentalWaveformBounds);

    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawText("Instrumental",
        instrumentalLabelBounds.reduced(4),
        juce::Justification::centredLeft,
        true);

    if (instrumentalLength > 0.0)
    {
        auto innerBounds = instrumentalWaveformBounds.reduced(2);

        g.setColour(juce::Colours::darkgrey.brighter(0.3f));
        thumbnail.drawChannel(g,
            innerBounds,
            0.0,
            instrumentalLength,
            0,
            1.0f);

        if (hasValidLoop())
        {
            const double startProp =
                juce::jlimit(0.0, 1.0, loopStartSec / instrumentalLength);
            const double endProp =
                juce::jlimit(0.0, 1.0, loopEndSec / instrumentalLength);

            const int totalW = innerBounds.getWidth();

            const int loopX = innerBounds.getX()
                + juce::roundToInt(startProp * (double)totalW);
            const int loopW = juce::jmax(1,
                juce::roundToInt((endProp - startProp) * (double)totalW));

            juce::Rectangle<int> loopRect(loopX,
                innerBounds.getY(),
                loopW,
                innerBounds.getHeight());

            g.setColour(juce::Colours::lightgreen);
            thumbnail.drawChannel(g,
                loopRect,
                loopStartSec,
                loopEndSec,
                0,
                1.0f);
        }

        const double current = transportSource.getCurrentPosition();
        if (current >= 0.0 && instrumentalLength > 0.0)
        {
            const double proportion =
                juce::jlimit(0.0, 1.0, current / instrumentalLength);

            const int x = instrumentalWaveformBounds.getX()
                + juce::roundToInt(proportion
                    * (double)instrumentalWaveformBounds.getWidth());

            g.setColour(juce::Colours::yellow);
            g.drawLine((float)x,
                (float)instrumentalWaveformBounds.getY(),
                (float)x,
                (float)instrumentalWaveformBounds.getBottom(),
                2.0f);
        }

        if (hasValidLoop())
        {
            const int   xStart = timeToX(loopStartSec);
            const int   xEnd = timeToX(loopEndSec);

            const float topY = (float)instrumentalWaveformBounds.getY();
            const float bottomY = (float)instrumentalWaveformBounds.getBottom();

            g.setColour(juce::Colours::red);

            g.drawLine((float)xStart, topY, (float)xStart, bottomY, 2.0f);
            g.drawLine((float)xEnd, topY, (float)xEnd, bottomY, 2.0f);

            const float arrowHeight = 10.0f;
            const float arrowHalfW = 6.0f;

            juce::Path startArrow;
            startArrow.addTriangle((float)xStart, topY,
                (float)xStart - arrowHalfW, topY - arrowHeight,
                (float)xStart + arrowHalfW, topY - arrowHeight);
            g.fillPath(startArrow);

            juce::Path endArrow;
            endArrow.addTriangle((float)xEnd, topY,
                (float)xEnd - arrowHalfW, topY - arrowHeight,
                (float)xEnd + arrowHalfW, topY - arrowHeight);
            g.fillPath(endArrow);
        }
    }
    else
    {
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawFittedText("Click 'IMPORT' to load an instrumental",
            instrumentalWaveformBounds.reduced(10),
            juce::Justification::centred,
            2);
    }

    const double compLength = compedThumbnail.getTotalLength();
    const bool   canDrawComped =
        hasLastCompResult && hasCompedThumbnail && (compLength > 0.0);

    if (takesAreaBounds.isEmpty())
        return;

    if (!canDrawComped)
    {
        g.setColour(juce::Colours::white);
        g.setFont(16.0f);
        g.drawFittedText("No comped result yet.\nRun COMPING on the Recording tab first.",
            takesAreaBounds.reduced(10),
            juce::Justification::centred,
            2);
        return;
    }

    juce::Rectangle<int> row, labelRect, waveRect, controlsRect;
    getCompRowLayout(row, labelRect, waveRect, controlsRect);
    const int centerY = row.getCentreY();

    // Use Neon theme so it matches TakeLaneComponent
    const NeonTheme* tPtr = nullptr;
    if (auto* neon = dynamic_cast<NeonLookAndFeel*>(&getLookAndFeel()))
        tPtr = &neon->getTheme();

    auto panelCol = tPtr ? tPtr->panel : juce::Colours::darkgrey.darker(0.6f);
    auto outlineCol = tPtr ? tPtr->controlOutline : juce::Colours::grey;
    auto selectCol = tPtr ? tPtr->accentCyan : juce::Colours::cyan;
    auto soloCol = tPtr ? tPtr->accentPink : juce::Colours::orange;
    auto textCol = tPtr ? tPtr->textSecondary : juce::Colours::white;

    // Base card
    g.setColour(panelCol);
    g.fillRoundedRectangle(row.toFloat(), 4.0f);

    // Waveform “slot” like in take lanes
    auto waveOuter = waveRect.reduced(6, 8);
    g.setColour(panelCol.darker(0.5f));
    g.fillRect(waveOuter);
    g.setColour(panelCol.brighter(0.25f));
    g.drawRect(waveOuter);

    // Selection / solo highlights
    if (compedSolo)
    {
        g.setColour(soloCol.withAlpha(0.12f));
        g.fillRoundedRectangle(row.toFloat(), 4.0f);
    }

    if (compedSelected)
    {
        g.setColour(selectCol.withAlpha(0.9f));
        g.drawRoundedRectangle(row.toFloat().expanded(0.5f), 4.0f, 1.5f);
    }

    // ----- Left label area (COMPED TAKE + parameters) -----
    auto labelArea = labelRect.reduced(8, 4);

    juce::Rectangle<int> titleArea = labelArea.removeFromTop(22);
    juce::Rectangle<int> accArea = labelArea.removeFromTop(18);
    juce::Rectangle<int> emoArea = labelArea.removeFromTop(18);
    juce::Rectangle<int> cfArea = labelArea.removeFromTop(18);

    const int acc = juce::jlimit(0, 100, lastCompAlphaPct);
    const int emo = juce::jlimit(0, 100, 100 - lastCompAlphaPct);
    const int cf = juce::jlimit(0, 100, lastCompCrossfadePct);

    g.setColour(textCol);
    g.setFont(16.0f);
    g.drawText("COMPED TAKE 1",
        titleArea,
        juce::Justification::centredLeft,
        true);

    g.setFont(14.0f);
    g.drawText("Accuracy " + juce::String(acc) + "%", accArea,
        juce::Justification::centredLeft, true);
    g.drawText("Emotion " + juce::String(emo) + "%", emoArea,
        juce::Justification::centredLeft, true);
    g.drawText("Crossfade " + juce::String(cf) + "%", cfArea,
        juce::Justification::centredLeft, true);

    // ----- Comped waveform + top red bar with segment take numbers -----
    auto inner = waveOuter.reduced(4);
    const int topBarHeight = 22;
    auto topBarRect = inner.removeFromTop(topBarHeight);
    auto compWaveArea = inner;

    g.setColour(juce::Colours::darkred);
    g.fillRect(topBarRect);

    g.setColour(panelCol.brighter(0.8f));   // waveform colour
    compedThumbnail.drawChannel(g,
        compWaveArea,
        0.0,
        compLength,
        0,
        1.0f);

    // ALWAYS draw playhead over comped waveform while playing
    const double compPos = takeTransport.getCurrentPosition();
    if (compPos >= 0.0 && compLength > 0.0)
    {
        const double prop =
            juce::jlimit(0.0, 1.0, compPos / compLength);

        const int x = compWaveArea.getX()
            + juce::roundToInt(prop * (double)compWaveArea.getWidth());

        g.setColour(juce::Colours::yellow);
        g.drawLine((float)x,
            (float)compWaveArea.getY(),
            (float)x,
            (float)compWaveArea.getBottom(),
            2.0f);
    }

    // Segment markers + take index labels in the red bar
    g.setFont(14.0f);
    g.setColour(juce::Colours::white);

    for (int i = 0; i < compSegments.size(); ++i)
    {
        const auto& seg = compSegments.getReference(i);
        if (!(seg.endSec > seg.startSec))
            continue;

        const int xStart = compedTimeToX(seg.startSec, compWaveArea);
        const int xEnd = compedTimeToX(seg.endSec, compWaveArea);

        g.setColour(juce::Colours::lightgreen);
        g.drawLine((float)xStart,
            (float)topBarRect.getY(),
            (float)xStart,
            (float)compWaveArea.getBottom(),
            2.0f);

        const int midX = xStart + (xEnd - xStart) / 2;
        const int labelWidth = 30;
        juce::Rectangle<int> labelBox(midX - labelWidth / 2,
            topBarRect.getY(),
            labelWidth,
            topBarRect.getHeight());

        juce::String text = (seg.takeIndex > 0)
            ? juce::String(seg.takeIndex)
            : juce::String("-");
        g.setColour(juce::Colours::white);
        g.drawText(text,
            labelBox,
            juce::Justification::centred,
            true);
    }

    // No more boxSize/selectRect/soloRect painting – the real buttons draw themselves


    //const int boxSize = 14;

    //juce::Rectangle<int> selectRect(
    //    controlsRect.getX() + controlsRect.getWidth() / 4 - boxSize / 2,
    //    centerY - boxSize / 2,
    //    boxSize, boxSize);

    //juce::Rectangle<int> soloRect(
    //    controlsRect.getX() + 3 * controlsRect.getWidth() / 4 - boxSize / 2,
    //    centerY - boxSize / 2,
    //    boxSize, boxSize);

    //g.setColour(juce::Colours::white);
    //g.drawRect(selectRect, 1);
    //g.drawRect(soloRect, 1);

    //if (compedSelected)
    //    g.fillRect(selectRect.reduced(3));

    //if (compedSolo)
    //    g.fillRect(soloRect.reduced(3));
}

//==============================================================================

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(10);

    auto topRow = area.removeFromTop(40);

    auto tabArea = topRow.removeFromRight(180);
    const int tabHeight = 26;

    auto recTabArea = tabArea.removeFromLeft(80)
        .withHeight(tabHeight)
        .withY(tabArea.getCentreY() - tabHeight / 2);
    auto compTabArea = tabArea.removeFromLeft(80)
        .withHeight(tabHeight)
        .withY(tabArea.getCentreY() - tabHeight / 2);

    recordingTabButton.setBounds(recTabArea);
    compedTabButton.setBounds(compTabArea);
    compedTabButton.setEnabled(hasLastCompResult);

    importButton.setBounds(topRow.removeFromLeft(220));
    topRow.removeFromLeft(10);
    playButton.setBounds(topRow.removeFromLeft(80));
    topRow.removeFromLeft(10);
    stopButton.setBounds(topRow.removeFromLeft(80));
    topRow.removeFromLeft(10);
    recordButton.setBounds(topRow.removeFromLeft(90));
    topRow.removeFromLeft(10);
    ioButton.setBounds(topRow.removeFromLeft(80));
    topRow.removeFromLeft(10);
    resetButton.setBounds(topRow.removeFromLeft(100));
    topRow.removeFromLeft(10);
    loadProjectButton.setBounds(topRow.removeFromLeft(110));
    topRow.removeFromLeft(6);
    saveProjectButton.setBounds(topRow.removeFromLeft(120));
    topRow.removeFromLeft(10);

    auto bpmArea = topRow.removeFromLeft(100);
    bpmLabel.setBounds(bpmArea);
    bpmBounds = bpmArea;


    topRow.removeFromLeft(10);
    metronomeToggle.setBounds(topRow.removeFromLeft(110));

    if (viewMode == ViewMode::Recording)
        layoutRecordingView(area);
    else
        layoutCompReviewView(area);

    // Takes viewport only visible in Recording view
    takesViewport.setVisible(viewMode == ViewMode::Recording);
}

void MainComponent::layoutRecordingView(juce::Rectangle<int> area)
{
    const int gap = 15;
    area.removeFromTop(gap);

    const int trackHeight = juce::jmin(140, area.getHeight() / 3);
    auto trackArea = area.removeFromTop(trackHeight);

    auto labelArea = trackArea.removeFromLeft(130);

    instrumentalLabelBounds = labelArea;
    instrumentalWaveformBounds = trackArea;

    takesAreaBounds = area;

    const int compPanelHeight = 210;
    auto compPanelArea = takesAreaBounds.removeFromBottom(compPanelHeight);

    const int headerHeight = 22;
    auto headerArea = takesAreaBounds.removeFromTop(headerHeight);
    headerArea = headerArea.reduced(0, 4);

    auto headerRight = headerArea.removeFromRight(160);
    auto soloArea = headerRight.removeFromRight(60);
    auto selectArea = headerRight.removeFromRight(80);

    //selectTakeLabel.setBounds(selectArea);
   // soloLabel.setBounds(soloArea);

    auto headerLeft = headerArea.removeFromLeft(220);
    auto takeVolLabelArea = headerLeft.removeFromLeft(90);
    auto takeVolSliderArea = headerLeft;

    takeVolumeLabel.setBounds(takeVolLabelArea);
    takeVolumeSlider.setBounds(takeVolSliderArea);

    auto compArea = compPanelArea.reduced(20, 8);

    // Split comp area into two columns: STYLE (left) and CROSSFADE (right)
    auto styleColumn = compArea.removeFromLeft(compArea.getWidth() / 2);
    auto crossfadeColumn = compArea;

    // Common sizes for knobs
    const int knobSize = 120; // 100–200px range as you wanted
    const int titleHeight = 24;
    const int sideLabelHeight = 18;

    // === STYLE column ===
    {
        auto col = styleColumn.reduced(10, 4);

        // Top title "STYLE"
        auto titleArea = col.removeFromTop(titleHeight);
        accuracyEmotionLabel.setBounds(titleArea.withSizeKeepingCentre(titleArea.getWidth(), titleHeight));

        // Knob area
        auto knobArea = col.removeFromTop(knobSize + 10);
        auto knobBounds = knobArea.withSizeKeepingCentre(knobSize, knobSize);
        accuracyEmotionSlider.setBounds(knobBounds.toNearestInt());

        // Side labels: ACCURACY (left), EMOTION (right)
        auto sideRow = col.removeFromTop(sideLabelHeight);
        auto leftArea = sideRow.removeFromLeft(sideRow.getWidth() / 2).reduced(0, 0);
        auto rightArea = sideRow;

        styleLeftLabel.setBounds(leftArea.reduced(0, 0));
        styleRightLabel.setBounds(rightArea.reduced(0, 0));
    }

    // === CROSSFADE column ===
    {
        auto col = crossfadeColumn.reduced(10, 4);

        // Top title "CROSSFADE"
        auto titleArea = col.removeFromTop(titleHeight);
        crossfadeLabel.setBounds(titleArea.withSizeKeepingCentre(titleArea.getWidth(), titleHeight));

        // Knob area
        auto knobArea = col.removeFromTop(knobSize + 10);
        auto knobBounds = knobArea.withSizeKeepingCentre(knobSize, knobSize);
        crossfadeSlider.setBounds(knobBounds.toNearestInt());

        // Side labels: SHORT (left), LONG (right)
        auto sideRow = col.removeFromTop(sideLabelHeight);
        auto leftArea = sideRow.removeFromLeft(sideRow.getWidth() / 2);
        auto rightArea = sideRow;

        crossfadeLeftLabel.setBounds(leftArea);
        crossfadeRightLabel.setBounds(rightArea);
    }

    // COMPING button centred under both knobs
    {
        const int buttonHeight = 30;
        const int buttonWidth = 180;
        auto buttonRow = compPanelArea.reduced(20, 8).removeFromBottom(buttonHeight + 4);

        auto buttonArea = buttonRow.withSizeKeepingCentre(buttonWidth, buttonHeight);
        compingButton.setBounds(buttonArea);
    }

    // No export button in Recording view
    exportCompedButton.setBounds(0, 0, 0, 0);

    // Position the scrollable takes viewport over the remaining takes area
    takesViewport.setBounds(takesAreaBounds);

    // No comped buttons in Recording view
    compedSelectButton.setBounds(0, 0, 0, 0);
    compedSoloButton.setBounds(0, 0, 0, 0);
    layoutTakeLanes();


}

void MainComponent::layoutCompReviewView(juce::Rectangle<int> area)
{
    const int gap = 15;
    area.removeFromTop(gap);

    const int exportHeight = 60;
    compExportArea = area.removeFromBottom(exportHeight);

    auto exportArea = compExportArea.reduced(20, 8);

    const int buttonHeight = 32;
    const int buttonWidth = juce::jmin(300, exportArea.getWidth() - 40);

    auto buttonBounds = exportArea.withSizeKeepingCentre(buttonWidth, buttonHeight);
    exportCompedButton.setBounds(buttonBounds);

    const int trackHeight = juce::jmin(140, area.getHeight() / 3);
    auto trackArea = area.removeFromTop(trackHeight);

    instrumentalLabelBounds = trackArea.removeFromLeft(130);
    instrumentalWaveformBounds = trackArea;

    takesAreaBounds = area;

    const int headerHeight = 22;
    auto headerArea = takesAreaBounds.removeFromTop(headerHeight);
    headerArea = headerArea.reduced(0, 4);

    auto headerRight = headerArea.removeFromRight(160);
    auto soloArea = headerRight.removeFromRight(60);
    auto selectArea = headerRight.removeFromRight(80);

    //selectTakeLabel.setBounds(selectArea);
    //soloLabel.setBounds(soloArea);

    auto headerLeft = headerArea.removeFromLeft(220);
    auto takeVolLabelArea = headerLeft.removeFromLeft(90);
    auto takeVolSliderArea = headerLeft;

    takeVolumeLabel.setBounds(takeVolLabelArea);
    takeVolumeSlider.setBounds(takeVolSliderArea);

    {
        juce::Rectangle<int> row, labelRect, waveRect, controlsRect;
        getCompRowLayout(row, labelRect, waveRect, controlsRect);

        // Match TakeLaneComponent: two pill-shaped buttons sharing the controls area
        auto controlsForButtons = controlsRect;

        auto selectArea = controlsForButtons.removeFromLeft(controlsForButtons.getWidth() / 2);
        compedSelectButton.setBounds(selectArea.reduced(6, 6));
        compedSoloButton.setBounds(controlsForButtons.reduced(6, 6));
    }


    accuracyEmotionLabel.setBounds(0, 0, 0, 0);
    accuracyEmotionSlider.setBounds(0, 0, 0, 0);
    crossfadeLabel.setBounds(0, 0, 0, 0);
    crossfadeSlider.setBounds(0, 0, 0, 0);
    styleLeftLabel.setBounds(0, 0, 0, 0);
    styleRightLabel.setBounds(0, 0, 0, 0);
    crossfadeLeftLabel.setBounds(0, 0, 0, 0);
    crossfadeRightLabel.setBounds(0, 0, 0, 0);
    compingButton.setBounds(0, 0, 0, 0);
    takesViewport.setBounds(0, 0, 0, 0);


}

//==============================================================================
// Time helpers
//==============================================================================

double MainComponent::xToTime(float x) const
{
    const double totalLength = thumbnail.getTotalLength();
    if (totalLength <= 0.0 || instrumentalWaveformBounds.getWidth() <= 0)
        return 0.0;

    const double norm =
        juce::jlimit(0.0, 1.0,
            (x - (double)instrumentalWaveformBounds.getX())
            / (double)instrumentalWaveformBounds.getWidth());

    return norm * totalLength;
}

int MainComponent::timeToX(double t) const
{
    const double totalLength = thumbnail.getTotalLength();
    if (totalLength <= 0.0 || instrumentalWaveformBounds.getWidth() <= 0)
        return instrumentalWaveformBounds.getX();

    const double prop =
        juce::jlimit(0.0, 1.0, t / totalLength);

    return instrumentalWaveformBounds.getX()
        + juce::roundToInt(prop * (double)instrumentalWaveformBounds.getWidth());
}

int MainComponent::compedTimeToX(double t, const juce::Rectangle<int>& area) const
{
    const double totalLength = compedThumbnail.getTotalLength();
    if (totalLength <= 0.0 || area.getWidth() <= 0)
        return area.getX();

    const double prop = juce::jlimit(0.0, 1.0, t / totalLength);
    return area.getX()
        + juce::roundToInt(prop * (double)area.getWidth());
}

void MainComponent::getCompRowLayout(juce::Rectangle<int>& row,
    juce::Rectangle<int>& labelRect,
    juce::Rectangle<int>& waveRect,
    juce::Rectangle<int>& controlsRect) const
{
    row = takesAreaBounds.reduced(4);

    auto tmp = row;
    labelRect = tmp.removeFromLeft(130);   // left info panel
    controlsRect = tmp.removeFromRight(140); // right Select/Solo buttons
    waveRect = tmp;                       // big waveform in the middle
}


//==============================================================================
// Takes view helpers
//==============================================================================

void MainComponent::syncTakeLanesWithTakeTracks()
{
    // Capture take info under lock, then build UI without the lock.
    juce::Array<juce::String> takeNames;
    juce::Array<int>          startSamples;
    juce::Array<int>          numSamples;
    int numTakes = 0;

    {
        const juce::ScopedLock sl(vocalLock);
        numTakes = takeTracks.size();

        takeNames.ensureStorageAllocated(numTakes);
        startSamples.ensureStorageAllocated(numTakes);
        numSamples.ensureStorageAllocated(numTakes);

        for (int i = 0; i < numTakes; ++i)
        {
            const auto& t = takeTracks.getReference(i);
            takeNames.add(t.name);
            startSamples.add(t.startSample);
            numSamples.add(t.numSamples);
        }
    }


    if (numTakes == takeLaneComponents.size())
        return; // already in sync

    takesContainer.removeAllChildren();
    takeLaneComponents.clear(true);

    for (int i = 0; i < numTakes; ++i)
    {
        auto* lane = new TakeLaneComponent(takeNames[i], i);

        // Waveform slice for this take
        lane->setWaveformSource(&vocalWaveBuffer,
            startSamples[i],
            numSamples[i]);

        // All lanes share the same time range = current loop (or 0..loopLen)
        double startSec = loopStartSec;
        double endSec = loopEndSec;
        if (endSec <= startSec && cachedLoopLengthSec > 0.0)
            endSec = startSec + cachedLoopLengthSec;

        lane->setTimeRange(startSec, endSec);
        lane->setSelected(i == selectedTakeIndex);
        lane->setSoloed(i == soloTakeIndex);

        lane->setCallbacks(
            [this](int idx)
            {
                setSelectedTake(idx);
                refreshTakeLaneSelectionStates();
            },
            [this](int idx)
            {
                setSoloTake(idx);
                refreshTakeLaneSelectionStates();
            });

        takesContainer.addAndMakeVisible(lane);
        takeLaneComponents.add(lane);
    }


    layoutTakeLanes();
}

void MainComponent::layoutTakeLanes()
{
    const int width = takesAreaBounds.getWidth();
    const int laneHeight = 64;
    const int laneGap = 4;

    int y = 0;
    for (auto* lane : takeLaneComponents)
    {
        lane->setBounds(0, y, width, laneHeight);
        y += laneHeight + laneGap;
    }

    const int contentHeight = juce::jmax(takesAreaBounds.getHeight(), y);
    takesContainer.setBounds(0, 0, width, contentHeight);

    takesViewport.setBounds(takesAreaBounds);
}

void MainComponent::refreshTakeLaneSelectionStates()
{
    for (auto* lane : takeLaneComponents)
    {
        const int idx = lane->getTakeIndex();
        lane->setSelected(idx == selectedTakeIndex);
        lane->setSoloed(idx == soloTakeIndex);
    }
}

void MainComponent::updateTakeLanePlayhead(double globalTimeSeconds)
{
    for (auto* lane : takeLaneComponents)
        lane->setPlayheadTime(globalTimeSeconds);
}
