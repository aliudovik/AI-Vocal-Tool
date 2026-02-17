// MainComponent_ProjectState.cpp
#include "MainComponent.h"
#include "AppPaths.h"

namespace
{
    juce::File getPhraseDirectoryFallback(int phraseIndexHint)
    {
        auto singerDir = AppPaths::singerUserDir();
        singerDir.createDirectory();

        const int safeIndex = juce::jmax(1, phraseIndexHint);
        const juce::String phraseName = "phrase" + juce::String(safeIndex).paddedLeft('0', 2);
        auto phraseDir = singerDir.getChildFile(phraseName);

        if (!phraseDir.exists())
            phraseDir.createDirectory();

        if (phraseDir.isDirectory())
            return phraseDir;

        return singerDir;
    }

    juce::File getValidPhraseDirectory(const juce::File& candidate, int phraseIndexHint)
    {
        if (candidate.isDirectory())
            return candidate;

        return getPhraseDirectoryFallback(phraseIndexHint);
    }

    juce::StringArray getKeyModes()
    {
        return { "Major", "Minor", "Chromatic" };
    }

    juce::StringArray getKeyRootsEnharmonic()
    {
        return { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
    }

    juce::String normaliseKeyMode(const juce::String& mode)
    {
        auto m = mode.trim().toLowerCase();
        if (m == "major") return "major";
        if (m == "minor") return "minor";
        return "chromatic";
    }

    int modeToComboId(const juce::String& mode)
    {
        const auto m = normaliseKeyMode(mode);
        if (m == "major") return 1;
        if (m == "minor") return 2;
        return 3;
    }

    juce::String comboIdToMode(int id)
    {
        if (id == 1) return "major";
        if (id == 2) return "minor";
        return "chromatic";
    }

    int rootToComboId(const juce::String& root)
    {
        const auto roots = getKeyRootsEnharmonic();
        auto needle = root.trim().toUpperCase();
        for (int i = 0; i < roots.size(); ++i)
        {
            auto label = roots[i].toUpperCase();
            if (label == needle)
                return i + 1;
            if (label.contains("/"))
            {
                auto parts = juce::StringArray::fromTokens(label, "/", {});
                for (auto& p : parts)
                {
                    if (p.trim() == needle)
                        return i + 1;
                }
            }
        }
        return 1; // C
    }

    juce::String comboIdToRoot(int id)
    {
        const auto roots = getKeyRootsEnharmonic();
        if (id <= 0 || id > roots.size())
            return "C";
        return roots[id - 1];
    }
}

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
    s.selectedKeyMode = selectedKeyMode;
    s.selectedKeyRoot = selectedKeyRoot;

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
        cs.sourceOffsetSec = seg.sourceOffsetSec;
        cs.takeIndex = seg.takeIndex;
        s.compSegments.add(cs);
    }
    
    // SAVE grid offset
    s.gridOffsetSec = gridOffsetSec;

    // SAVE manual crossfade boundaries
    s.compBoundaries.clear();
    for (const auto& b : compBoundaries)
    {
        CompBoundaryState bs;
        bs.leftSegIndex = b.leftSegIndex;
        bs.xfadeStartSec = b.xfadeStartSec;
        bs.xfadeEndSec = b.xfadeEndSec;
        s.compBoundaries.add(bs);
    }

    juce::Array<CompResult> saveResults = compResults;
    if (activeCompResultIndex >= 0 && activeCompResultIndex < saveResults.size())
    {
        auto& active = saveResults.getReference(activeCompResultIndex);
        active.compedFile = lastCompedFile;
        active.compmapFile = lastCompmapFile;
        active.alphaPct = lastCompAlphaPct;
        active.crossfadePct = lastCompCrossfadePct;
        active.fadeFraction = lastCompFadeFraction;
        active.segments = compSegments;
        active.boundaries = compBoundaries;
        active.selected = compedSelected;
        active.solo = compedSolo;
    }

    s.activeCompResultIndex = activeCompResultIndex;
    for (const auto& r : saveResults)
    {
        CompResultState rs;
        rs.hasResult = r.compedFile.existsAsFile() || r.compedFile.getFullPathName().isNotEmpty();
        rs.compedFilePath = r.compedFile.getFullPathName();
        rs.compmapFilePath = r.compmapFile.getFullPathName();
        rs.alphaPct = r.alphaPct;
        rs.crossfadePct = r.crossfadePct;
        rs.fadeFraction = r.fadeFraction;
        rs.selected = r.selected;
        rs.solo = r.solo;

        for (const auto& seg : r.segments)
        {
            CompSegmentState cs;
            cs.startSec = seg.startSec;
            cs.endSec = seg.endSec;
            cs.sourceOffsetSec = seg.sourceOffsetSec;
            cs.takeIndex = seg.takeIndex;
            rs.segments.add(cs);
        }
        for (const auto& b : r.boundaries)
        {
            CompBoundaryState bs;
            bs.leftSegIndex = b.leftSegIndex;
            bs.xfadeStartSec = b.xfadeStartSec;
            bs.xfadeEndSec = b.xfadeEndSec;
            rs.boundaries.add(bs);
        }
        s.compResults.add(rs);
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
    visibleStartSec = 0.0;
    visibleEndSec = 0.0;

    bpm = 120;
    bpmSet = false;
    metronomeOn = false;
    selectedKeyMode = "chromatic";
    selectedKeyRoot = "C";

    refreshBpmLabel();
    refreshKeyLabel();
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
    
    gridOffsetSec = 0.0;
    compBoundaries.clear();

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
    compResults.clear();
    activeCompResultIndex = -1;
    rebuildCompResultThumbnails();
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
    
    gridOffsetSec = s.gridOffsetSec;

    currentPhraseIndex = s.currentPhraseIndex;
    if (currentPhraseIndex <= 0)
        currentPhraseIndex = 1;

    currentPhraseDirectory = getValidPhraseDirectory(juce::File(s.currentPhraseDirectory),
                                                     currentPhraseIndex);

    bpm = s.bpm;
    bpmSet = s.bpmSet;
    metronomeOn = s.metronomeOn;
    selectedKeyMode = normaliseKeyMode(s.selectedKeyMode);
    if (selectedKeyMode.isEmpty())
        selectedKeyMode = "chromatic";
    selectedKeyRoot = s.selectedKeyRoot.isEmpty() ? juce::String("C") : s.selectedKeyRoot;
    metronomeToggle.setToggleState(metronomeOn, juce::dontSendNotification);
    refreshBpmLabel();
    refreshKeyLabel();

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

            visibleStartSec = 0.0;
            visibleEndSec = totalLengthSec;
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

    compResults.clear();
    if (!s.compResults.isEmpty())
    {
        const int count = juce::jmin(3, s.compResults.size());
        for (int i = 0; i < count; ++i)
        {
            const auto& rs = s.compResults.getReference(i);
            if (!rs.compedFilePath.isNotEmpty())
                continue;
            CompResult r;
            r.compedFile = juce::File(rs.compedFilePath);
            r.compmapFile = juce::File(rs.compmapFilePath);
            r.alphaPct = rs.alphaPct;
            r.crossfadePct = rs.crossfadePct;
            r.fadeFraction = rs.fadeFraction;
            r.selected = rs.selected;
            r.solo = rs.solo;

            for (const auto& cs : rs.segments)
            {
                CompSegment seg;
                seg.startSec = cs.startSec;
                seg.endSec = cs.endSec;
                seg.sourceOffsetSec = cs.sourceOffsetSec;
                seg.takeIndex = cs.takeIndex;
                r.segments.add(seg);
            }
            for (const auto& bs : rs.boundaries)
            {
                CompBoundary cb;
                cb.leftSegIndex = bs.leftSegIndex;
                cb.xfadeStartSec = bs.xfadeStartSec;
                cb.xfadeEndSec = bs.xfadeEndSec;
                r.boundaries.add(cb);
            }
            if (r.compedFile.getFullPathName().isNotEmpty())
                compResults.add(r);
        }
        activeCompResultIndex = juce::jlimit(0, juce::jmax(0, compResults.size() - 1), s.activeCompResultIndex);
    }
    else if (s.hasLastCompResult)
    {
        CompResult legacy;
        legacy.compedFile = juce::File(s.lastCompedFilePath);
        legacy.compmapFile = juce::File(s.lastCompmapFilePath);
        legacy.alphaPct = s.lastCompAlphaPct;
        legacy.crossfadePct = s.lastCompCrossfadePct;
        legacy.fadeFraction = s.lastCompFadeFraction;
        legacy.selected = s.compedSelected;
        legacy.solo = s.compedSolo;
        for (const auto& cs : s.compSegments)
        {
            CompSegment seg;
            seg.startSec = cs.startSec;
            seg.endSec = cs.endSec;
            seg.sourceOffsetSec = cs.sourceOffsetSec;
            seg.takeIndex = cs.takeIndex;
            legacy.segments.add(seg);
        }
        for (const auto& bs : s.compBoundaries)
        {
            CompBoundary cb;
            cb.leftSegIndex = bs.leftSegIndex;
            cb.xfadeStartSec = bs.xfadeStartSec;
            cb.xfadeEndSec = bs.xfadeEndSec;
            legacy.boundaries.add(cb);
        }
        compResults.add(legacy);
        activeCompResultIndex = 0;
    }

    hasLastCompResult = !compResults.isEmpty();
    if (hasLastCompResult)
    {
        setActiveCompResult(activeCompResultIndex, false);
        // Prefer CompedAuditionSource (device sample rate) when we have segments; use file only when no compmap.
        if (compSegments.isEmpty() && lastCompmapFile.existsAsFile())
            loadLastCompForReview();
        else if (!compSegments.isEmpty())
        {
            prepareCompedAuditionSource();
            if (lastCompmapFile.existsAsFile())
            {
                juce::FileInputStream in(lastCompmapFile);
                if (in.openedOk())
                {
                    auto jsonVar = juce::JSON::parse(in);
                    if (jsonVar.isObject())
                        compmapJsonCache = jsonVar;
                }
            }
        }
        else if (lastCompedFile.existsAsFile())
            loadCompedFile(lastCompedFile);
        compedTabButton.setEnabled(true);
    }
    else
    {
        compedTabButton.setEnabled(false);
        rebuildCompResultThumbnails();
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
    currentPhraseDirectory = getValidPhraseDirectory(currentPhraseDirectory, currentPhraseIndex);

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

            // --- user cancelled ---
            if (!target.getFullPathName().isNotEmpty())
            {
                quitAfterSave = false;
                return;
            }

            if (target.getFileExtension().isEmpty())
                target = target.withFileExtension(".json");

            juce::String error;
            if (!ProjectState::saveToFile(state, target, error))
            {
                quitAfterSave = false;
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

            // --- quit if this save was triggered by "Save and Close"
            if (quitAfterSave)
            {
                quitAfterSave = false;
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            }
        });
}

void MainComponent::launchProjectLoadChooser()
{
    auto chooserStartDir = getValidPhraseDirectory(currentPhraseDirectory, currentPhraseIndex);

    fileChooser = std::make_unique<juce::FileChooser>(
        "Load project...",
        chooserStartDir,
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


// Suggest saving upon the program closing

void MainComponent::promptSaveOnExit()
{
    auto* w = new juce::AlertWindow(
        "Quit",
        "Any unsaved changes will be lost! This alpha doesn't have an automatic backup feature.",
        juce::AlertWindow::WarningIcon);

    w->addButton("Save and Close", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Close - no saving", 0);

    w->enterModalState(true,
        juce::ModalCallbackFunction::create([this, w](int result)
        {
            if (result == 1)  // Save and Close
            {
                quitAfterSave = true;
                saveProjectToFile();   // your existing function
            }
            else              // Close - no saving
            {
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            }
        }),
        true);
}



//==============================================================================
// BPM helpers
//==============================================================================

void MainComponent::promptForBpm()
{
    auto* w = new juce::AlertWindow("Set BPM", {}, juce::AlertWindow::NoIcon);

    w->setSize(450, 310);

    // ---- full‑width centred message ----
    auto* msg = new juce::Label();
    msg->setText("BPM is essential for successful Vocal Comping.\n\n"
                 "To align, click shift and drag your mouse on the beat grid.",
                 juce::dontSendNotification);
    msg->setJustificationType(juce::Justification::centred);
    msg->setMinimumHorizontalScale(1.0f);

    const int margin = 24;
    msg->setSize(w->getWidth() - margin * 2, 80);   // full width minus equal margins
    w->addCustomComponent(msg);
    // -----------------------------------

    w->addTextEditor("bpm", juce::String(bpm), "BPM:");
    if (auto* editor = w->getTextEditor("bpm"))
        editor->setJustification(juce::Justification::centred);

    w->addComboBox("key_mode", getKeyModes(), "Mode:");
    if (auto* modeBox = w->getComboBoxComponent("key_mode"))
    {
        modeBox->setSelectedId(modeToComboId(selectedKeyMode), juce::dontSendNotification);
    }

    w->addComboBox("key_root", getKeyRootsEnharmonic(), "Key:");
    if (auto* rootBox = w->getComboBoxComponent("key_root"))
    {
        rootBox->setSelectedId(rootToComboId(selectedKeyRoot), juce::dontSendNotification);
    }

    if (auto* modeBox = w->getComboBoxComponent("key_mode"))
    {
        modeBox->onChange = [w]()
        {
            auto* mode = w->getComboBoxComponent("key_mode");
            auto* root = w->getComboBoxComponent("key_root");
            if (mode == nullptr || root == nullptr)
                return;
            root->setEnabled(comboIdToMode(mode->getSelectedId()) != "chromatic");
        };
        modeBox->onChange();
    }

    w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));

    w->centreAroundComponent(this, w->getWidth(), w->getHeight());

    w->enterModalState(true,
        juce::ModalCallbackFunction::create([this, w](int result)
        {
            if (result != 0)
            {
                auto text = w->getTextEditorContents("bpm");
                int value = text.getIntValue();
                if (value <= 0) value = bpm;

                bpm = juce::jlimit(40, 240, value);
                bpmSet = true;
                refreshBpmLabel();

                if (auto* modeBox = w->getComboBoxComponent("key_mode"))
                    selectedKeyMode = comboIdToMode(modeBox->getSelectedId());
                if (auto* rootBox = w->getComboBoxComponent("key_root"))
                    selectedKeyRoot = comboIdToRoot(rootBox->getSelectedId());
                refreshKeyLabel();
            }
            else
            {
                bpmSet = true;
                refreshBpmLabel();
            }
        }),
        true);
}

void MainComponent::promptForKeySelection()
{
    auto* w = new juce::AlertWindow("Set Key", {}, juce::AlertWindow::NoIcon);
    w->setSize(360, 180);

    w->addComboBox("key_mode", getKeyModes(), "Mode:");
    if (auto* modeBox = w->getComboBoxComponent("key_mode"))
        modeBox->setSelectedId(modeToComboId(selectedKeyMode), juce::dontSendNotification);

    w->addComboBox("key_root", getKeyRootsEnharmonic(), "Key:");
    if (auto* rootBox = w->getComboBoxComponent("key_root"))
        rootBox->setSelectedId(rootToComboId(selectedKeyRoot), juce::dontSendNotification);

    if (auto* modeBox = w->getComboBoxComponent("key_mode"))
    {
        modeBox->onChange = [w]()
        {
            auto* mode = w->getComboBoxComponent("key_mode");
            auto* root = w->getComboBoxComponent("key_root");
            if (mode == nullptr || root == nullptr)
                return;
            root->setEnabled(comboIdToMode(mode->getSelectedId()) != "chromatic");
        };
        modeBox->onChange();
    }

    w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->centreAroundComponent(this, w->getWidth(), w->getHeight());

    w->enterModalState(true,
        juce::ModalCallbackFunction::create([this, w](int result)
        {
            if (result == 1)
            {
                if (auto* modeBox = w->getComboBoxComponent("key_mode"))
                    selectedKeyMode = comboIdToMode(modeBox->getSelectedId());
                if (auto* rootBox = w->getComboBoxComponent("key_root"))
                    selectedKeyRoot = comboIdToRoot(rootBox->getSelectedId());
                refreshKeyLabel();
            }
        }),
        true);
}

void MainComponent::refreshBpmLabel()
{
    bpmLabel.setText("BPM: " + juce::String(bpm),
        juce::dontSendNotification);
}

void MainComponent::refreshKeyLabel()
{
    const auto mode = normaliseKeyMode(selectedKeyMode);
    if (mode == "chromatic")
    {
        keyLabel.setText("Key: Chromatic", juce::dontSendNotification);
        return;
    }

    const auto modeUi = (mode == "major") ? juce::String("Major") : juce::String("Minor");
    const auto rootUi = selectedKeyRoot.isEmpty() ? juce::String("C") : selectedKeyRoot;
    keyLabel.setText("Key: " + rootUi + " " + modeUi, juce::dontSendNotification);
}
