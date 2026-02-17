// MainComponent_Interaction.cpp
#include "MainComponent.h"
#include "CompedAuditionSource.h"
#include <cmath>

//==============================================================================

void MainComponent::buttonClicked(juce::Button* button)
{
    
    
    
    if (button == &importButton)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Import Instrumental...");
        menu.addItem(2, "Import Takes...");

        menu.showMenuAsync(juce::PopupMenu::Options(),
            [this](int result)
            {
                if (result == 1)
                    importInstrumental();
                else if (result == 2)
                    importTakesFromFiles();
            });

        return;
    }
    else if (button == &playButton)
    {
        const bool haveInstrumental = (readerSource.get() != nullptr);

        if (haveInstrumental)
        {
            if (!bpmSet)
            {
                promptForBpm();
                return;
            }

            transportSource.setPosition(getEffectiveLoopStartSec());
            transportSource.start();
        }

        if (viewMode == ViewMode::Recording)
        {
            if (selectedTakeIndex >= 0 || soloTakeIndex >= 0)
            {
                const bool soloMode = (soloTakeIndex >= 0);
                const int  indexToUse = soloMode ? soloTakeIndex : selectedTakeIndex;

                if (takeReaderSource == nullptr)
                {
                    if (soloMode)
                        setSoloTake(indexToUse);
                    else
                        setSelectedTake(indexToUse);
                }

                if (takeReaderSource != nullptr)
                {
                    takeTransport.setPosition(getEffectiveCompLoopStartSec());
                    takeTransport.start();
                }
            }
        }
        else if (viewMode == ViewMode::CompReview)
        {
            const bool haveCompedPlaybackSource =
                (compedAuditionSource != nullptr) || (takeReaderSource != nullptr);

            if (haveCompedPlaybackSource && (compedSelected || compedSolo))
            {
                takeTransport.setPosition(getEffectiveCompLoopStartSec());
                takeTransport.start();
            }
        }
    }
    else if (button == &stopButton)
    {
        if (isRecording)
            stopRecording();
        else
            transportSource.stop();

        takeTransport.stop();
    }
    else if (button == &saveProjectButton)
    {
        saveProjectToFile();
    }
    else if (button == &loadProjectButton)
    {
        loadProjectFromFile();
    }
    else if (button == &resetButton)
    {
        resetProjectState();
    }
    else if (button == &compingButton)
    {
        // Prevent multiple dialogs if user spam-clicks
        if (compingDialogWindow != nullptr)
            return;
        
        // --- EARLY GUARD (before opening progress window) ---
        const int alphaPct = juce::jlimit(0, 100, juce::roundToInt(accuracyEmotionSlider.getValue()));
        const int crossfadePct = juce::jlimit(0, 100, juce::roundToInt(crossfadeSlider.getValue()));

        const juce::String compedName =
            "comped-" + juce::String(alphaPct) + "-" + juce::String(crossfadePct) + ".wav";
        juce::File compedTargetFile = currentPhraseDirectory.getChildFile(compedName);

        if (compedTargetFile.existsAsFile())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Comping done already with these values",
                "If you want to record/import more takes:\n\n"
                "reset project -> load example again.\n"
                "You can reimport the takes from the current phrase folder.\n\n");
            return;
        }

        auto* content = new CompingProgressComponent(neonLookAndFeel);
        compingProgressComponent = content;

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(content);
        opts.dialogTitle = "Magic (algorithms) is happening.";
        opts.dialogBackgroundColour = neonLookAndFeel.getTheme().background;
        opts.escapeKeyTriggersCloseButton = false;   // user can’t dismiss manually
        opts.useNativeTitleBar = false;
        opts.resizable = false;
        opts.useBottomRightCornerResizer = false;
        //opts.runModal = false;
        opts.componentToCentreAround = this;

        compingDialogWindow = opts.launchAsync();

        runCompingFromGui();
        return;
    }
    else if (button == &recordButton)
    {
        if (!isRecording)
        {
            if (readerSource.get() == nullptr || !hasValidLoop() || !bpmSet)
                return;

            if (fullRecordingIndex == 0)
            {
                loopLocked = true;
                cachedLoopLengthSec = loopEndSec - loopStartSec;
            }

            ++fullRecordingIndex;

            juce::File baseDir = currentPhraseDirectory;
            baseDir.createDirectory();

            juce::File fullFile = baseDir.getChildFile(
                "full_" + juce::String(fullRecordingIndex) + ".wav");

            currentFullRecordingFile = fullFile;

            std::unique_ptr<juce::FileOutputStream> outStream(fullFile.createOutputStream());

            if (outStream == nullptr || !outStream->openedOk())
            {
                --fullRecordingIndex;
                return;
            }

            double writerSampleRate = currentSampleRate;
            if (writerSampleRate <= 0.0)
                writerSampleRate = 44100.0;

            {
                const juce::ScopedLock sl(writerLock);
                recordingWriter.reset(
                    wavFormat.createWriterFor(outStream.release(),
                        writerSampleRate,
                        1,
                        16,
                        {},
                        0));
            }

            if (recordingWriter == nullptr)
            {
                --fullRecordingIndex;
                return;
            }

            loopLengthSamples = (cachedLoopLengthSec > 0.0 && currentSampleRate > 0.0)
                ? juce::roundToInt(cachedLoopLengthSec * currentSampleRate)
                : 0;

            {
                const juce::ScopedLock sl(vocalLock);

                if (fullRecordingIndex == 1)
                {
                    totalRecordedSamples = 0;
                    takeTracks.clear();

                    const double maxRecordingSeconds = 5.0 * 60.0;
                    vocalBufferCapacitySamples = (int)(currentSampleRate * maxRecordingSeconds);
                    if (vocalBufferCapacitySamples <= 0)
                        vocalBufferCapacitySamples = 44100 * 60;

                    vocalWaveBuffer.setSize(1,
                        vocalBufferCapacitySamples,
                        false,
                        false,
                        false);

                    const int maxExpectedTakes =
                        (loopLengthSamples > 0 && cachedLoopLengthSec > 0.0)
                        ? juce::jmax(32, (int)(maxRecordingSeconds / cachedLoopLengthSec) + 4)
                        : 256;

                    takeTracks.ensureStorageAllocated(maxExpectedTakes);
                }
            }

            takeTransport.stop();

            transportSource.setPosition(loopStartSec);
            transportSource.start();

            isRecording = true;
            recordButton.setButtonText("Stop Rec");
        }
        else
        {
            stopRecording();
        }
    }
    else if (button == &ioButton)
    {
        auto* selector = new juce::AudioDeviceSelectorComponent(
            deviceManager,
            1, 4,
            0, 2,
            true,
            true,
            true,
            false);

        selector->setSize(500, 400);

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(selector);
        opts.dialogTitle = "Audio IN/OUT";
        opts.dialogBackgroundColour = juce::Colours::darkgrey;
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar = true;
        opts.resizable = true;
        opts.componentToCentreAround = this;

        opts.launchAsync();
    }
    else
    {
        int compButtonRow = -1;
        bool isSelectButton = false;
        for (int i = 0; i < compedSelectButtons.size(); ++i)
        {
            if (button == compedSelectButtons[i])
            {
                compButtonRow = i;
                isSelectButton = true;
                break;
            }
            if (button == compedSoloButtons[i])
            {
                compButtonRow = i;
                isSelectButton = false;
                break;
            }
        }

        if (compButtonRow >= 0 && compButtonRow < compResults.size())
        {
            setActiveCompResult(compButtonRow, true);

            const bool canPlayComped =
                (!isRecording && ((compedAuditionSource != nullptr) || (takeReaderSource != nullptr)));

            if (isSelectButton)
            {
                // "Select" acts as explicit row selection (radio-style).
                for (int i = 0; i < compResults.size(); ++i)
                {
                    auto& r = compResults.getReference(i);
                    r.selected = (i == compButtonRow);
                    if (i != compButtonRow)
                        r.solo = false;
                }

                compedSelected = true;
                compedSolo = false;

                if (compButtonRow >= 0 && compButtonRow < compResults.size())
                {
                    auto& active = compResults.getReference(compButtonRow);
                    active.selected = true;
                    active.solo = false;
                }

                if (canPlayComped)
                {
                    startPlaybackForSelection(true, true);
                }
            }
            else
            {
                if (compedSolo)
                    compedSolo = false;
                else
                {
                    compedSolo = true;
                    compedSelected = false;
                }

                if (canPlayComped)
                {
                    if (compedSolo)
                        startPlaybackForSelection(false, true);
                    else
                        takeTransport.stop();
                }
            }

            syncActiveCompResultFromLegacyState();
            refreshCompedButtons();
            repaint();
            return;
        }
    }
    if (button == &recordingTabButton)
    {
        transportSource.stop();               
        takeTransport.stop();
        viewMode = ViewMode::Recording;
        updateTabButtonStyles();
        resized();
        repaint();
    }
    else if (button == &compedTabButton)
    {
        if (!hasLastCompResult)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Comped view unavailable",
                "You need to run comping at least once before using the Comped tab.");
            return;
        }

        if (!hasCompedThumbnail && compSegments.isEmpty())
        {
            if (!loadLastCompForReview())
                DBG("CompReview: loadLastCompForReview() failed");
        }

        // Source is already prepared by loadLastCompForReview()/setActiveCompResult.
        if (compedAuditionSource != nullptr)
            takeTransport.setLooping(true);


        transportSource.stop();             
        takeTransport.stop();

        const double totalLength = thumbnail.getTotalLength();
        if (totalLength > 0.0 && visibleEndSec <= visibleStartSec + 0.0001)
        {
            visibleStartSec = 0.0;
            visibleEndSec = totalLength;
        }

        viewMode = ViewMode::CompReview;
        updateTabButtonStyles();
        resized();
        refreshCompedButtons();
        repaint();
    }
    else if (button == &exportCompedButton)
    {
        if (viewMode != ViewMode::CompReview)
            return;

        if (compSegments.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Nothing to export",
                "No comped segments available.\nRun COMPING first.");
            return;
        }

        juce::File initialFile = currentPhraseDirectory
            .getChildFile(lastCompedFile.getFileName());

        fileChooser = std::make_unique<juce::FileChooser>(
            "Export comped take as...",
            initialFile,
            "*.wav");

        auto flags = juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync(flags,
            [this](const juce::FileChooser& fc)
            {
                auto target = fc.getResult();
                fileChooser.reset();

                if (!target.getFullPathName().isNotEmpty())
                    return;

                if (target.getFileExtension().isEmpty())
                    target = target.withFileExtension(".wav");

                // Export EXACT audition (Option 2)
                exportCompedAuditionToFileAsync(target);
            });

        return;
    }

    else if (button == &metronomeToggle)
    {
        metronomeOn = metronomeToggle.getToggleState();
    }
}

//==============================================================================

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        const bool isPlaying = transportSource.isPlaying() || takeTransport.isPlaying() || isRecording;
        if (isPlaying)
            stopButton.triggerClick();
        else
            playButton.triggerClick();
        return true;
    }

    return juce::Component::keyPressed(key);
}

//==============================================================================

void MainComponent::timerCallback()
{
    syncTakeLanesWithTakeTracks();
    if (transportSource.isPlaying() || takeTransport.isPlaying())
        enforceLoopWrap();
    
    // If take is looping but instrumental stopped, restart instrumental
    if (!transportSource.isPlaying() && takeTransport.isPlaying())
    {
        if (hasValidLoop())
            transportSource.setPosition(getEffectiveLoopStartSec());
        else
            transportSource.setPosition(0.0);

        transportSource.start();
    }

    if (viewMode == ViewMode::Recording)
    {
        // Keep number of lanes in sync with the take list
        syncTakeLanesWithTakeTracks();

        // Compute a global time in seconds for the playhead
        double globalTime = getPlayheadPositionSec();

        if (hasValidLoop())
        {
            // When only the take is playing (no instrumental), align it to the loop
            if (!transportSource.isPlaying() && takeTransport.isPlaying())
                globalTime = loopStartSec + takeTransport.getCurrentPosition();
        }
        
        

        updateTakeLanePlayhead(globalTime);
    }

    if (transportSource.isPlaying() || takeTransport.isPlaying())


        repaint();
}

//==============================================================================

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &thumbnail)
    {
        const double totalLength = thumbnail.getTotalLength();
        if (totalLength > 0.0 && visibleEndSec <= visibleStartSec + 0.0001)
        {
            visibleStartSec = 0.0;
            visibleEndSec = totalLength;
        }
        repaint();
    }
    else if (source == &compedThumbnail)
    {
        if (compedThumbnail.getTotalLength() > 0.0)
            hasCompedThumbnail = true;

        repaint();
    }
    else
    {
        for (const auto& rowThumb : compResultThumbnails)
        {
            if (source == rowThumb.get())
            {
                repaint();
                return;
            }
        }
    }
}



bool MainComponent::getCompWaveAreaForInteraction(juce::Rectangle<int>& outCompWaveArea) const
{
    if (!hasCompedThumbnail)
        return false;

    if (compedThumbnail.getTotalLength() <= 0.0)
        return false;

    const int rowCount = juce::jmax(1, juce::jmin(3, compResults.size()));
    const int rowIndex = juce::jlimit(0, rowCount - 1, activeCompResultIndex >= 0 ? activeCompResultIndex : 0);
    juce::Rectangle<int> row, labelRect, waveRect, controlsRect;
    getCompRowLayout(rowIndex, rowCount, row, labelRect, waveRect, controlsRect);

    auto waveOuter = waveRect.reduced(6, 8);
    auto inner = waveOuter.reduced(4);
    const int topBarHeight = 22;
    inner.removeFromTop(topBarHeight);
    outCompWaveArea = inner;

    return outCompWaveArea.getWidth() > 0 && outCompWaveArea.getHeight() > 0;
}

bool MainComponent::getCompWaveAndTopBarForInteraction(juce::Rectangle<int>& compWaveArea,
                                                       juce::Rectangle<int>& topBarRect) const
{
    if (!hasCompedThumbnail || compedThumbnail.getTotalLength() <= 0.0)
        return false;

    const int rowCount = juce::jmax(1, juce::jmin(3, compResults.size()));
    const int rowIndex = juce::jlimit(0, rowCount - 1, activeCompResultIndex >= 0 ? activeCompResultIndex : 0);
    juce::Rectangle<int> row, labelRect, waveRect, controlsRect;
    getCompRowLayout(rowIndex, rowCount, row, labelRect, waveRect, controlsRect);

    auto waveOuter = waveRect.reduced(6, 8);
    auto inner = waveOuter.reduced(4);
    const int topBarHeight = 22;

    topBarRect = inner.removeFromTop(topBarHeight);
    compWaveArea = inner;

    return compWaveArea.getWidth() > 0 && compWaveArea.getHeight() > 0;
}

double MainComponent::compedXToTime(int x, const juce::Rectangle<int>& compWaveArea) const
{
    const double compLength = compedThumbnail.getTotalLength();
    if (compLength <= 0.0 || compWaveArea.getWidth() <= 0)
        return 0.0;

    const double prop = juce::jlimit(
        0.0, 1.0,
        (double)(x - compWaveArea.getX()) / (double)compWaveArea.getWidth());

    const double compVisibleStart = juce::jlimit(0.0, compLength, visibleStartSec - loopStartSec);
    const double compVisibleEnd = juce::jlimit(0.0, compLength, visibleEndSec - loopStartSec);
    if (compVisibleEnd <= compVisibleStart + 0.0001)
        return prop * compLength;

    return compVisibleStart + prop * (compVisibleEnd - compVisibleStart);
}



//==============================================================================
// Mouse handling
//==============================================================================

static constexpr int kXFadeHitPx = 6;
static constexpr double kMinXFadeWinSec = 0.005; // 5ms
static constexpr int kSegHandleHitPx = 8;
static constexpr double kMinSegDurationSec = 0.05; // 50ms


void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    dragMode = DragMode::none;

    // ------------------------------------------------------------
// Comped-tab crossfade handle dragging (two white lines)
// ------------------------------------------------------------

    // Reset comped-handle drag state
    compDragMode = CompDragMode::None;
    activeBoundaryIndex = -1;
    activeSegmentIndex = -1;
    segmentSlipDragStartMouseSec = 0.0;
    segmentSlipDragStartOffsetSec = 0.0;

    if (viewMode == ViewMode::CompReview && !compResults.isEmpty())
    {
        const int rowCount = juce::jmax(1, juce::jmin(3, compResults.size()));
        for (int i = 0; i < rowCount; ++i)
        {
            juce::Rectangle<int> row, labelRect, waveRect, controlsRect;
            getCompRowLayout(i, rowCount, row, labelRect, waveRect, controlsRect);
            if (row.contains(event.getPosition()))
            {
                if (i != activeCompResultIndex)
                {
                    setActiveCompResult(i, true);
                }

                // Clicking a row should select it visually and functionally.
                for (int r = 0; r < compResults.size(); ++r)
                {
                    auto& rowState = compResults.getReference(r);
                    rowState.selected = (r == i);
                    if (r != i)
                        rowState.solo = false;
                }
                compedSelected = true;
                compedSolo = false;
                syncActiveCompResultFromLegacyState();
                refreshCompedButtons();
                repaint();
                break;
            }
        }
    }

    // ------------------------------------------------------------
    // Comped-tab SHIFT + segment body dragging (slip content)
    // ------------------------------------------------------------
    if (viewMode == ViewMode::CompReview && event.mods.isShiftDown())
    {
        juce::Rectangle<int> compWaveArea, topBarRect;
        if (getCompWaveAndTopBarForInteraction(compWaveArea, topBarRect)
            && topBarRect.contains(event.getPosition())
            && compSegments.size() > 0)
        {
            const int mx = event.getPosition().x;

            // Keep triangle boundary handles higher priority than Shift-slip.
            bool nearBoundaryHandle = false;
            for (int i = 1; i < compSegments.size(); ++i)
            {
                const int bx = compedTimeToX(compSegments.getReference(i).startSec, compWaveArea);
                if (std::abs(mx - bx) <= kSegHandleHitPx)
                {
                    nearBoundaryHandle = true;
                    break;
                }
            }

            if (!nearBoundaryHandle)
            {
                for (int i = 0; i < compSegments.size(); ++i)
                {
                    const auto& seg = compSegments.getReference(i);
                    const int x1 = compedTimeToX(seg.startSec, compWaveArea);
                    const int x2 = compedTimeToX(seg.endSec, compWaveArea);
                    const int left = juce::jmin(x1, x2);
                    const int right = juce::jmax(x1, x2);
                    const bool isLast = (i == compSegments.size() - 1);
                    const bool hit = isLast ? (mx >= left && mx <= right) : (mx >= left && mx < right);

                    if (hit)
                    {
                        activeSegmentIndex = i;
                        compDragMode = CompDragMode::SegmentSlip;
                        segmentSlipDragStartMouseSec = compedXToTime(mx, compWaveArea);
                        segmentSlipDragStartOffsetSec = seg.sourceOffsetSec;
                        segmentTimingTipShowUntilMs = 0.0;
                        restartPlaybackForCompEdit();
                        return;
                    }
                }
            }
        }
    }

    // CompReview: hit-test crossfade handles
    if (viewMode == ViewMode::CompReview)
    {
        juce::Rectangle<int> compWaveArea;
        if (getCompWaveAreaForInteraction(compWaveArea) && compWaveArea.contains(event.getPosition()))
        {
            int bestB = -1;
            CompDragMode bestMode = CompDragMode::None;
            int bestDist = 999999;

            const int mx = event.getPosition().x;

            for (int b = 0; b < compBoundaries.size(); ++b)
            {
                const auto& cb = compBoundaries.getReference(b);

                const int xs = compedTimeToX(cb.xfadeStartSec, compWaveArea);
                const int xe = compedTimeToX(cb.xfadeEndSec, compWaveArea);

                const int ds = std::abs(mx - xs);
                const int de = std::abs(mx - xe);

                if (ds <= kXFadeHitPx && ds < bestDist)
                {
                    bestDist = ds;
                    bestB = b;
                    bestMode = CompDragMode::XFadeStart;
                }
                if (de <= kXFadeHitPx && de < bestDist)
                {
                    bestDist = de;
                    bestB = b;
                    bestMode = CompDragMode::XFadeEnd;
                }
            }

            if (bestB >= 0)
            {
                activeBoundaryIndex = bestB;
                compDragMode = bestMode;
                
                restartPlaybackForCompEdit();
                
                return; // consume event
            }
        }
    }
    
    // ------------------------------------------------------------
    // Comped-tab SEGMENT boundary handle dragging (green triangles)
    // ------------------------------------------------------------
    if (viewMode == ViewMode::CompReview)
    {
        juce::Rectangle<int> compWaveArea, topBarRect;
        if (getCompWaveAndTopBarForInteraction(compWaveArea, topBarRect))
        {
            auto p = event.getPosition();
            auto handleZone = topBarRect.withTop(topBarRect.getY() - 12);

            if (handleZone.contains(p))
            {
                int bestB = -1;
                int bestDist = 999999;
                const int mx = p.x;

                for (int i = 1; i < compSegments.size(); ++i)
                {
                    const double boundaryS = compSegments.getReference(i).startSec;
                    const int x = compedTimeToX(boundaryS, compWaveArea);
                    const int d = std::abs(mx - x);

                    if (d <= kSegHandleHitPx && d < bestDist)
                    {
                        bestDist = d;
                        bestB = i - 1; // boundary index between seg i-1 and i
                    }
                }

                if (bestB >= 0)
                {
                    activeBoundaryIndex = bestB;
                    compDragMode = CompDragMode::SegmentBoundary;
                    
                    restartPlaybackForCompEdit();
                    
                    return; // consume event
                }
            }
        }
    }

    if (viewMode == ViewMode::CompReview)
    {
        compDragMode = CompDragMode::None;
        activeBoundaryIndex = -1;

        juce::Rectangle<int> compWaveArea;
        if (getCompWaveAreaForInteraction(compWaveArea)
            && compWaveArea.contains(event.getPosition())
            && compBoundaries.size() > 0
            && compSegments.size() >= 2)
        {
            const int mouseX = event.getPosition().getX();
            const int hitPx = 6;

            int bestB = -1;
            CompDragMode bestMode = CompDragMode::None;
            int bestDist = 999999;

            for (int b = 0; b < compBoundaries.size(); ++b)
            {
                const auto& cb = compBoundaries.getReference(b);

                // boundary b corresponds to between segment leftIdx and leftIdx+1
                int leftIdx = cb.leftSegIndex >= 0 ? cb.leftSegIndex : b;
                leftIdx = juce::jlimit(0, compSegments.size() - 2, leftIdx);

                if (leftIdx + 1 >= compSegments.size())
                    continue;

                const int xStart = compedTimeToX(cb.xfadeStartSec, compWaveArea);
                const int xEnd = compedTimeToX(cb.xfadeEndSec, compWaveArea);

                const int dStart = std::abs(mouseX - xStart);
                const int dEnd = std::abs(mouseX - xEnd);

                if (dStart <= hitPx && dStart < bestDist)
                {
                    bestDist = dStart;
                    bestB = b;
                    bestMode = CompDragMode::XFadeStart;
                }
                if (dEnd <= hitPx && dEnd < bestDist)
                {
                    bestDist = dEnd;
                    bestB = b;
                    bestMode = CompDragMode::XFadeEnd;
                }
            }

            if (bestB >= 0 && bestMode != CompDragMode::None)
            {
                activeBoundaryIndex = bestB;
                compDragMode = bestMode;
                repaint();
                return; // consume event so loop/grid dragging doesn't trigger
            }
        }
    }



    if (keyBounds.contains(event.getPosition()))
    {
        promptForKeySelection();
        return;
    }

    if (bpmBounds.contains(event.getPosition()))
    {
        dragMode = DragMode::bpmAdjust;
        bpmDragStartY = (int)event.position.y;
        bpmDragStartValue = bpm;
        return;
    }

    // CHECK FOR GRID ADJUSTMENT (Shift + Click on Waveform)
    if (event.mods.isShiftDown() && instrumentalWaveformBounds.contains(event.getPosition()))
    {
        dragMode = DragMode::gridAdjust;

        return;
    }

    if (!hasValidLoop() || loopLocked)
        return;

    

    if (!instrumentalWaveformBounds.contains(event.getPosition()))
        return;

    // Normal Loop Handle logic
    const int xStart = timeToX(loopStartSec);
    const int xEnd = timeToX(loopEndSec);
    const int mouseX = event.getPosition().getX();
    const int handleRadius = 12;

    if (std::abs(mouseX - xStart) <= handleRadius)
        dragMode = DragMode::leftHandle;
    else if (std::abs(mouseX - xEnd) <= handleRadius)
        dragMode = DragMode::rightHandle;
}


void MainComponent::mouseDrag(const juce::MouseEvent& event)
{
    // ------------------------------------------------------------
// Comped-tab crossfade handle dragging
// ------------------------------------------------------------
    
    // ------------------------------------------------------------
    // Segment boundary dragging (green handle)
    // ------------------------------------------------------------
    if (viewMode == ViewMode::CompReview
        && compDragMode == CompDragMode::SegmentSlip
        && activeSegmentIndex >= 0
        && activeSegmentIndex < compSegments.size())
    {
        juce::Rectangle<int> compWaveArea, topBarRect;
        if (getCompWaveAndTopBarForInteraction(compWaveArea, topBarRect))
        {
            auto& seg = compSegments.getReference(activeSegmentIndex);
            const double mouseSec = compedXToTime((int)event.position.x, compWaveArea);
            const double delta = mouseSec - segmentSlipDragStartMouseSec;
            double newOffset = segmentSlipDragStartOffsetSec + delta;

            const double totalLen = compedThumbnail.getTotalLength();
            const double compLen = (totalLen > 0.0)
                ? totalLen
                : (compSegments.isEmpty() ? 0.0 : compSegments.getLast().endSec);

            const double minOffset = -seg.startSec;
            const double maxOffset = compLen - seg.endSec;
            newOffset = juce::jlimit(minOffset, maxOffset, newOffset);

            if (std::abs(newOffset - seg.sourceOffsetSec) > 1.0e-9)
            {
                seg.sourceOffsetSec = newOffset;
                updateCompedAuditionSourceFromEdits();
                restartCompPlaybackIfPlaying();
            }

            repaint();
            return;
        }
    }

    if (viewMode == ViewMode::CompReview
        && compDragMode == CompDragMode::SegmentBoundary
        && activeBoundaryIndex >= 0
        && activeBoundaryIndex + 1 < compSegments.size())
    {
        juce::Rectangle<int> compWaveArea, topBarRect;
        if (getCompWaveAndTopBarForInteraction(compWaveArea, topBarRect))
        {
            const int b = activeBoundaryIndex;
            auto& leftSeg = compSegments.getReference(b);
            auto& rightSeg = compSegments.getReference(b + 1);

            const double oldBoundary = leftSeg.endSec;
            double newBoundary = compedXToTime((int)event.position.x, compWaveArea);

            const double minBound = leftSeg.startSec + kMinSegDurationSec;
            const double maxBound = rightSeg.endSec - kMinSegDurationSec;

            newBoundary = juce::jlimit(minBound, maxBound, newBoundary);

            const double delta = newBoundary - oldBoundary;

            if (std::abs(delta) > 1.0e-9)
            {
                leftSeg.endSec = newBoundary;
                rightSeg.startSec = newBoundary;

                if (b < compBoundaries.size())
                {
                    auto& cb = compBoundaries.getReference(b);
                    cb.xfadeStartSec += delta;
                    cb.xfadeEndSec += delta;
                    clampBoundaryWindowToSegments(b);
                }

                if (b - 1 >= 0)
                    clampBoundaryWindowToSegments(b - 1);
                if (b + 1 < compBoundaries.size())
                    clampBoundaryWindowToSegments(b + 1);
                
                sanitizeCompSegments();

                updateCompedAuditionSourceFromEdits();
                restartCompPlaybackIfPlaying();
            }

            repaint();
            return;
        }
    }

    // CompReview: live crossfade handle dragging (Option 2 audition)
    if (viewMode == ViewMode::CompReview
        && compDragMode != CompDragMode::None
        && activeBoundaryIndex >= 0
        && activeBoundaryIndex < compBoundaries.size()
        && compSegments.size() >= 2)
    {
        juce::Rectangle<int> compWaveArea;
        if (getCompWaveAreaForInteraction(compWaveArea))
        {
            const int b = activeBoundaryIndex;

            auto& cb = compBoundaries.getReference(b);
            const auto& left = compSegments.getReference(b);
            const auto& right = compSegments.getReference(b + 1);

            const double boundaryS = left.endSec;

            double t = compedXToTime((int)event.position.x, compWaveArea);

            if (compDragMode == CompDragMode::XFadeStart)
            {
                // Start is constrained to [left.start, boundary]
                t = juce::jlimit(left.startSec, boundaryS, t);
                cb.xfadeStartSec = t;

                // Enforce minimum window
                if (cb.xfadeEndSec - cb.xfadeStartSec < kMinXFadeWinSec)
                    cb.xfadeStartSec = juce::jlimit(left.startSec, boundaryS, cb.xfadeEndSec - kMinXFadeWinSec);
            }
            else if (compDragMode == CompDragMode::XFadeEnd)
            {
                // End is constrained to [boundary, right.end]
                t = juce::jlimit(boundaryS, right.endSec, t);
                cb.xfadeEndSec = t;

                // Enforce minimum window
                if (cb.xfadeEndSec - cb.xfadeStartSec < kMinXFadeWinSec)
                    cb.xfadeEndSec = juce::jlimit(boundaryS, right.endSec, cb.xfadeStartSec + kMinXFadeWinSec);
            }

            // Live update audition source so sound changes immediately
            if (compedAuditionSource != nullptr)
                compedAuditionSource->setBoundaryWindow(b, cb.xfadeStartSec, cb.xfadeEndSec);
            
            restartCompPlaybackIfPlaying();

            repaint();
            return;
        }
    }


    if (viewMode == ViewMode::CompReview
        && compDragMode != CompDragMode::None
        && activeBoundaryIndex >= 0
        && activeBoundaryIndex < compBoundaries.size()
        && compSegments.size() >= 2)
    {
        juce::Rectangle<int> compWaveArea;
        if (!getCompWaveAreaForInteraction(compWaveArea))
            return;

        const int b = activeBoundaryIndex;
        auto& cb = compBoundaries.getReference(b);

        int leftIdx = cb.leftSegIndex >= 0 ? cb.leftSegIndex : b;
        leftIdx = juce::jlimit(0, compSegments.size() - 2, leftIdx);

        const auto& leftSeg = compSegments.getReference(leftIdx);
        const auto& rightSeg = compSegments.getReference(leftIdx + 1);
        const double boundaryS = leftSeg.endSec;

        double t = compedXToTime((int)event.position.x, compWaveArea);

        constexpr double minWin = 0.005; // 5 ms minimum window

        if (compDragMode == CompDragMode::XFadeStart)
        {
            cb.xfadeStartSec = juce::jlimit(leftSeg.startSec, boundaryS, t);

            // enforce minimum width
            if (cb.xfadeEndSec - cb.xfadeStartSec < minWin)
                cb.xfadeEndSec = cb.xfadeStartSec + minWin;

            cb.xfadeEndSec = juce::jlimit(boundaryS, rightSeg.endSec, cb.xfadeEndSec);
        }
        else if (compDragMode == CompDragMode::XFadeEnd)
        {
            cb.xfadeEndSec = juce::jlimit(boundaryS, rightSeg.endSec, t);

            if (cb.xfadeEndSec - cb.xfadeStartSec < minWin)
                cb.xfadeStartSec = cb.xfadeEndSec - minWin;

            cb.xfadeStartSec = juce::jlimit(leftSeg.startSec, boundaryS, cb.xfadeStartSec);
        }

        repaint();
        return; // consume event
    }

    if (dragMode == DragMode::bpmAdjust)
    {
        const int dy = (int)(event.position.y - bpmDragStartY);
        const int step = -dy / 3;
        int newBpm = juce::jlimit(40, 240, bpmDragStartValue + step);

        if (newBpm != bpm)
        {
            bpm = newBpm;
            bpmSet = true;
            refreshBpmLabel();
        }
        return;
    }

    if (dragMode == DragMode::gridAdjust)
    {
        if (bpm <= 0) return;

        const double totalLength = thumbnail.getTotalLength();
        const int w = instrumentalWaveformBounds.getWidth();
        if (totalLength <= 0.0 || w <= 0) return;

        // Track X in pixels
        const double mouseX = (double)event.position.x;
        if (!gridDragHasLast)
        {
            gridDragLastMouseTime = mouseX;   // re-use this variable as "lastX"
            gridDragHasLast = true;
            return;
        }

        const double dx = mouseX - gridDragLastMouseTime;
        gridDragLastMouseTime = mouseX;

        const double secondsPerPixel = totalLength / (double)w;

        // Slower factor (tweak 0.05 .. 0.25 to taste)
        const double speed = 0.12;
        gridOffsetSec -= dx * secondsPerPixel * speed;

        // Wrap to your grid spacing period (2 beats)
        const double secondsPerBeat = 60.0 / bpm;
        const double gridPeriodSec = secondsPerBeat * 2.0;

        gridOffsetSec = std::fmod(gridOffsetSec, gridPeriodSec);
        if (gridOffsetSec < 0.0)
            gridOffsetSec += gridPeriodSec;

        repaint();
        return;
    }


    if (!hasValidLoop() || dragMode == DragMode::none || loopLocked)
        return;

    const double totalLength = thumbnail.getTotalLength();
    if (totalLength <= 0.0) return;

    // Calculate raw mouse time
    double mouseTime = xToTime((float)event.position.x);

    // APPLY QUANTIZATION if BPM is set (Magnet effect)
    if (bpmSet)
    {
        double snapped = snapToGrid(mouseTime);
        // Only snap if we are close (e.g. within 15 pixels)
        float distPx = std::abs(timeToX(snapped) - timeToX(mouseTime));
        if (distPx < 15.0f)
            mouseTime = snapped;
    }

    if (dragMode == DragMode::leftHandle)
    {
        const double maxStart = juce::jmax(0.0, loopEndSec - minLoopLengthSec);
        loopStartSec = juce::jlimit(0.0, maxStart, mouseTime);

        if (readerSource.get() != nullptr)
        {
            transportSource.setPosition(loopStartSec);
            transportSource.start();
        }
    }
    else if (dragMode == DragMode::rightHandle)
    {
        const double minEnd = juce::jmin(totalLength, loopStartSec + minLoopLengthSec);
        double newEnd = juce::jlimit(minEnd, totalLength, mouseTime);

        if (transportSource.isPlaying())
        {
            const double current = getPlayheadPositionSec();
            if (newEnd < current)
                getPlayheadPositionSec();
        }
        loopEndSec = newEnd;
    }

    repaint();
}



double MainComponent::snapToGrid(double timeIn)
{
    if (!bpmSet || bpm <= 0) return timeIn;

    const double secondsPerBeat = 60.0 / bpm;
    // Calculate how many beats we are from the offset
    double beatIndex = std::round((timeIn - gridOffsetSec) / secondsPerBeat);
    return gridOffsetSec + (beatIndex * secondsPerBeat);
}


void MainComponent::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    // Finish comped-handle drag: persist JSON, no Python rebuild (Option 2)
    if (viewMode == ViewMode::CompReview && compDragMode != CompDragMode::None)
    {
        if (compmapJsonCache.isObject() && lastCompmapFile.existsAsFile())
        {
            if (auto* rootObj = compmapJsonCache.getDynamicObject())
            {
                auto boundariesVar = rootObj->getProperty("boundaries");
                juce::Array<juce::var>* boundariesArr = boundariesVar.getArray();

                if (boundariesArr == nullptr)
                {
                    // create boundaries array if missing
                    boundariesVar = juce::var(juce::Array<juce::var>());
                    rootObj->setProperty("boundaries", boundariesVar);
                    boundariesArr = rootObj->getProperty("boundaries").getArray();
                }

                if (boundariesArr != nullptr)
                {
                    // ensure size
                    while (boundariesArr->size() < compBoundaries.size())
                        boundariesArr->add(juce::var(new juce::DynamicObject()));

                    // update only xfadeStartSec/xfadeEndSec, preserve other fields
                    for (int b = 0; b < compBoundaries.size(); ++b)
                    {
                        auto elem = boundariesArr->getReference(b);
                        juce::DynamicObject* obj = elem.getDynamicObject();
                        if (obj == nullptr)
                        {
                            elem = juce::var(new juce::DynamicObject());
                            boundariesArr->set(b, elem);
                            obj = elem.getDynamicObject();
                        }

                        const auto& cb = compBoundaries.getReference(b);
                        obj->setProperty("xfadeStartSec", cb.xfadeStartSec);
                        obj->setProperty("xfadeEndSec", cb.xfadeEndSec);
                    }
                    
                    // --- update segments in JSON ---
                    auto segVar = rootObj->getProperty("segments");
                    juce::Array<juce::var>* segArr = segVar.getArray();
                    if (segArr != nullptr)
                    {
                        for (int i = 0; i < compSegments.size() && i < segArr->size(); ++i)
                        {
                            auto elem = segArr->getReference(i);
                            juce::DynamicObject* obj = elem.getDynamicObject();
                            if (obj == nullptr)
                            {
                                elem = juce::var(new juce::DynamicObject());
                                segArr->set(i, elem);
                                obj = elem.getDynamicObject();
                            }

                            obj->setProperty("start_s", compSegments.getReference(i).startSec);
                            obj->setProperty("end_s", compSegments.getReference(i).endSec);
                            obj->setProperty("source_offset_s", compSegments.getReference(i).sourceOffsetSec);
                        }
                    }

                    const auto jsonText = juce::JSON::toString(compmapJsonCache, true);
                    lastCompmapFile.replaceWithText(jsonText);
                }
            }
        }

        compDragMode = CompDragMode::None;
        activeBoundaryIndex = -1;
        activeSegmentIndex = -1;
        repaint();
        return;
    }

    // existing loop/grid mouse-up behavior
    dragMode = DragMode::none;
    gridDragHasLast = false;
}



void MainComponent::mouseMove(const juce::MouseEvent& event)
{
    if (viewMode == ViewMode::CompReview)
    {
        const bool inZoomHintArea =
            instrumentalWaveformBounds.contains(event.getPosition()) || takesAreaBounds.contains(event.getPosition());
        if (inZoomHintArea && !zoomScaleTipShownOnce)
        {
            zoomScaleTipShownOnce = true;
            zoomScaleTipShowUntilMs = juce::Time::getMillisecondCounterHiRes() + 2800.0;
            repaint();
        }

        juce::Rectangle<int> compWaveArea, topBarRect;
        
        if (getCompWaveAndTopBarForInteraction(compWaveArea, topBarRect))
        {
            auto p = event.getPosition();
            auto handleZone = topBarRect.withTop(topBarRect.getY() - 12);
            
            if (handleZone.contains(p))
            {
                int mx = p.x;
                for (int i = 1; i < compSegments.size(); ++i)
                {
                    const double boundaryS = compSegments.getReference(i).startSec;
                    const int x = compedTimeToX(boundaryS, compWaveArea);
                    
                    if (std::abs(mx - x) <= kSegHandleHitPx)
                    {
                        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                        return;
                    }
                }
            }

            if (event.mods.isShiftDown() && topBarRect.contains(p))
            {
                const int mx = p.x;
                bool nearBoundary = false;
                for (int i = 1; i < compSegments.size(); ++i)
                {
                    const int bx = compedTimeToX(compSegments.getReference(i).startSec, compWaveArea);
                    if (std::abs(mx - bx) <= kSegHandleHitPx)
                    {
                        nearBoundary = true;
                        break;
                    }
                }

                if (!nearBoundary)
                {
                    for (int i = 0; i < compSegments.size(); ++i)
                    {
                        const auto& seg = compSegments.getReference(i);
                        const int x1 = compedTimeToX(seg.startSec, compWaveArea);
                        const int x2 = compedTimeToX(seg.endSec, compWaveArea);
                        const int left = juce::jmin(x1, x2);
                        const int right = juce::jmax(x1, x2);
                        const bool isLast = (i == compSegments.size() - 1);
                        const bool hit = isLast ? (mx >= left && mx <= right) : (mx >= left && mx < right);
                        if (hit)
                        {
                            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
                            return;
                        }
                    }
                }
            }
        }
        if (getCompWaveAreaForInteraction(compWaveArea) && compWaveArea.contains(event.getPosition()))
        {
            const int mx = event.getPosition().x;

            for (int b = 0; b < compBoundaries.size(); ++b)
            {
                const auto& cb = compBoundaries.getReference(b);
                const int xs = compedTimeToX(cb.xfadeStartSec, compWaveArea);
                const int xe = compedTimeToX(cb.xfadeEndSec, compWaveArea);

                if (std::abs(mx - xs) <= kXFadeHitPx || std::abs(mx - xe) <= kXFadeHitPx)
                {
                    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                    return;
                }
            }
        }

        setMouseCursor(juce::MouseCursor::NormalCursor);
        // don't return; you might still want BPM cursor behavior etc if visible
    }


    // 1. BPM Dragging Cursor
    if (bpmBounds.contains(event.getPosition()))
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        isHoveringInstrumental = false; // clear this just in case
        return;
    }

    if (keyBounds.contains(event.getPosition()))
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        isHoveringInstrumental = false;
        return;
    }

    // 2. Check if we are inside the instrumental waveform
    bool insideInstrumental = instrumentalWaveformBounds.contains(event.getPosition());

    // Update state for Tooltip
    if (insideInstrumental != isHoveringInstrumental)
    {
        isHoveringInstrumental = insideInstrumental;
        repaint();
    }

    if (isHoveringInstrumental)
    {
        lastMousePosition = event.getPosition();
        repaint(); // continuous repaint to follow mouse
    }

    // 3. Logic for Loop Handles vs Normal Cursor
    if (!hasValidLoop() || !insideInstrumental)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    const int xStart = timeToX(loopStartSec);
    const int xEnd = timeToX(loopEndSec);
    const int mouseX = (int)event.position.x;
    const int handleRadius = 12;

    if (std::abs(mouseX - xStart) <= handleRadius || std::abs(mouseX - xEnd) <= handleRadius)
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    else
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void MainComponent::mouseWheelMove(const juce::MouseEvent& event,
                                   const juce::MouseWheelDetails& wheel)
{
    if (viewMode != ViewMode::CompReview || !event.mods.isCtrlDown())
    {
        juce::Component::mouseWheelMove(event, wheel);
        return;
    }

    const double totalLength = thumbnail.getTotalLength();
    if (totalLength <= 0.0 || instrumentalWaveformBounds.getWidth() <= 0)
        return;

    if (visibleEndSec <= visibleStartSec + 0.0001)
    {
        visibleStartSec = 0.0;
        visibleEndSec = totalLength;
    }

    const double oldStart = visibleStartSec;
    const double oldEnd = visibleEndSec;
    const double oldSpan = juce::jmax(0.0001, oldEnd - oldStart);
    const double minSpan = juce::jmin(0.5, totalLength);

    const double wheelSteps = (wheel.deltaY == 0.0f) ? 0.0 : (double)wheel.deltaY;
    const double zoomBase = 1.15;
    const double zoomMultiplier = std::pow(zoomBase, std::abs(wheelSteps) * 6.0);
    const double newSpan = (wheelSteps > 0.0)
        ? juce::jmax(minSpan, oldSpan / zoomMultiplier)
        : juce::jmin(totalLength, oldSpan * zoomMultiplier);

    const double anchorTime = xToTime((float)event.getPosition().x);
    const double anchorNorm = juce::jlimit(0.0, 1.0, (anchorTime - oldStart) / oldSpan);

    double newStart = anchorTime - anchorNorm * newSpan;
    double newEnd = newStart + newSpan;

    if (newStart < 0.0)
    {
        newEnd -= newStart;
        newStart = 0.0;
    }
    if (newEnd > totalLength)
    {
        const double overflow = newEnd - totalLength;
        newStart -= overflow;
        newEnd = totalLength;
    }

    visibleStartSec = juce::jlimit(0.0, totalLength, newStart);
    visibleEndSec = juce::jlimit(visibleStartSec + 0.0001, totalLength, newEnd);
    repaint();
}


void MainComponent::refreshCompedButtons()
{
    for (int i = 0; i < compedSelectButtons.size(); ++i)
    {
        const bool hasResult = (i < compResults.size());
        bool selected = false;
        bool solo = false;

        if (hasResult)
        {
            selected = compResults.getReference(i).selected;
            solo = compResults.getReference(i).solo;
        }

        if (auto* selectBtn = compedSelectButtons[i])
        {
            selectBtn->setVisible(viewMode == ViewMode::CompReview && hasResult);
            selectBtn->setEnabled(hasResult);
            selectBtn->setToggleState(selected, juce::dontSendNotification);
            selectBtn->setButtonText(selected ? "Selected" : "Select");
        }

        if (auto* soloBtn = compedSoloButtons[i])
        {
            soloBtn->setVisible(viewMode == ViewMode::CompReview && hasResult);
            soloBtn->setEnabled(hasResult);
            soloBtn->setToggleState(solo, juce::dontSendNotification);
            soloBtn->setButtonText(solo ? "Soloed" : "Solo");
        }
    }
}


