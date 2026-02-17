// MainComponent_Views.cpp
#include "MainComponent.h"
#include "CompedAuditionSource.h"

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

    static void drawNeonTooltip(juce::Graphics& g, const juce::String& text, int x, int y, bool centered, const NeonTheme& theme)
    {
        juce::Font font(15.0f);
        int width = font.getStringWidth(text) + 24;
        int height = 34;

        // Calculate bounds
        juce::Rectangle<int> bounds(x, y, width, height);
        if (centered)
            bounds = bounds.withCentre({ x, y });

        // Draw Shadow
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRoundedRectangle(bounds.translated(2, 2).toFloat(), 6.0f);

        // Draw Background (Dark Panel)
        g.setColour(theme.panel.brighter(0.1f));
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);

        // Draw Neon Border
        g.setColour(theme.accentCyan);
        g.drawRoundedRectangle(bounds.toFloat(), 6.0f, 1.5f);

        // Draw Text
        g.setColour(theme.textPrimary);
        g.setFont(font);
        g.drawText(text, bounds, juce::Justification::centred, false);

        // Draw Little "Tail" (Triangle) pointing down if centered
        if (centered)
        {
            juce::Path p;
            p.addTriangle((float)x - 6, (float)bounds.getBottom(),
                (float)x + 6, (float)bounds.getBottom(),
                (float)x, (float)bounds.getBottom() + 6);

            g.setColour(theme.panel.brighter(0.1f));
            g.fillPath(p);
            g.setColour(theme.accentCyan);
            g.strokePath(p, juce::PathStrokeType(1.5f));
        }
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
        const double viewStart = juce::jlimit(0.0, totalLength, visibleStartSec);
        const double viewEnd = juce::jlimit(viewStart + 0.0001, totalLength, visibleEndSec);
        const double viewSpan = juce::jmax(0.0001, viewEnd - viewStart);

        g.setColour(juce::Colours::darkgrey.brighter(0.3f));
        thumbnail.drawChannel(g,
            innerBounds,
            viewStart,
            viewEnd,
            0,
            1.0f);

        drawBeatGrid(g, innerBounds, totalLength);

    



        if (hasValidLoop())
        {
            const double loopVisStart = juce::jmax(loopStartSec, viewStart);
            const double loopVisEnd = juce::jmin(loopEndSec, viewEnd);
            const double startProp = juce::jlimit(0.0, 1.0, (loopVisStart - viewStart) / viewSpan);
            const double endProp = juce::jlimit(0.0, 1.0, (loopVisEnd - viewStart) / viewSpan);

            const int totalW = innerBounds.getWidth();

            const int loopX = innerBounds.getX()
                + juce::roundToInt(startProp * (double)totalW);
            const int loopW = juce::jmax(1,
                juce::roundToInt((endProp - startProp) * (double)totalW));

            juce::Rectangle<int> loopRect(loopX,
                innerBounds.getY(),
                loopW,
                innerBounds.getHeight());

            if (loopVisEnd > loopVisStart + 0.0001)
            {
                g.setColour(juce::Colours::lightgreen);
                thumbnail.drawChannel(g,
                    loopRect,
                    loopVisStart,
                    loopVisEnd,
                    0,
                    1.0f);
            }
        }

        const double current = getPlayheadPositionSec();
        if (current >= 0.0 && totalLength > 0.0)
        {
            const double proportion =
                juce::jlimit(0.0, 1.0, (current - viewStart) / viewSpan);

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
            const int viewLeft = instrumentalWaveformBounds.getX();
            const int viewRight = instrumentalWaveformBounds.getRight();

            g.setColour(juce::Colours::red);

            const bool drawStart = (xStart >= viewLeft && xStart <= viewRight);
            const bool drawEnd = (xEnd >= viewLeft && xEnd <= viewRight);

            if (drawStart)
                g.drawLine((float)xStart, topY, (float)xStart, bottomY, 2.0f);
            if (drawEnd)
                g.drawLine((float)xEnd, topY, (float)xEnd, bottomY, 2.0f);

            const float arrowHeight = 10.0f;
            const float arrowHalfW = 6.0f;

            if (drawStart)
            {
                juce::Path startArrow;
                startArrow.addTriangle((float)xStart, topY,
                    (float)xStart - arrowHalfW, topY - arrowHeight,
                    (float)xStart + arrowHalfW, topY - arrowHeight);
                g.fillPath(startArrow);
            }

            if (drawEnd)
            {
                juce::Path endArrow;
                endArrow.addTriangle((float)xEnd, topY,
                    (float)xEnd - arrowHalfW, topY - arrowHeight,
                    (float)xEnd + arrowHalfW, topY - arrowHeight);
                g.fillPath(endArrow);
            }
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

    // === GRID TOOLTIP LOGIC ===
// Only show if we have an instrumental and BPM is set (intro windows done)
if (thumbnail.getTotalLength() > 0.0 && bpmSet)
{

    // 2. REGULAR MODE: Mouse Hover
    if (isHoveringInstrumental)
    {
        // Offset slightly so it doesn't cover the cursor
        drawNeonTooltip(g, "Shift + Drag to align",
            lastMousePosition.x + 15, lastMousePosition.y + 20,
            false, neonLookAndFeel.getTheme());
    }
}

    
}




//==============================================================================

static double wrapPositive(double v, double period)
{
    if (period <= 0.0) return 0.0;
    v = std::fmod(v, period);
    if (v < 0.0) v += period;
    return v;
}

void MainComponent::drawBeatGrid(juce::Graphics& g,
                                 const juce::Rectangle<int>& innerBounds,
                                 double totalLengthSec) const
{
    if (bpm <= 0 || totalLengthSec <= 0.0)
        return;

    constexpr int beatsPerLine = 2; // << 2 beats between lines
    constexpr int beatsPerBar  = 4; // << strong each 4 beats (i.e., bar lines)

    const double secondsPerBeat = 60.0 / bpm;
    const double gridPeriodSec  = secondsPerBeat * beatsPerLine;

    const double offset = wrapPositive(gridOffsetSec, gridPeriodSec);

    const double n0 = std::ceil((0.0 - offset) / gridPeriodSec);

    for (int i = 0;; ++i)
    {
        const double t = offset + (n0 + i) * gridPeriodSec;
        if (t > totalLengthSec) break;
        if (t < 0.0) continue;

        const int x = timeToX(t);
        if (x < innerBounds.getX() || x >= innerBounds.getRight())
            continue;

        // Beat number for this line relative to the offset
        const long long beatNumber =
            (long long) std::llround((t - offset) / secondsPerBeat);

        const bool isMajor = (beatNumber % beatsPerBar) == 0; // every 4 beats
        const float thickness = isMajor ? 1.3f : 1.0f;

        g.setColour(juce::Colours::white.withAlpha(isMajor ? 0.55f : 0.22f));
        g.drawLine((float)x, (float)innerBounds.getY(),
                   (float)x, (float)innerBounds.getBottom(),
                   thickness);
    }
}

// ----------------------------------------------------------
// Comped-view drawing helpers
// ----------------------------------------------------------

juce::Colour MainComponent::getColourForTake(int takeIndex) const
{
    static juce::Array<juce::Colour> palette = {
        juce::Colour::fromRGB(255, 80, 80),
        juce::Colour::fromRGB(255, 180, 60),
        juce::Colour::fromRGB(255, 240, 80),
        juce::Colour::fromRGB(80, 220, 80),
        juce::Colour::fromRGB(80, 220, 255),
        juce::Colour::fromRGB(80, 120, 255),
        juce::Colour::fromRGB(220, 80, 255),
        juce::Colour::fromRGB(255, 80, 170)
    };

    if (takeIndex <= 0)
        return juce::Colours::darkgrey;

    return palette[(takeIndex - 1) % palette.size()];
}

void MainComponent::drawZebraStripes(juce::Graphics& g,
                                     const juce::Rectangle<int>& r,
                                     juce::Colour c1,
                                     juce::Colour c2) const
{
    if (r.getWidth() <= 0 || r.getHeight() <= 0) return;

    g.setColour(c1);
    g.fillRect(r);

    juce::Graphics::ScopedSaveState ss(g);
    g.reduceClipRegion(r);

    g.setColour(c2);

    const int stripeW = 8;
    const int gap = 6;

    int startX = r.getX() - r.getHeight();

    for (int x = startX; x < r.getRight(); x += stripeW + gap)
    {
        juce::Path p;
        float x0 = (float)x;
        float y0 = (float)r.getY();
        float h  = (float)r.getHeight();
        float w  = (float)stripeW;

        // slant = height to the right
        p.startNewSubPath(x0, y0);
        p.lineTo(x0 + w, y0);
        p.lineTo(x0 + w + h, y0 + h);
        p.lineTo(x0 + h, y0 + h);
        p.closeSubPath();

        g.fillPath(p);
    }
}

void MainComponent::drawCompedTopBar(juce::Graphics& g,
                                     const juce::Rectangle<int>& topBar,
                                     const juce::Rectangle<int>& compWaveArea,
                                     int compResultIndex)
{
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRect(topBar);
    juce::Graphics::ScopedSaveState ss(g);
    g.reduceClipRegion(topBar);

    const bool useActive = (compResultIndex == activeCompResultIndex);
    const auto& rowSegments = useActive
        ? compSegments
        : compResults.getReference(compResultIndex).segments;
    const auto& rowBoundaries = useActive
        ? compBoundaries
        : compResults.getReference(compResultIndex).boundaries;
    const bool rowSelected =
        (compResultIndex >= 0 && compResultIndex < compResults.size())
            ? compResults.getReference(compResultIndex).selected
            : true;

    if (rowSegments.isEmpty())
        return;

    // 1) Fill each segment with its take colour
    for (int i = 0; i < rowSegments.size(); ++i)
    {
        const auto& seg = rowSegments.getReference(i);
        int x1 = compedTimeToX(seg.startSec, compWaveArea);
        int x2 = compedTimeToX(seg.endSec, compWaveArea);

        if (x2 < x1) std::swap(x1, x2);

        juce::Rectangle<int> r(x1, topBar.getY(), x2 - x1, topBar.getHeight());
        g.setColour(getColourForTake(seg.takeIndex));
        g.fillRect(r);

        if (useActive && compDragMode == CompDragMode::SegmentSlip && activeSegmentIndex == i)
        {
            g.setColour(juce::Colours::white.withAlpha(0.95f));
            g.drawRect(r, 2);
        }
    }

    // 2) Overlay zebra stripes in crossfade zones
    for (int b = 0; b < rowBoundaries.size(); ++b)
    {
        const auto& cb = rowBoundaries.getReference(b);
        if (b + 1 >= rowSegments.size()) continue;

        int xs = compedTimeToX(cb.xfadeStartSec, compWaveArea);
        int xe = compedTimeToX(cb.xfadeEndSec, compWaveArea);
        if (xe < xs) std::swap(xs, xe);

        juce::Rectangle<int> r(xs, topBar.getY(), xe - xs, topBar.getHeight());

        drawZebraStripes(g, r,
                         getColourForTake(rowSegments[b].takeIndex),
                         getColourForTake(rowSegments[b + 1].takeIndex));
    }

    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRect(topBar);

    // De-emphasize headers of deselected comp rows for visual clarity.
    if (!rowSelected)
    {
        g.setColour(juce::Colours::darkgrey.withAlpha(0.62f));
        g.fillRect(topBar);
        g.setColour(juce::Colours::grey.withAlpha(0.45f));
        g.drawRect(topBar);
    }
}

float MainComponent::renderCompSampleAtTime(double tSec, int& cachedSeg, int compResultIndex) const
{
    if (compResultIndex != activeCompResultIndex)
        return 0.0f;

    if (compSegments.isEmpty() || compedAuditionSource == nullptr)
        return 0.0f;

    while (cachedSeg + 1 < compSegments.size()
        && tSec >= compSegments.getReference(cachedSeg).endSec)
        ++cachedSeg;

    while (cachedSeg > 0
        && tSec < compSegments.getReference(cachedSeg).startSec)
        --cachedSeg;

    cachedSeg = juce::jlimit(0, compSegments.size() - 1, cachedSeg);

    int seg = cachedSeg;
    const auto& segState = compSegments.getReference(seg);
    const double sourceSec = tSec + segState.sourceOffsetSec;
    float sample = compedAuditionSource
        ->readSampleAtTime(segState.takeIndex, sourceSec);

    auto applyBoundary = [&](int b) -> bool
    {
        if (b < 0 || b >= compBoundaries.size())
            return false;

        const auto& cb = compBoundaries.getReference(b);
        if (!(cb.xfadeEndSec > cb.xfadeStartSec))
            return false;

        if (tSec < cb.xfadeStartSec || tSec >= cb.xfadeEndSec)
            return false;

        const int left = b;
        const int right = b + 1;
        if (right >= compSegments.size())
            return false;

        const auto& leftSeg = compSegments.getReference(left);
        const auto& rightSeg = compSegments.getReference(right);
        const double leftSourceSec = tSec + leftSeg.sourceOffsetSec;
        const double rightSourceSec = tSec + rightSeg.sourceOffsetSec;

        float a = compedAuditionSource->readSampleAtTime(leftSeg.takeIndex, leftSourceSec);
        float bS = compedAuditionSource->readSampleAtTime(rightSeg.takeIndex, rightSourceSec);

        const double u = juce::jlimit(0.0, 1.0,
            (tSec - cb.xfadeStartSec) / (cb.xfadeEndSec - cb.xfadeStartSec));

        float wA = (float)(1.0 - u);
        float wB = (float)(u);

        sample = a * wA + bS * wB;
        return true;
    };

    if (!applyBoundary(seg - 1))
        applyBoundary(seg);

    return sample;
}

void MainComponent::drawCompedWaveformRealtime(juce::Graphics& g,
                                               const juce::Rectangle<int>& area,
                                               int compResultIndex)
{
    const bool useActive = (compResultIndex == activeCompResultIndex);
    const auto& rowSegments = useActive
        ? compSegments
        : compResults.getReference(compResultIndex).segments;

    if (!useActive || compedAuditionSource == nullptr || rowSegments.isEmpty())
    {
        g.setColour(juce::Colours::lightgrey);
        if (compResultIndex >= 0
            && compResultIndex < compResults.size()
            && compResultIndex < kMaxCompResultRows)
        {
            if (auto* rowThumb = compResultThumbnails[(size_t)compResultIndex].get())
            {
                const double rowLen = rowThumb->getTotalLength();
                if (rowLen > 0.0)
                {
                    const double compVisibleStart = juce::jlimit(0.0, rowLen, visibleStartSec - loopStartSec);
                    const double compVisibleEnd = juce::jlimit(0.0, rowLen, visibleEndSec - loopStartSec);
                    if (compVisibleEnd > compVisibleStart + 0.0001)
                        rowThumb->drawChannel(g, area, compVisibleStart, compVisibleEnd, 0, 1.0f);
                    return;
                }
            }
        }

        if (useActive && compedThumbnail.getTotalLength() > 0.0)
        {
            const double compLen = compedThumbnail.getTotalLength();
            const double compVisibleStart = juce::jlimit(0.0, compLen, visibleStartSec - loopStartSec);
            const double compVisibleEnd = juce::jlimit(0.0, compLen, visibleEndSec - loopStartSec);
            if (compVisibleEnd > compVisibleStart + 0.0001)
                compedThumbnail.drawChannel(g, area, compVisibleStart, compVisibleEnd, 0, 1.0f);
        }
        return;
    }

    const double totalLen = compedThumbnail.getTotalLength();
    const double sr = compedAuditionSource->getSampleRateHz();
    if (totalLen <= 0.0 || sr <= 0.0)
        return;

    g.setColour(juce::Colours::lightgrey);

    const int w = area.getWidth();
    const float mid = (float)area.getCentreY();
    const float amp = (float)area.getHeight() * 0.5f;
    const double compVisibleStart = juce::jlimit(0.0, totalLen, visibleStartSec - loopStartSec);
    const double compVisibleEnd = juce::jlimit(0.0, totalLen, visibleEndSec - loopStartSec);
    if (compVisibleEnd <= compVisibleStart + 0.0001)
        return;
    const double compVisibleSpan = juce::jmax(0.0001, compVisibleEnd - compVisibleStart);

    int cachedSeg = 0;

    for (int x = 0; x < w; ++x)
    {
        const double t0 = compVisibleStart + compVisibleSpan * (double)x / (double)w;
        const double t1 = compVisibleStart + compVisibleSpan * (double)(x + 1) / (double)w;

        int s0 = (int)std::floor(t0 * sr);
        int s1 = (int)std::floor(t1 * sr);
        if (s1 <= s0) s1 = s0 + 1;

        float minV = 1.0f, maxV = -1.0f;
        const int step = juce::jmax(1, (s1 - s0) / 8);

        for (int s = s0; s <= s1; s += step)
        {
            double t = (double)s / sr;
            float v = renderCompSampleAtTime(t, cachedSeg, compResultIndex);
            minV = juce::jmin(minV, v);
            maxV = juce::jmax(maxV, v);
        }

        const int drawX = area.getX() + x;
        g.drawLine((float)drawX, mid - maxV * amp,
                   (float)drawX, mid - minV * amp);
    }
}



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
        const double viewStart = juce::jlimit(0.0, instrumentalLength, visibleStartSec);
        const double viewEnd = juce::jlimit(viewStart + 0.0001, instrumentalLength, visibleEndSec);
        const double viewSpan = juce::jmax(0.0001, viewEnd - viewStart);

        g.setColour(juce::Colours::darkgrey.brighter(0.3f));
        thumbnail.drawChannel(g,
            innerBounds,
            viewStart,
            viewEnd,
            0,
            1.0f);

        drawBeatGrid(g, innerBounds, instrumentalLength);
        // === GRID DRAWING END ===


        if (hasValidLoop())
        {
            const double loopVisStart = juce::jmax(loopStartSec, viewStart);
            const double loopVisEnd = juce::jmin(loopEndSec, viewEnd);
            const double startProp =
                juce::jlimit(0.0, 1.0, (loopVisStart - viewStart) / viewSpan);
            const double endProp =
                juce::jlimit(0.0, 1.0, (loopVisEnd - viewStart) / viewSpan);

            const int totalW = innerBounds.getWidth();

            const int loopX = innerBounds.getX()
                + juce::roundToInt(startProp * (double)totalW);
            const int loopW = juce::jmax(1,
                juce::roundToInt((endProp - startProp) * (double)totalW));

            juce::Rectangle<int> loopRect(loopX,
                innerBounds.getY(),
                loopW,
                innerBounds.getHeight());

            if (loopVisEnd > loopVisStart + 0.0001)
            {
                g.setColour(juce::Colours::lightgreen);
                thumbnail.drawChannel(g,
                    loopRect,
                    loopVisStart,
                    loopVisEnd,
                    0,
                    1.0f);
            }
        }

        const double current = getPlayheadPositionSec();
        if (current >= 0.0 && instrumentalLength > 0.0)
        {
            const double proportion =
                juce::jlimit(0.0, 1.0, (current - viewStart) / viewSpan);

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
            const int viewLeft = instrumentalWaveformBounds.getX();
            const int viewRight = instrumentalWaveformBounds.getRight();

            g.setColour(juce::Colours::red);

            const bool drawStart = (xStart >= viewLeft && xStart <= viewRight);
            const bool drawEnd = (xEnd >= viewLeft && xEnd <= viewRight);

            if (drawStart)
                g.drawLine((float)xStart, topY, (float)xStart, bottomY, 2.0f);
            if (drawEnd)
                g.drawLine((float)xEnd, topY, (float)xEnd, bottomY, 2.0f);

            const float arrowHeight = 10.0f;
            const float arrowHalfW = 6.0f;

            if (drawStart)
            {
                juce::Path startArrow;
                startArrow.addTriangle((float)xStart, topY,
                    (float)xStart - arrowHalfW, topY - arrowHeight,
                    (float)xStart + arrowHalfW, topY - arrowHeight);
                g.fillPath(startArrow);
            }

            if (drawEnd)
            {
                juce::Path endArrow;
                endArrow.addTriangle((float)xEnd, topY,
                    (float)xEnd - arrowHalfW, topY - arrowHeight,
                    (float)xEnd + arrowHalfW, topY - arrowHeight);
                g.fillPath(endArrow);
            }
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

    const bool canDrawComped = hasLastCompResult && !compResults.isEmpty();

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

    // Use Neon theme so it matches TakeLaneComponent
    const NeonTheme* tPtr = nullptr;
    if (auto* neon = dynamic_cast<NeonLookAndFeel*>(&getLookAndFeel()))
        tPtr = &neon->getTheme();

    auto panelCol = tPtr ? tPtr->panel : juce::Colours::darkgrey.darker(0.6f);
    auto selectCol = tPtr ? tPtr->accentCyan : juce::Colours::cyan;
    auto soloCol = tPtr ? tPtr->accentPink : juce::Colours::orange;
    auto textCol = tPtr ? tPtr->textSecondary : juce::Colours::white;

    const int rowCount = juce::jlimit(1, 3, compResults.size());
    juce::Rectangle<int> row, labelRect, waveRect, controlsRect;
    juce::Rectangle<int> activeTopBarRect;
    juce::Rectangle<int> activeCompWaveArea;

    for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex)
    {
        const auto& result = compResults.getReference(rowIndex);
        const bool rowIsActive = (rowIndex == activeCompResultIndex);

        getCompRowLayout(rowIndex, rowCount, row, labelRect, waveRect, controlsRect);

        g.setColour(panelCol);
        g.fillRoundedRectangle(row.toFloat(), 4.0f);

        auto waveOuter = waveRect.reduced(6, 8);
        g.setColour(panelCol.darker(0.5f));
        g.fillRect(waveOuter);
        g.setColour(panelCol.brighter(0.25f));
        g.drawRect(waveOuter);

        if (result.solo)
        {
            g.setColour(soloCol.withAlpha(0.12f));
            g.fillRoundedRectangle(row.toFloat(), 4.0f);
        }
        if (result.selected)
        {
            g.setColour(selectCol.withAlpha(0.9f));
            g.drawRoundedRectangle(row.toFloat().expanded(0.5f), 4.0f, 1.5f);
        }

        auto labelArea = labelRect.reduced(8, 4);
        juce::Rectangle<int> titleArea = labelArea.removeFromTop(22);
        juce::Rectangle<int> accArea = labelArea.removeFromTop(18);
        juce::Rectangle<int> emoArea = labelArea.removeFromTop(18);
        juce::Rectangle<int> cfArea = labelArea.removeFromTop(18);

        const int acc = juce::jlimit(0, 100, result.alphaPct);
        const int emo = juce::jlimit(0, 100, 100 - result.alphaPct);
        const int cf = juce::jlimit(0, 100, result.crossfadePct);

        g.setColour(textCol);
        g.setFont(16.0f);
        g.drawText(getCompResultTitle(rowIndex), titleArea, juce::Justification::centredLeft, true);
        g.setFont(14.0f);
        g.drawText("Accuracy " + juce::String(acc) + "%", accArea, juce::Justification::centredLeft, true);
        g.drawText("Emotion " + juce::String(emo) + "%", emoArea, juce::Justification::centredLeft, true);
        g.drawText("Crossfade " + juce::String(cf) + "%", cfArea, juce::Justification::centredLeft, true);

        auto inner = waveOuter.reduced(4);
        const int topBarHeight = 22;
        auto topBarRect = inner.removeFromTop(topBarHeight);
        auto compWaveArea = inner;

        drawCompedTopBar(g, topBarRect, compWaveArea, rowIndex);
        drawCompedWaveformRealtime(g, compWaveArea, rowIndex);

        if (rowIsActive)
        {
            activeTopBarRect = topBarRect;
            activeCompWaveArea = compWaveArea;
        }
    }

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (!activeTopBarRect.isEmpty() && segmentTimingTipShowUntilMs > nowMs)
    {
        drawNeonTooltip(g,
                        "Shift + Drag to edit the segment timing",
                        activeTopBarRect.getCentreX(),
                        activeTopBarRect.getY() - 24,
                        true,
                        neonLookAndFeel.getTheme());
    }

    if (!activeCompWaveArea.isEmpty() && compBoundaries.size() > 0)
    {
        {
            juce::Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(activeCompWaveArea);
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            for (int b = 0; b < compBoundaries.size(); ++b)
            {
                const auto& cb = compBoundaries.getReference(b);
                int x1 = compedTimeToX(cb.xfadeStartSec, activeCompWaveArea);
                int x2 = compedTimeToX(cb.xfadeEndSec, activeCompWaveArea);
                if (x2 < x1) std::swap(x1, x2);
                g.fillRect(juce::Rectangle<int>(x1, activeCompWaveArea.getY(),
                    juce::jmax(1, x2 - x1), activeCompWaveArea.getHeight()));
            }
        }

        {
            juce::Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(activeTopBarRect.getUnion(activeCompWaveArea));
            for (int b = 0; b < compBoundaries.size(); ++b)
            {
                const auto& cb = compBoundaries.getReference(b);
                const int x1 = compedTimeToX(cb.xfadeStartSec, activeCompWaveArea);
                const int x2 = compedTimeToX(cb.xfadeEndSec, activeCompWaveArea);
                const bool isActive = (b == activeBoundaryIndex && compDragMode != CompDragMode::None);
                g.setColour(juce::Colours::white.withAlpha(isActive ? 1.0f : 0.85f));
                const float thickness = isActive ? 2.5f : 2.0f;
                g.drawLine((float)x1, (float)activeTopBarRect.getY(), (float)x1, (float)activeCompWaveArea.getBottom(), thickness);
                g.drawLine((float)x2, (float)activeTopBarRect.getY(), (float)x2, (float)activeCompWaveArea.getBottom(), thickness);
            }
        }
    }

    const double compPos = takeTransport.getCurrentPosition();
    const double compLength = compedThumbnail.getTotalLength();
    if (!activeCompWaveArea.isEmpty() && compPos >= 0.0 && compLength > 0.0)
    {
        const double compVisibleStart = juce::jlimit(0.0, compLength, visibleStartSec - loopStartSec);
        const double compVisibleEnd = juce::jlimit(0.0, compLength, visibleEndSec - loopStartSec);
        if (compVisibleEnd > compVisibleStart + 0.0001)
        {
            const double compVisibleSpan = juce::jmax(0.0001, compVisibleEnd - compVisibleStart);
            const int x = activeCompWaveArea.getX()
                + juce::roundToInt(juce::jlimit(0.0, 1.0, (compPos - compVisibleStart) / compVisibleSpan)
                                   * (double)activeCompWaveArea.getWidth());
            g.setColour(juce::Colours::yellow);
            g.drawLine((float)x, (float)activeCompWaveArea.getY(), (float)x, (float)activeCompWaveArea.getBottom(), 2.0f);
        }
    }

    {
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(activeTopBarRect.getUnion(activeCompWaveArea));
        g.setFont(14.0f);
        g.setColour(juce::Colours::white);
        for (int i = 0; i < compSegments.size(); ++i)
        {
            const auto& seg = compSegments.getReference(i);
            if (!(seg.endSec > seg.startSec) || activeCompWaveArea.isEmpty())
                continue;

            const int xStart = compedTimeToX(seg.startSec, activeCompWaveArea);
            const int xEnd = compedTimeToX(seg.endSec, activeCompWaveArea);
            g.setColour(juce::Colours::lightgreen);
            g.drawLine((float)xStart, (float)activeTopBarRect.getY(), (float)xStart, (float)activeCompWaveArea.getBottom(), 2.0f);
            const int midX = xStart + (xEnd - xStart) / 2;

            if (i > 0)
            {
                const bool isActive = (compDragMode == CompDragMode::SegmentBoundary && activeBoundaryIndex == (i - 1));
                g.setColour(isActive ? juce::Colours::lime : juce::Colours::lightgreen);
                juce::Path tri;
                tri.addTriangle((float)xStart, (float)activeTopBarRect.getY(),
                                (float)xStart - 6.0f, (float)activeTopBarRect.getY() - 8.0f,
                                (float)xStart + 6.0f, (float)activeTopBarRect.getY() - 8.0f);
                g.fillPath(tri);
            }

            juce::Rectangle<int> labelBox(midX - 15, activeTopBarRect.getY(), 30, activeTopBarRect.getHeight());
            g.setColour(juce::Colours::white);
            g.drawText(seg.takeIndex > 0 ? juce::String(seg.takeIndex) : "-", labelBox, juce::Justification::centred, true);
        }
    }

    if (zoomScaleTipShowUntilMs > nowMs && !instrumentalWaveformBounds.isEmpty())
    {
        drawNeonTooltip(g,
                        "Ctrl + wheel to scale",
                        instrumentalWaveformBounds.getX() + 26,
                        instrumentalWaveformBounds.getY() + 26,
                        false,
                        neonLookAndFeel.getTheme());
    }
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

    topRow.removeFromLeft(8);
    auto keyArea = topRow.removeFromLeft(125);
    keyLabel.setBounds(keyArea);
    keyBounds = keyArea;

    topRow.removeFromLeft(10);
    metronomeToggle.setBounds(topRow.removeFromLeft(110));

    if (viewMode == ViewMode::Recording)
        layoutRecordingView(area);
    else
        layoutCompReviewView(area);

    if (viewMode == ViewMode::CompReview)
        refreshCompedButtons();

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
    const int knobSize = 120; // 100?200px range as you wanted
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
        
        // Example placement near Accuracy/Emotion slider (adjust to your layout)
        auto toggleArea = knobArea.removeFromTop(24);
        mlModeToggle.setBounds(toggleArea.removeFromLeft(180));

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
    for (int i = 0; i < compedSelectButtons.size(); ++i)
    {
        if (auto* b = compedSelectButtons[i]) b->setBounds(0, 0, 0, 0);
        if (auto* b = compedSoloButtons[i]) b->setBounds(0, 0, 0, 0);
    }
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

    const int rowCount = juce::jmax(1, juce::jmin(3, compResults.size()));
    for (int rowIndex = 0; rowIndex < compedSelectButtons.size(); ++rowIndex)
    {
        auto* selectBtn = compedSelectButtons[rowIndex];
        auto* soloBtn = compedSoloButtons[rowIndex];
        if (selectBtn == nullptr || soloBtn == nullptr)
            continue;

        if (rowIndex >= rowCount)
        {
            selectBtn->setBounds(0, 0, 0, 0);
            soloBtn->setBounds(0, 0, 0, 0);
            continue;
        }

        juce::Rectangle<int> row, labelRect, waveRect, controlsRect;
        getCompRowLayout(rowIndex, rowCount, row, labelRect, waveRect, controlsRect);

        auto controlsForButtons = controlsRect.reduced(6);
        const int gapPx = 8;
        auto leftArea = controlsForButtons.removeFromLeft(controlsForButtons.getWidth() / 2);
        controlsForButtons.removeFromLeft(gapPx);
        auto rightArea = controlsForButtons;
        int side = juce::jmin(
            juce::jmin(leftArea.getWidth(), rightArea.getWidth()),
            juce::jmin(leftArea.getHeight(), rightArea.getHeight()));

        selectBtn->setBounds(leftArea.withSizeKeepingCentre(side, side));
        soloBtn->setBounds(rightArea.withSizeKeepingCentre(side, side));
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
    mlModeToggle.setBounds(0, 0, 0, 0);


}

//==============================================================================
// Time helpers
//==============================================================================

double MainComponent::xToTime(float x) const
{
    const double totalLength = thumbnail.getTotalLength();
    if (totalLength <= 0.0 || instrumentalWaveformBounds.getWidth() <= 0)
        return 0.0;

    const double visibleStart = juce::jlimit(0.0, totalLength, visibleStartSec);
    const double visibleEnd = juce::jlimit(visibleStart + 0.0001, totalLength, visibleEndSec);
    const double visibleSpan = juce::jmax(0.0001, visibleEnd - visibleStart);

    const double norm =
        juce::jlimit(0.0, 1.0,
            (x - (double)instrumentalWaveformBounds.getX())
            / (double)instrumentalWaveformBounds.getWidth());

    return visibleStart + norm * visibleSpan;
}

int MainComponent::timeToX(double t) const
{
    const double totalLength = thumbnail.getTotalLength();
    if (totalLength <= 0.0 || instrumentalWaveformBounds.getWidth() <= 0)
        return instrumentalWaveformBounds.getX();

    const double visibleStart = juce::jlimit(0.0, totalLength, visibleStartSec);
    const double visibleEnd = juce::jlimit(visibleStart + 0.0001, totalLength, visibleEndSec);
    const double visibleSpan = juce::jmax(0.0001, visibleEnd - visibleStart);
    const double prop = (t - visibleStart) / visibleSpan;

    return instrumentalWaveformBounds.getX()
        + juce::roundToInt(prop * (double)instrumentalWaveformBounds.getWidth());
}

int MainComponent::compedTimeToX(double t, const juce::Rectangle<int>& area) const
{
    const double compLength = compedThumbnail.getTotalLength();
    if (compLength <= 0.0 || area.getWidth() <= 0)
        return area.getX();

    const double compVisibleStart = juce::jlimit(0.0, compLength, visibleStartSec - loopStartSec);
    const double compVisibleEnd = juce::jlimit(0.0, compLength, visibleEndSec - loopStartSec);
    if (compVisibleEnd <= compVisibleStart + 0.0001)
    {
        const double fallbackProp = t / compLength;
        return area.getX() + juce::roundToInt(fallbackProp * (double)area.getWidth());
    }

    const double prop = (t - compVisibleStart) / (compVisibleEnd - compVisibleStart);
    return area.getX()
        + juce::roundToInt(prop * (double)area.getWidth());
}

void MainComponent::getCompRowLayout(int rowIndex,
    int rowCount,
    juce::Rectangle<int>& row,
    juce::Rectangle<int>& labelRect,
    juce::Rectangle<int>& waveRect,
    juce::Rectangle<int>& controlsRect) const
{
    const int safeCount = juce::jmax(1, rowCount);
    const int safeIndex = juce::jlimit(0, safeCount - 1, rowIndex);

    auto allRows = takesAreaBounds.reduced(4);
    const int gap = 6;
    const int totalGap = gap * (safeCount - 1);
    const int rowH = juce::jmax(60, (allRows.getHeight() - totalGap) / safeCount);
    row = juce::Rectangle<int>(
        allRows.getX(),
        allRows.getY() + safeIndex * (rowH + gap),
        allRows.getWidth(),
        rowH);

    auto tmp = row;
    labelRect = tmp.removeFromLeft(130);   // left info panel
    controlsRect = tmp.removeFromRight(140); // right Select/Solo buttons
    waveRect = tmp;                       // big waveform in the middle
}

void MainComponent::getCompRowLayout(juce::Rectangle<int>& row,
    juce::Rectangle<int>& labelRect,
    juce::Rectangle<int>& waveRect,
    juce::Rectangle<int>& controlsRect) const
{
    getCompRowLayout(0, 1, row, labelRect, waveRect, controlsRect);
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
            // SELECT toggle
            [this](int idx)
            {
                if (selectedTakeIndex == idx)
                {
                    selectedTakeIndex = -1;
                    takeTransport.stop();   // or stopAllPlayback();
                }
                else
                {
                    setSelectedTake(idx);   // this already starts playback
                }

                refreshTakeLaneSelectionStates();
                repaint();
            },

            // SOLO toggle
            [this](int idx)
            {
                if (soloTakeIndex == idx)
                {
                    soloTakeIndex = -1;
                    takeTransport.stop();   // or stopAllPlayback();
                }
                else
                {
                    setSoloTake(idx);       // this already starts playback
                }

                refreshTakeLaneSelectionStates();
                repaint();
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
