// MainComponent_Interaction.cpp
#include "MainComponent.h"

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

            transportSource.setPosition(loopStartSec);
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
                    takeTransport.setPosition(0.0);
                    takeTransport.start();
                }
            }
        }
        else if (viewMode == ViewMode::CompReview)
        {
            if (takeReaderSource != nullptr && (compedSelected || compedSolo))
            {
                takeTransport.setPosition(0.0);
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

        auto* content = new CompingProgressComponent(neonLookAndFeel);
        compingProgressComponent = content;

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(content);
        opts.dialogTitle = "AI Comping";
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
    else if (button == &compedSelectButton)
    {
        const bool haveInstrumental = (readerSource.get() != nullptr);
        const bool canPlayComped = (!isRecording && takeReaderSource != nullptr);

        if (compedSelected)
            compedSelected = false;
        else
        {
            compedSelected = true;
            compedSolo = false;
        }

        if (canPlayComped)
        {
            if (compedSelected)
            {
                if (haveInstrumental && hasValidLoop())
                {
                    transportSource.setPosition(loopStartSec);
                    transportSource.start();
                }
                else
                {
                    // no instrumental -> just comped
                    transportSource.stop();
                }

                takeTransport.setPosition(0.0);
                takeTransport.start();
            }
            else
            {
                // deselected -> stop comped playback
                takeTransport.stop();
            }
        }

        refreshCompedButtons();
        repaint();
    }
    else if (button == &compedSoloButton)
    {
        const bool haveInstrumental = (readerSource.get() != nullptr);
        const bool canPlayComped = (!isRecording && takeReaderSource != nullptr);
        juce::ignoreUnused(haveInstrumental);

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
            {
                // Solo -> stop instrumental, only comped
                transportSource.stop();
                takeTransport.setPosition(0.0);
                takeTransport.start();
            }
            else
            {
                // Unsolo -> stop comped; user can hit PLAY if they want both
                takeTransport.stop();
            }
        }

        refreshCompedButtons();
        repaint();
    }

    else if (button == &recordingTabButton)
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

        transportSource.stop();             
        takeTransport.stop();

        viewMode = ViewMode::CompReview;
        updateTabButtonStyles();
        refreshCompedButtons();
        resized();
        repaint();
    }
    else if (button == &exportCompedButton)
    {
        if (viewMode != ViewMode::CompReview)
            return;

        if (!hasLastCompResult || !lastCompedFile.existsAsFile())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Nothing to export",
                "There is no comped file to export yet.\n"
                "Run COMPING first.");
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

                if (target.existsAsFile())
                    target.deleteFile();

                if (lastCompedFile.copyFileTo(target))
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::InfoIcon,
                        "Export successful",
                        "Comped file exported to:\n" + target.getFullPathName());
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        "Export failed",
                        "Could not write to:\n" + target.getFullPathName());
                }
            });

        return;
    }
    else if (button == &metronomeToggle)
    {
        metronomeOn = metronomeToggle.getToggleState();
    }
}

//==============================================================================

void MainComponent::timerCallback()
{
    syncTakeLanesWithTakeTracks();
    if (transportSource.isPlaying() && hasValidLoop())
    {
        const double pos = transportSource.getCurrentPosition();

        if (pos >= loopEndSec)
        {
            transportSource.setPosition(loopStartSec);

            bool shouldRestartTake = false;

            if (viewMode == ViewMode::Recording)
            {
                shouldRestartTake =
                    (selectedTakeIndex >= 0 || soloTakeIndex >= 0)
                    && takeReaderSource != nullptr;
            }
            else if (viewMode == ViewMode::CompReview)
            {
                shouldRestartTake =
                    (compedSelected || compedSolo)
                    && takeReaderSource != nullptr;
            }

            if (shouldRestartTake)
            {
                takeTransport.setPosition(0.0);
                takeTransport.start();
            }
        }

    }

    if (viewMode == ViewMode::Recording)
    {
        // Keep number of lanes in sync with the take list
        syncTakeLanesWithTakeTracks();

        // Compute a global time in seconds for the playhead
        double globalTime = transportSource.getCurrentPosition();

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
        repaint();
    }
    else if (source == &compedThumbnail)
    {
        if (compedThumbnail.getTotalLength() > 0.0)
            hasCompedThumbnail = true;

        repaint();
    }
}

//==============================================================================
// Mouse handling
//==============================================================================

void MainComponent::mouseDown(const juce::MouseEvent& event)
{

    // BPM & loop handles
    dragMode = DragMode::none;

    if (bpmBounds.contains(event.getPosition()))
    {
        dragMode = DragMode::bpmAdjust;
        bpmDragStartY = event.position.y;
        bpmDragStartValue = bpm;
        return;
    }

    if (!hasValidLoop() || loopLocked)
        return;

    if (!instrumentalWaveformBounds.contains(event.getPosition()))
        return;

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

    if (!hasValidLoop() || dragMode == DragMode::none || loopLocked)
        return;

    const double totalLength = thumbnail.getTotalLength();
    if (totalLength <= 0.0)
        return;

    const double mouseTime = xToTime((float)event.position.x);

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
            const double current = transportSource.getCurrentPosition();
            if (newEnd < current)
                transportSource.setPosition(loopStartSec);
        }

        loopEndSec = newEnd;
    }

    repaint();
}

void MainComponent::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    dragMode = DragMode::none;
}

void MainComponent::mouseMove(const juce::MouseEvent& event)
{
    if (bpmBounds.contains(event.getPosition()))
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    if (!hasValidLoop()
        || !instrumentalWaveformBounds.contains(event.getPosition()))
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    const int xStart = timeToX(loopStartSec);
    const int xEnd = timeToX(loopEndSec);
    const int mouseX = (int)event.position.x;
    const int handleRadius = 12;

    if (std::abs(mouseX - xStart) <= handleRadius
        || std::abs(mouseX - xEnd) <= handleRadius)
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    else
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void MainComponent::refreshCompedButtons()
{
    compedSelectButton.setToggleState(compedSelected, juce::dontSendNotification);
    compedSoloButton.setToggleState(compedSolo, juce::dontSendNotification);

    compedSelectButton.setButtonText(compedSelected ? "Selected" : "Select");
    compedSoloButton.setButtonText(compedSolo ? "Soloed" : "Solo");
}

