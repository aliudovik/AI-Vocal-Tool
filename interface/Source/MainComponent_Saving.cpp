// MainComponent_ProjectState.cpp
#include "MainComponent.h"

//==============================================================================
// Project state mapping
//==============================================================================

ProjectState MainComponent::createProjectState() const
{
    ProjectState s;

    s.instrumentalPath = currentInstrumentalFile.getFullPathName();
    s.loopStartSec = loopStartSec;
    s.loopEndSec = loopEndSec;
    s.loopLocked = loopLocked;
    s.cachedLoopLengthSec = cachedLoopLengthSec;

    s.bpm = bpm;
    s.bpmSet = bpmSet;
    s.metronomeOn = metronomeOn;

    s.currentPhraseIndex = currentPhraseIndex;
    s.currentPhraseDirectory = currentPhraseDirectory.getFullPathName();

    s.fullRecordingIndex = fullRecordingIndex;
    s.nextTakeIndex = nextTakeIndex;

    s.selectedTakeIndex = selectedTakeIndex;
    s.soloTakeIndex = soloTakeIndex;
    s.takeVolume = takeVolumeSlider.getValue();

    s.hasLastCompResult = hasLastCompResult;
    s.lastCompedFilePath = lastCompedFile.getFullPathName();
    s.lastCompmapFilePath = lastCompmapFile.getFullPathName();
    s.lastCompAlphaPct = lastCompAlphaPct;
    s.lastCompCrossfadePct = lastCompCrossfadePct;
    s.lastCompFadeFraction = lastCompFadeFraction;

    s.compedSelected = compedSelected;
    s.compedSolo = compedSolo;

    s.viewIsCompReview = (viewMode == ViewMode::CompReview);

    for (int i = 0; i < compSegments.size(); ++i)
    {
        const auto& seg = compSegments.getReference(i);
        CompSegmentState cs;
        cs.startSec = seg.startSec;
        cs.endSec = seg.endSec;
        cs.takeIndex = seg.takeIndex;
        s.compSegments.add(cs);
    }

    return s;
}

void MainComponent::resetProjectState()
{
    transportSource.stop();
    transportSource.setSource(nullptr);
    readerSource.reset();

    thumbnail.clear();

    loopStartSec = 0.0;
    loopEndSec = 0.0;

    bpm = 120;
    bpmSet = false;
    metronomeOn = false;

    refreshBpmLabel();
    metronomeToggle.setToggleState(false, juce::dontSendNotification);
    metronomeToggle.setEnabled(false);

    playButton.setEnabled(false);
    stopButton.setEnabled(false);

    {
        const juce::ScopedLock sl(writerLock);
        recordingWriter.reset();
    }

    isRecording = false;
    loopLocked = false;
    fullRecordingIndex = 0;
    nextTakeIndex = 1;
    cachedLoopLengthSec = 0.0;
    recordButton.setButtonText("Record");
    recordButton.setEnabled(false);

    selectedTakeIndex = -1;
    soloTakeIndex = -1;
    takeTransport.stop();
    takeTransport.setSource(nullptr);
    takeReaderSource.reset();

    {
        const juce::ScopedLock sl(vocalLock);
        vocalWaveBuffer.setSize(0, 0);
        totalRecordedSamples = 0;
        loopLengthSamples = 0;
        takeTracks.clear();
        vocalBufferCapacitySamples = 0;
    }

    currentInstrumentalFile = juce::File();

    hasLastCompResult = false;
    hasCompedThumbnail = false;
    compedThumbnail.clear();
    compSegments.clear();
    lastCompAlphaPct = 0;
    lastCompCrossfadePct = 0;
    lastCompFadeFraction = 0.0;
    compedSelected = true;
    compedSolo = false;
    compedTabButton.setEnabled(false);
    updateTabButtonStyles();

    repaint();
}



void MainComponent::applyProjectState(const ProjectState& s)
{
    if (isRecording)
        stopRecording();

    transportSource.stop();
    takeTransport.stop();

    {
        const juce::ScopedLock sl(writerLock);
        recordingWriter.reset();
    }

    transportSource.setSource(nullptr);

    readerSource.reset();
    thumbnail.clear();


    {
        const juce::ScopedLock sl(vocalLock);
        vocalWaveBuffer.setSize(0, 0);
        takeTracks.clear();
        totalRecordedSamples = 0;
        loopLengthSamples = 0;
        vocalBufferCapacitySamples = 0;
    }

    selectedTakeIndex = -1;
    soloTakeIndex = -1;

    takeTransport.setSource(nullptr);
    takeReaderSource.reset();

    hasCompedThumbnail = false;
    compedThumbnail.clear();
    compSegments.clear();

    hasLastCompResult = false;
    lastCompedFile = juce::File();
    lastCompmapFile = juce::File();
    lastCompAlphaPct = 0;
    lastCompCrossfadePct = 0;
    lastCompFadeFraction = 0.0;
    compedSelected = true;
    compedSolo = false;

    currentPhraseDirectory = juce::File(s.currentPhraseDirectory);
    currentPhraseIndex = s.currentPhraseIndex;

    bpm = s.bpm;
    bpmSet = s.bpmSet;
    metronomeOn = s.metronomeOn;
    metronomeToggle.setToggleState(metronomeOn, juce::dontSendNotification);
    refreshBpmLabel();

    loopStartSec = s.loopStartSec;
    loopEndSec = s.loopEndSec;
    loopLocked = s.loopLocked;
    cachedLoopLengthSec = s.cachedLoopLengthSec;

    fullRecordingIndex = s.fullRecordingIndex;
    nextTakeIndex = s.nextTakeIndex;

    currentInstrumentalFile = juce::File(s.instrumentalPath);
    if (currentInstrumentalFile.existsAsFile())
    {
        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager.createReaderFor(currentInstrumentalFile));

        if (reader != nullptr)
        {
            auto newSource =
                std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);

            const double sr = newSource->getAudioFormatReader()->sampleRate;
            const double totalLengthSec =
                (double)newSource->getAudioFormatReader()->lengthInSamples / sr;

            transportSource.setSource(newSource.get(),
                0,
                nullptr,
                sr);
            transportSource.setLooping(false);

            readerSource = std::move(newSource);

            thumbnail.setSource(new juce::FileInputSource(currentInstrumentalFile));
            minLoopLengthSec = juce::jmin(5.0, totalLengthSec);

            loopStartSec = juce::jlimit(0.0, totalLengthSec, loopStartSec);
            if (loopEndSec <= 0.0)
                loopEndSec = totalLengthSec;

            loopEndSec = juce::jlimit(loopStartSec + minLoopLengthSec,
                totalLengthSec,
                loopEndSec);
        }
    }

    rebuildTakesFromPhraseDirectory();

    if (s.selectedTakeIndex >= 0 && s.selectedTakeIndex < takeTracks.size())
        selectedTakeIndex = s.selectedTakeIndex;

    if (s.soloTakeIndex >= 0 && s.soloTakeIndex < takeTracks.size())
        soloTakeIndex = s.soloTakeIndex;

    double vol = s.takeVolume;
    vol = juce::jlimit(0.0, 1.5, vol);
    takeVolumeSlider.setValue(vol, juce::dontSendNotification);
    takeTransport.setGain((float)vol);

    hasLastCompResult = s.hasLastCompResult;
    lastCompedFile = juce::File(s.lastCompedFilePath);
    lastCompmapFile = juce::File(s.lastCompmapFilePath);
    lastCompAlphaPct = s.lastCompAlphaPct;
    lastCompCrossfadePct = s.lastCompCrossfadePct;
    lastCompFadeFraction = s.lastCompFadeFraction;
    compedSelected = s.compedSelected;
    compedSolo = s.compedSolo;

    compSegments.clear();
    for (int i = 0; i < s.compSegments.size(); ++i)
    {
        const auto& cs = s.compSegments.getReference(i);
        CompSegment seg;
        seg.startSec = cs.startSec;
        seg.endSec = cs.endSec;
        seg.takeIndex = cs.takeIndex;
        compSegments.add(seg);
    }

    if (hasLastCompResult && lastCompedFile.existsAsFile())
    {
        loadCompedFile(lastCompedFile);

        if (!loadLastCompForReview() && !s.compSegments.isEmpty())
        {
            compSegments.clear();
            for (int i = 0; i < s.compSegments.size(); ++i)
            {
                const auto& cs = s.compSegments.getReference(i);
                CompSegment seg;
                seg.startSec = cs.startSec;
                seg.endSec = cs.endSec;
                seg.takeIndex = cs.takeIndex;
                compSegments.add(seg);
            }
        }

        hasLastCompResult = true;
        compedTabButton.setEnabled(true);
    }
    else
    {
        hasLastCompResult = false;
        compedTabButton.setEnabled(false);
    }

    viewMode = (s.viewIsCompReview && hasLastCompResult)
        ? ViewMode::CompReview
        : ViewMode::Recording;

    updateTabButtonStyles();

    const bool haveInstrumental = (readerSource.get() != nullptr);

    playButton.setEnabled(haveInstrumental || hasLastCompResult || takeTracks.size() > 0);
    stopButton.setEnabled(haveInstrumental || hasLastCompResult);
    metronomeToggle.setEnabled(haveInstrumental);
    recordButton.setEnabled(haveInstrumental && hasValidLoop());

    resized();
    repaint();
}

//==============================================================================
// Project save/load dialogs
//==============================================================================

void MainComponent::saveProjectToFile()
{
    ProjectState state = createProjectState();

    juce::File defaultFile = currentPhraseDirectory
        .getChildFile("project_phrase"
            + juce::String(currentPhraseIndex).paddedLeft('0', 2)
            + ".json");

    fileChooser = std::make_unique<juce::FileChooser>(
        "Save project as...",
        defaultFile,
        "*.json");

    auto flags = juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags,
        [this, state](const juce::FileChooser& fc) mutable
        {
            juce::File target = fc.getResult();
            fileChooser.reset();

            if (!target.getFullPathName().isNotEmpty())
                return;

            if (target.getFileExtension().isEmpty())
                target = target.withFileExtension(".json");

            juce::String error;
            if (!ProjectState::saveToFile(state, target, error))
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Save project failed",
                    "Could not save project:\n" + error);
                return;
            }

            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Project saved",
                "Project saved to:\n" + target.getFullPathName());
        });
}

void MainComponent::launchProjectLoadChooser()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load project...",
        currentPhraseDirectory,
        "*.json");

    auto flags = juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags,
        [this](const juce::FileChooser& fc)
        {
            juce::File file = fc.getResult();
            fileChooser.reset();

            if (!file.getFullPathName().isNotEmpty())
                return;

            ProjectState state;
            juce::String error;

            if (!ProjectState::loadFromFile(state, file, error))
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Load project failed",
                    "Could not load project:\n" + error);
                return;
            }

            applyProjectState(state);

            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Project loaded",
                "Project loaded from:\n" + file.getFullPathName());
        });
}



void MainComponent::loadProjectFromFile()
{
    // Decide whether we should warn the user
    const bool hasExistingProjectData =
        (readerSource != nullptr)
        || (takeTracks.size() > 0)
        || hasLastCompResult
        || isRecording
        || currentInstrumentalFile.existsAsFile();

    if (!hasExistingProjectData)
    {
        // Nothing to lose: just open chooser
        launchProjectLoadChooser();
        return;
    }

    auto* w = new juce::AlertWindow("Load another project?",
        {},
        juce::AlertWindow::WarningIcon);

    w->addTextBlock("You will lose any unsaved data from this project if you load another one!\n\nContinue?");
    w->addButton("Yes", 1);
    w->addButton("No", 0);

    w->centreAroundComponent(this, 420, 220);

    w->enterModalState(true,
        juce::ModalCallbackFunction::create([this, w](int result)
            {
                if (result == 1) // Yes
                {
                    // 1) Reset current project
                    resetProjectState();

                    // 2) Open the file chooser *after* the alert has gone away
                    juce::MessageManager::callAsync([this]
                        {
                            launchProjectLoadChooser();
                        });
                }
                // If "No", do nothing.
            }),
        true);
}



//==============================================================================
// BPM helpers
//==============================================================================

void MainComponent::promptForBpm()
{
    auto* w = new juce::AlertWindow("Set BPM",
        {},
        juce::AlertWindow::NoIcon);

    w->addTextBlock("BPM is essential for successful Vocal Comping.");
    w->addTextEditor("bpm", juce::String(bpm), "BPM:");

    if (auto* editor = w->getTextEditor("bpm"))
        editor->setJustification(juce::Justification::centred);

    w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));

    w->setSize(420, 220);
    w->centreAroundComponent(this, w->getWidth(), w->getHeight());

    w->enterModalState(true,
        juce::ModalCallbackFunction::create([this, w](int result)
            {
                if (result != 0)
                {
                    auto text = w->getTextEditorContents("bpm");
                    int  value = text.getIntValue();

                    if (value <= 0)
                        value = bpm;

                    bpm = juce::jlimit(40, 240, value);
                    bpmSet = true;
                    refreshBpmLabel();
                }
                else
                {
                    bpmSet = true;
                    refreshBpmLabel();
                }
            }),
        true);
}

void MainComponent::refreshBpmLabel()
{
    bpmLabel.setText("BPM: " + juce::String(bpm),
        juce::dontSendNotification);
}
