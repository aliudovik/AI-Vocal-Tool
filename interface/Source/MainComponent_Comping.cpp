// MainComponent_Comping.cpp
#include "MainComponent.h"
#include <thread>

//==============================================================================



void MainComponent::runCompingFromGui()
{
    // ---- QUICK VALIDATION ON UI THREAD ----

    if (isRecording)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Comping unavailable",
            "Please stop recording before running vocal comping.");
        return;
    }

    if (!bpmSet || bpm <= 0)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "BPM required",
            "Please set BPM before running vocal comping.");
        return;
    }

    juce::Array<juce::File> takeFiles;
    currentPhraseDirectory.findChildFiles(
        takeFiles,
        juce::File::findFiles,
        false,
        "take_*.wav");

    if (takeFiles.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "No takes found",
            "There are no take_*.wav files in this phrase folder.\n"
            "Record or import some takes before running comping.");
        return;
    }

    const int alphaPctRaw = juce::roundToInt(accuracyEmotionSlider.getValue());
    const int alphaPct = juce::jlimit(0, 100, alphaPctRaw);

    const double cfSliderVal = crossfadeSlider.getValue();
    const int    crossfadePct = juce::jlimit(0, 100, juce::roundToInt(cfSliderVal));
    const double fadeFraction = juce::jmap(cfSliderVal, 0.0, 100.0, 0.05, 0.30);
    const int    bpmValue = bpm;

    const juce::String phraseNum = juce::String(currentPhraseIndex).paddedLeft('0', 2);
    const juce::String select = "singer_user/phrase" + phraseNum;

    DBG("runCompingFromGui(): alphaPct=" << alphaPct
        << ", crossfadePct=" << crossfadePct
        << ", fadeFraction=" << fadeFraction
        << ", bpm=" << bpmValue
        << ", select=" << select);

    const juce::String compedName =
        "comped-" + juce::String(alphaPct) + "-" + juce::String(crossfadePct) + ".wav";
    juce::File compedTargetFile = currentPhraseDirectory.getChildFile(compedName);

    const juce::String compmapName = "compmap-" + juce::String(alphaPct) + ".json";
    juce::File compmapTargetFile = currentPhraseDirectory.getChildFile(compmapName);

    // Remember last result metadata (file paths & settings)
    lastCompedFile = compedTargetFile;
    lastCompmapFile = compmapTargetFile;
    lastCompAlphaPct = alphaPct;
    lastCompCrossfadePct = crossfadePct;
    lastCompFadeFraction = fadeFraction;
    compedSelected = true;
    compedSolo = false;
    refreshCompedButtons();

    // Figure out project root and python path
    juce::File projectRoot =
        currentPhraseDirectory.getParentDirectory()
        .getParentDirectory()
        .getParentDirectory();

    if (!projectRoot.isDirectory())
        projectRoot = juce::File::getCurrentWorkingDirectory();

    DBG("runCompingFromGui(): projectRoot=" << projectRoot.getFullPathName());

    juce::File pythonExe = projectRoot
        .getChildFile(".venv")
        .getChildFile("Scripts")
        .getChildFile("python.exe");

    if (!pythonExe.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Comping error",
            "Python virtual environment not found at:\n"
            + pythonExe.getFullPathName()
            + "\n\nCheck your venv location and update runCompingFromGui().");
        return;
    }

    // Build arguments for the Python call
    juce::StringArray args;
    args.add(pythonExe.getFullPathName());
    args.add("-m");
    args.add("src.run_comping");
    args.add("--base");
    args.add("data_pilot");
    args.add("--select");
    args.add(select);
    args.add("--alpha_pct");
    args.add(juce::String(alphaPct));
    args.add("--bpm");
    args.add(juce::String(bpmValue));
    args.add("--fade_fraction");
    args.add(juce::String(fadeFraction));

    args.add("--out_dir");
    args.add(currentPhraseDirectory.getFullPathName());

    args.add("--out_comped_path");
    args.add(compedTargetFile.getFullPathName());
    args.add("--out_compmap_path");
    args.add(compmapTargetFile.getFullPathName());

    // Make copies for the background thread (no references!)
    auto projectRootCopy = projectRoot;
    auto argsCopy = args;
    auto compedFileCopy = compedTargetFile;
    auto compmapFileCopy = compmapTargetFile;

    // ---- DO THE HEAVY WORK ON A BACKGROUND THREAD ----
    std::thread([this,
        projectRootCopy,
        argsCopy,
        compedFileCopy,
        compmapFileCopy]() mutable
        {
            bool success = false;
            bool compmapMissing = false;
            juce::String errorMessage;
            juce::String processOutput;

            juce::File oldCwd = juce::File::getCurrentWorkingDirectory();
            projectRootCopy.setAsCurrentWorkingDirectory();

            juce::ChildProcess process;
            if (!process.start(argsCopy))
            {
                errorMessage = "Could not launch Python process.\n"
                    "Command: " + argsCopy.joinIntoString(" ");
            }
            else
            {
                // This is the blocking part
                process.waitForProcessToFinish(-1);
                processOutput = process.readAllProcessOutput();

                DBG("run_comping output:\n" + processOutput);

                // Check that the comped file exists
                if (!compedFileCopy.existsAsFile())
                {
                    errorMessage = "Python finished but the expected comped file "
                        "was not found:\n"
                        + compedFileCopy.getFullPathName();
                }
                else
                {
                    success = true;
                    compmapMissing = !compmapFileCopy.existsAsFile();
                }
            }

            // Restore CWD
            oldCwd.setAsCurrentWorkingDirectory();

            // Jump back to JUCE message thread for all UI work
            juce::MessageManager::callAsync([this,
                success,
                compmapMissing,
                errorMessage,
                compedFileCopy,
                compmapFileCopy]() mutable
                {
                    if (!success)
                    {
                        onCompingFinished(false);

                        if (errorMessage.isNotEmpty())
                        {
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::AlertWindow::WarningIcon,
                                "Comping error",
                                errorMessage);
                        }

                        return;
                    }

                    // Successful comping – load the file into the transport
                    if (!loadCompedFile(compedFileCopy))
                    {
                        onCompingFinished(false);
                        return;
                    }

                    hasLastCompResult = true;
                    lastCompedFile = compedFileCopy;
                    lastCompmapFile = compmapFileCopy;

                    compedTabButton.setEnabled(true);
                    updateTabButtonStyles();

                    // Tell the progress component to jump to 100% and close
                    onCompingFinished(true);

                    // Optional warning if the compmap is missing
                    if (compmapMissing)
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::AlertWindow::WarningIcon,
                            "Comping warning",
                            "Comped audio was created, but the compmap JSON is missing:\n"
                            + compmapFileCopy.getFullPathName()
                            + "\n\nThe Comped tab will not show segment boundaries.");
                    }

                    // Final "comping complete" dialog that switches to the Comped tab
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::InfoIcon,
                        "Comping complete",
                        "The comped take has been created and loaded.\n"
                        "Press PLAY to listen.\n\n"
                        "You can now review it on the Comped tab.",
                        "OK",
                        this,
                        juce::ModalCallbackFunction::create([this](int)
                            {
                                if (!loadLastCompForReview())
                                    DBG("CompReview: loadLastCompForReview() failed");

                                viewMode = ViewMode::CompReview;
                                updateTabButtonStyles();
                                resized();
                                repaint();
                            }));
                });

        }).detach();
}


//==============================================================================

bool MainComponent::loadCompedFile(const juce::File& file)
{
    if (!file.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Comping error",
            "Comped file does not exist:\n" + file.getFullPathName());
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));

    if (reader == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Comping error",
            "Could not open the comped WAV file:\n" + file.getFullPathName());
        return false;
    }

    auto newSource =
        std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);

    takeTransport.stop();
    takeTransport.setSource(nullptr);
    takeReaderSource.reset();

    selectedTakeIndex = -1;
    soloTakeIndex = -1;

    takeTransport.setSource(
        newSource.get(),
        0,
        nullptr,
        currentSampleRate);

    takeTransport.setLooping(true);

    takeReaderSource = std::move(newSource);

    return true;
}

//==============================================================================

bool MainComponent::loadLastCompForReview()
{
    DBG("loadLastCompForReview() called");

    compSegments.clear();
    hasCompedThumbnail = false;
    compedThumbnail.clear();

    if (!hasLastCompResult)
    {
        DBG("loadLastCompForReview: hasLastCompResult is false");
        return false;
    }

    if (!lastCompedFile.existsAsFile())
    {
        DBG("loadLastCompForReview: lastCompedFile missing: "
            << lastCompedFile.getFullPathName());
        return false;
    }

    compedThumbnail.setSource(new juce::FileInputSource(lastCompedFile));
    hasCompedThumbnail = (compedThumbnail.getTotalLength() > 0.0);

    if (!hasCompedThumbnail)
    {
        DBG("loadLastCompForReview: compedThumbnail total length is zero");
    }

    if (!lastCompmapFile.existsAsFile())
    {
        DBG("loadLastCompForReview: lastCompmapFile missing: "
            << lastCompmapFile.getFullPathName()
            << " (no segment markers will be shown).");
        return hasCompedThumbnail;
    }

    juce::FileInputStream in(lastCompmapFile);
    if (!in.openedOk())
    {
        DBG("loadLastCompForReview: could not open compmap file stream");
        return hasCompedThumbnail;
    }

    auto jsonVar = juce::JSON::parse(in);
    if (jsonVar.isVoid() || !jsonVar.isObject())
    {
        DBG("loadLastCompForReview: JSON root is not an object");
        return hasCompedThumbnail;
    }

    auto* rootObj = jsonVar.getDynamicObject();
    if (rootObj == nullptr)
    {
        DBG("loadLastCompForReview: JSON root dynamic object is null");
        return hasCompedThumbnail;
    }

    auto segmentsVar = rootObj->getProperty("segments");
    if (!segmentsVar.isArray())
    {
        DBG("loadLastCompForReview: 'segments' is not an array");
        return hasCompedThumbnail;
    }

    auto* segmentsArray = segmentsVar.getArray();
    if (segmentsArray == nullptr)
    {
        DBG("loadLastCompForReview: 'segments' array pointer is null");
        return hasCompedThumbnail;
    }

    for (const auto& segVar : *segmentsArray)
    {
        if (!segVar.isObject())
            continue;

        auto* segObj = segVar.getDynamicObject();
        if (segObj == nullptr)
            continue;

        const double startSec = (double)segObj->getProperty("start_s");
        const double endSec = (double)segObj->getProperty("end_s");

        if (!(endSec > startSec))
            continue;

        int takeIndex = -1;

        auto winnerVar = segObj->getProperty("winner");
        if (winnerVar.isObject())
        {
            if (auto* winnerObj = winnerVar.getDynamicObject())
            {
                auto takeNameVar = winnerObj->getProperty("take");
                if (takeNameVar.isString())
                {
                    juce::String takeName = takeNameVar.toString();

                    if (takeName.startsWithIgnoreCase("take_"))
                    {
                        juce::String numStr =
                            takeName.fromFirstOccurrenceOf("take_", false, false);
                        takeIndex = numStr.getIntValue();
                    }
                    else
                    {
                        takeIndex = takeName.getIntValue();
                    }

                    if (takeIndex <= 0)
                        takeIndex = -1;
                }
            }
        }

        CompSegment seg;
        seg.startSec = startSec;
        seg.endSec = endSec;
        seg.takeIndex = takeIndex;

        compSegments.add(seg);
    }

    DBG("loadLastCompForReview: loaded " << compSegments.size()
        << " segments, hasCompedThumbnail=" << (int)hasCompedThumbnail);

    return hasCompedThumbnail || !compSegments.isEmpty();
}
