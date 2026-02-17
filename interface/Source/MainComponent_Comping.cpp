// MainComponent_Comping.cpp
#include "MainComponent.h"
#include <thread>
#include "CompedAuditionSource.h"
#include "AppPaths.h"


//==============================================================================

void MainComponent::sanitizeCompSegments()
{
    // Fix start/end order
    for (auto& s : compSegments)
    {
        if (s.endSec < s.startSec)
            std::swap(s.startSec, s.endSec);
    }

    // Sort segments by start
    std::sort(compSegments.begin(), compSegments.end(),
              [](const CompSegment& a, const CompSegment& b)
              {
                  return a.startSec < b.startSec;
              });
}

void MainComponent::syncActiveCompResultFromLegacyState()
{
    if (activeCompResultIndex < 0 || activeCompResultIndex >= compResults.size())
        return;

    auto& result = compResults.getReference(activeCompResultIndex);
    result.compedFile = lastCompedFile;
    result.compmapFile = lastCompmapFile;
    result.alphaPct = lastCompAlphaPct;
    result.crossfadePct = lastCompCrossfadePct;
    result.fadeFraction = lastCompFadeFraction;
    result.segments = compSegments;
    result.boundaries = compBoundaries;
    result.compmapJson = compmapJsonCache;
    result.selected = compedSelected;
    result.solo = compedSolo;
}

void MainComponent::syncLegacyStateFromActiveCompResult()
{
    if (activeCompResultIndex < 0 || activeCompResultIndex >= compResults.size())
        return;

    const auto& result = compResults.getReference(activeCompResultIndex);
    lastCompedFile = result.compedFile;
    lastCompmapFile = result.compmapFile;
    lastCompAlphaPct = result.alphaPct;
    lastCompCrossfadePct = result.crossfadePct;
    lastCompFadeFraction = result.fadeFraction;
    compSegments = result.segments;
    compBoundaries = result.boundaries;
    compmapJsonCache = result.compmapJson;
    compedSelected = result.selected;
    compedSolo = result.solo;
}

bool MainComponent::setActiveCompResult(int index, bool prepareAudition)
{
    if (index < 0 || index >= compResults.size())
        return false;

    if (activeCompResultIndex >= 0
        && activeCompResultIndex < compResults.size()
        && activeCompResultIndex != index)
        syncActiveCompResultFromLegacyState();

    activeCompResultIndex = index;
    syncLegacyStateFromActiveCompResult();

    hasLastCompResult = (compResults.size() > 0);
    hasCompedThumbnail = false;
    compedThumbnail.clear();
    if (lastCompedFile.existsAsFile())
    {
        compedThumbnail.setSource(new juce::FileInputSource(lastCompedFile));
        hasCompedThumbnail = compedThumbnail.getTotalLength() > 0.0;
    }
    else if (lastCompedFile.getFullPathName().isNotEmpty())
    {
        DBG("setActiveCompResult: missing comped file: " + lastCompedFile.getFullPathName());
    }
    else
    {
        DBG("setActiveCompResult: empty comped file path");
    }

    rebuildCompResultThumbnails();

    if (compSegments.isEmpty() && lastCompmapFile.existsAsFile())
    {
        if (!loadLastCompForReview())
            DBG("setActiveCompResult: loadLastCompForReview() failed for active result");
    }
    else if (prepareAudition)
        prepareCompedAuditionSource();

    refreshCompedButtons();
    return true;
}

void MainComponent::rebuildCompResultThumbnails()
{
    for (auto& thumb : compResultThumbnails)
    {
        if (thumb != nullptr)
            thumb->removeChangeListener(this);
        thumb.reset();
    }

    const int rowsToBuild = juce::jmin(kMaxCompResultRows, compResults.size());
    for (int i = 0; i < rowsToBuild; ++i)
    {
        const auto& rowFile = compResults.getReference(i).compedFile;
        if (!rowFile.existsAsFile())
            continue;

        auto rowThumb = std::make_unique<juce::AudioThumbnail>(512, formatManager, thumbnailCache);
        rowThumb->addChangeListener(this);
        rowThumb->setSource(new juce::FileInputSource(rowFile));
        compResultThumbnails[(size_t)i] = std::move(rowThumb);
    }
}

juce::String MainComponent::getCompResultTitle(int index) const
{
    if (index == 0)
        return "COMPED TAKE 1";
    if (index == 1)
        return "Alternative 1";
    if (index == 2)
        return "Alternative 2";
    return "Comped";
}

void MainComponent::runCompingFromGui()
{
    // ---- QUICK VALIDATION ON UI THREAD ----
    
    juce::Component::SafePointer<MainComponent> safeThis(this);

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
    const juce::String keyModeValue = selectedKeyMode.isNotEmpty() ? selectedKeyMode : "chromatic";
    const juce::String keyRootValue = selectedKeyRoot.isNotEmpty() ? selectedKeyRoot : "C";
    const bool debugML = false; // ML DEBUG

    const juce::String phraseNum = juce::String(currentPhraseIndex).paddedLeft('0', 2);
    const juce::String select = "singer_user/phrase" + phraseNum;

    DBG("runCompingFromGui(): alphaPct=" << alphaPct
        << ", crossfadePct=" << crossfadePct
        << ", fadeFraction=" << fadeFraction
        << ", bpm=" << bpmValue
        << ", keyMode=" << keyModeValue
        << ", keyRoot=" << keyRootValue
        << ", select=" << select);

    const juce::String compedName =
        "comped-" + juce::String(alphaPct) + "-" + juce::String(crossfadePct) + ".wav";
    juce::File compedTargetFile = currentPhraseDirectory.getChildFile(compedName);
    juce::File compedAlt1File = currentPhraseDirectory.getChildFile(
        "comped-" + juce::String(alphaPct) + "-" + juce::String(crossfadePct) + "-alt1.wav");
    juce::File compedAlt2File = currentPhraseDirectory.getChildFile(
        "comped-" + juce::String(alphaPct) + "-" + juce::String(crossfadePct) + "-alt2.wav");

    const juce::String compmapName = "compmap-" + juce::String(alphaPct) + ".json";
    juce::File compmapTargetFile = currentPhraseDirectory.getChildFile(compmapName);

    // Cache latest run settings for new comp-result entries.
    lastCompAlphaPct = alphaPct;
    lastCompCrossfadePct = crossfadePct;
    lastCompFadeFraction = fadeFraction;

    
    

    const bool mlUsed = mlModeToggle.getToggleState();

    // Figure out project root and python path
    juce::File projectRoot = AppPaths::root();

    if (!projectRoot.isDirectory())
        projectRoot = juce::File::getCurrentWorkingDirectory();

    DBG("runCompingFromGui(): projectRoot=" << projectRoot.getFullPathName());
    juce::Logger::writeToLog("runCompingFromGui(): projectRoot=" + projectRoot.getFullPathName());

    

    // --- Cross-Platform Python Path ---
    juce::File pythonExe   = AppPaths::pythonExe();


    
    if (!pythonExe.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Comping error",
            "Bundled Python not found:\n" + pythonExe.getFullPathName());
        return;
    }
    
    DBG("Python exe: " + pythonExe.getFullPathName());
    juce::Logger::writeToLog("runCompingFromGui(): python exe=" + pythonExe.getFullPathName());
    // Build arguments for the Python call
    juce::File runCompingPy = AppPaths::srcDir().getChildFile("run_comping.py");

    if (!runCompingPy.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Comping error",
            "run_comping.py not found:\n" + runCompingPy.getFullPathName());
        return;
    }

    juce::StringArray args;
    args.add(pythonExe.getFullPathName());
    args.add("-u");  // unbuffered output ? critical for reliable capture
    args.add(runCompingPy.getFullPathName());
    args.add("--base");
    args.add("data_pilot");
    args.add("--select");
    args.add(select);
    args.add("--alpha_pct");
    args.add(juce::String(alphaPct));
    args.add("--bpm");
    args.add(juce::String(bpmValue));
    args.add("--key_mode");
    args.add(keyModeValue);
    args.add("--key_root");
    args.add(keyRootValue);
    args.add("--key_tolerance_cents");
    args.add("65.0");
    args.add("--fade_fraction");
    args.add(juce::String(fadeFraction));
    if (mlUsed)
    {
        args.add("--use_ml");
    }

    args.add("--out_dir");
    args.add(currentPhraseDirectory.getFullPathName());

    args.add("--out_comped_path");
    args.add(compedTargetFile.getFullPathName());
    args.add("--out_compmap_path");
    args.add(compmapTargetFile.getFullPathName());

    juce::Logger::writeToLog("runCompingFromGui(): python args " + args.joinIntoString(" "));

    // Make copies for the background thread (no references!)
    auto projectRootCopy = projectRoot;
    auto argsCopy = args;
    auto compedFileCopy = compedTargetFile;
    auto compedAlt1Copy = compedAlt1File;
    auto compedAlt2Copy = compedAlt2File;
    auto compmapFileCopy = compmapTargetFile;
    auto ppath = pythonExe.getFullPathName();
    
    // Build expected progress files
    juce::File scoringDir = currentPhraseDirectory.getChildFile(
        "scoring-user-" + phraseNum + "-" + juce::String(alphaPct));

    juce::File featuresFile = scoringDir.getChildFile(
        "features-user-" + phraseNum + "-" + juce::String(alphaPct) + ".csv");

    juce::File segmentsFile = scoringDir.getChildFile(
        "segments-user-" + phraseNum + "-" + juce::String(alphaPct) + ".csv");

    // Start progress bar with real files
    if (compingProgressComponent != nullptr)
    {
        compingProgressComponent->startCompingProgress(
            featuresFile, segmentsFile, compmapTargetFile, compedTargetFile);
    }
    

    // ---- DO THE HEAVY WORK ON A BACKGROUND THREAD ----
    std::thread([safeThis,
        projectRootCopy,
        argsCopy,
        compedFileCopy,
        compedAlt1Copy,
        compedAlt2Copy,
        compmapFileCopy,
        mlUsed, debugML]() mutable
    {
        bool success = false;
        bool compmapMissing = false;
        juce::String errorMessage;
        juce::String processOutput;
        juce::String mlServerMsg;

        juce::File oldCwd = juce::File::getCurrentWorkingDirectory();
        projectRootCopy.setAsCurrentWorkingDirectory();

        juce::ChildProcess process;

        // ---- Build a command string for logs and launch safely per-platform ----
        juce::String cmd = AppPaths::buildPythonCommand(argsCopy);

        juce::Logger::writeToLog("run_comping CMD: " + cmd);
        juce::Logger::writeToLog("run_comping CWD: " + projectRootCopy.getFullPathName());

#if JUCE_WINDOWS
        const bool started = process.start(cmd);
#else
        const bool started = process.start(argsCopy);
#endif

        if (!started)
        {
            errorMessage = "Could not launch Python process.\nCommand:\n" + cmd;
            juce::Logger::writeToLog("run_comping: process.start() FAILED");
        }
        else
        {
            juce::Logger::writeToLog("run_comping: process started OK, waiting...");

            const int maxMillis = 180000;
            const int pollMs = 250;
            int waited = 0;

            while (process.isRunning() && waited < maxMillis)
            {
                if (compedFileCopy.existsAsFile())
                    break;

                process.waitForProcessToFinish(pollMs);
                waited += pollMs;
            }

            // Make sure the process has fully exited before reading output
            if (process.isRunning())
            {
                if (waited >= maxMillis)
                    process.kill();
                else
                    process.waitForProcessToFinish(5000);
            }

            processOutput = process.readAllProcessOutput();
            juce::uint32 exitCode = process.getExitCode();

            // Log everything to python_debug.log for post-mortem analysis
            AppPaths::logPythonExecution("run_comping", cmd,
                projectRootCopy.getFullPathName(), processOutput, exitCode);

            // If ML mode was used, store the server response for UI display
            if (mlUsed && processOutput.isNotEmpty())
                mlServerMsg = processOutput;

            if (!compedFileCopy.existsAsFile())
            {
                if (waited >= maxMillis)
                {
                    errorMessage = "Comping timed out after " + juce::String(maxMillis / 1000) +
                        " seconds and no comped file was created.\n\n" +
                        "Try again or reduce workload (see notes).";
                }
                else
                {
                    // Include Python output in the error so the user can see what happened
                    errorMessage = "Python finished (exit code " + juce::String((int)exitCode)
                        + ") but the expected comped file was not found:\n"
                        + compedFileCopy.getFullPathName();

                    if (processOutput.isNotEmpty())
                    {
                        errorMessage += "\n\n--- Python output ---\n"
                            + processOutput.substring(0, 1500);
                    }
                    else
                    {
                        errorMessage += "\n\n(No Python output captured. "
                            "Check python_debug.log in the app folder for details.)";
                    }
                }
            }
            else
            {
                success = true;
                compmapMissing = !compmapFileCopy.existsAsFile();
            }
        }

        oldCwd.setAsCurrentWorkingDirectory();

        juce::MessageManager::callAsync([safeThis,
            success,
            compmapMissing,
            errorMessage,
            compedFileCopy,
            compedAlt1Copy,
            compedAlt2Copy,
            compmapFileCopy,
            mlUsed,
            mlServerMsg, debugML]() mutable
        {
            if (safeThis == nullptr)
                return;

            if (!success)
            {
                safeThis->onCompingFinished(false);

                if (errorMessage.isNotEmpty())
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        "Comping error",
                        errorMessage);
                }
                return;
            }

            // Show ML server response only in debug mode
            if (mlUsed && mlServerMsg.isNotEmpty() && debugML)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon,
                    "ML Debug Output",
                    mlServerMsg);
            }

            juce::Array<juce::File> producedCompedFiles;
            producedCompedFiles.add(compedFileCopy);
            if (compedAlt1Copy.existsAsFile())
                producedCompedFiles.add(compedAlt1Copy);
            if (compedAlt2Copy.existsAsFile())
                producedCompedFiles.add(compedAlt2Copy);

            if (producedCompedFiles.isEmpty())
            {
                safeThis->onCompingFinished(false);
                return;
            }

            safeThis->compResults.clear();
            for (int i = 0; i < producedCompedFiles.size(); ++i)
            {
                MainComponent::CompResult result;
                result.compedFile = producedCompedFiles.getReference(i);
                result.compmapFile = compmapFileCopy;
                result.alphaPct = safeThis->lastCompAlphaPct;
                result.crossfadePct = safeThis->lastCompCrossfadePct;
                result.fadeFraction = safeThis->lastCompFadeFraction;
                result.selected = (i == 0);
                result.solo = false;
                safeThis->compResults.add(result);
            }

            safeThis->hasLastCompResult = (safeThis->compResults.size() > 0);
            safeThis->activeCompResultIndex = -1; // avoid overwriting fresh row 0 from stale legacy fields
            if (!safeThis->setActiveCompResult(0, false))
            {
                safeThis->onCompingFinished(false);
                return;
            }

            // Load segment/boundary data for all alternatives so first paint is complete.
            for (int i = 1; i < safeThis->compResults.size(); ++i)
            {
                if (!safeThis->setActiveCompResult(i, false))
                    DBG("runCompingFromGui: failed to activate alternative row " + juce::String(i));
            }
            if (!safeThis->setActiveCompResult(0, false))
            {
                safeThis->onCompingFinished(false);
                return;
            }
            safeThis->rebuildCompResultThumbnails();

            if (!safeThis->lastCompedFile.getFullPathName().isNotEmpty())
            {
                const auto producedPrimary = producedCompedFiles.getFirst().getFullPathName();
                const auto row0Primary =
                    safeThis->compResults.isEmpty()
                        ? juce::String("<none>")
                        : safeThis->compResults.getReference(0).compedFile.getFullPathName();
                DBG("Comping primary-path debug: producedPrimary=" + producedPrimary
                    + " row0Primary=" + row0Primary
                    + " lastCompedFile=" + safeThis->lastCompedFile.getFullPathName());
                safeThis->onCompingFinished(false);
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Comping error",
                    "No primary comped path selected after comping.");
                return;
            }
            // Use compmap + CompedAuditionSource from the start so playback sample rate matches device (no Python WAV rate mismatch).
            if (!safeThis->loadLastCompForReview())
                safeThis->loadCompedFile(safeThis->lastCompedFile); // fallback if no compmap (waveform-only)

            safeThis->compedTabButton.setEnabled(true);
            safeThis->updateTabButtonStyles();

            safeThis->onCompingFinished(true);

            if (compmapMissing)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Comping warning",
                    "Comped audio was created, but the compmap JSON is missing:\n"
                    + compmapFileCopy.getFullPathName()
                    + "\n\nThe Comped tab will not show segment boundaries.");
            }

            juce::String msg;

            if (mlUsed)
            {
                msg = "ML comping successful. Model used: BalancedRandomForest.\n"
                      "Wrote " + juce::String(producedCompedFiles.size())
                      + " comped audio file(s), primary at:\n" + compedFileCopy.getFullPathName();
            }
            else
            {
                msg = "Comping successful.\n"
                      "Wrote " + juce::String(producedCompedFiles.size())
                      + " comped audio file(s), primary at:\n" + compedFileCopy.getFullPathName();
            }

            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Comping complete",
                msg,
                "OK",
                safeThis,
                juce::ModalCallbackFunction::create([safeThis](int)
                {
                    if (safeThis->compSegments.isEmpty() && !safeThis->loadLastCompForReview())
                        DBG("CompReview: loadLastCompForReview() failed");

                    if (!safeThis->segmentTimingTipShownOnce)
                    {
                        safeThis->segmentTimingTipShownOnce = true;
                        safeThis->segmentTimingTipShowUntilMs =
                            juce::Time::getMillisecondCounterHiRes() + 6500.0;
                    }

                    safeThis->viewMode = ViewMode::CompReview;
                    safeThis->updateTabButtonStyles();
                    safeThis->resized();
                    safeThis->repaint();
                }));
        });

    }).detach();
}


//==============================================================================

bool MainComponent::loadCompedFile(const juce::File& file)
{
    if (!file.getFullPathName().isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Comping error",
            "No primary comped path selected.");
        return false;
    }

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

    const double fileSampleRate = reader->sampleRate;
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
        fileSampleRate > 0.0 ? fileSampleRate : currentSampleRate);

    takeTransport.setLooping(true);

    takeReaderSource = std::move(newSource);

    return true;
}

//==============================================================================

bool MainComponent::loadLastCompForReview()
{
    DBG("loadLastCompForReview() called");

    if (compResults.isEmpty() && hasLastCompResult)
    {
        CompResult legacy;
        legacy.compedFile = lastCompedFile;
        legacy.compmapFile = lastCompmapFile;
        legacy.alphaPct = lastCompAlphaPct;
        legacy.crossfadePct = lastCompCrossfadePct;
        legacy.fadeFraction = lastCompFadeFraction;
        legacy.selected = compedSelected;
        legacy.solo = compedSolo;
        legacy.segments = compSegments;
        legacy.boundaries = compBoundaries;
        legacy.compmapJson = compmapJsonCache;
        compResults.add(legacy);
        activeCompResultIndex = 0;
    }

    if (!compResults.isEmpty())
    {
        if (activeCompResultIndex < 0 || activeCompResultIndex >= compResults.size())
            activeCompResultIndex = 0;
        syncLegacyStateFromActiveCompResult();
    }

    compSegments.clear();
    compBoundaries.clear();
    compmapJsonCache = juce::var();
    hasCompedThumbnail = false;
    compedThumbnail.clear();

    if (!hasLastCompResult && compResults.isEmpty())
        return false;

    if (!lastCompedFile.getFullPathName().isNotEmpty())
    {
        DBG("loadLastCompForReview: empty comped path");
        return false;
    }

    if (!lastCompedFile.existsAsFile())
    {
        DBG("loadLastCompForReview: comped file missing " + lastCompedFile.getFullPathName());
        return false;
    }

    compedThumbnail.setSource(new juce::FileInputSource(lastCompedFile));
    hasCompedThumbnail = (compedThumbnail.getTotalLength() > 0.0);

    if (!lastCompmapFile.existsAsFile())
    {
        DBG("loadLastCompForReview: no compmap file, using waveform only");
        return hasCompedThumbnail;
    }

    juce::FileInputStream in(lastCompmapFile);
    if (!in.openedOk())
        return hasCompedThumbnail;

    auto jsonVar = juce::JSON::parse(in);
    if (jsonVar.isVoid() || !jsonVar.isObject())
        return hasCompedThumbnail;

    // Cache full compmap JSON so edits won't destroy unrelated fields
    compmapJsonCache = jsonVar;

    auto* rootObj = jsonVar.getDynamicObject();
    if (rootObj == nullptr)
        return hasCompedThumbnail;

    // If compmap stores the default fade_fraction, cache it for fallback defaults
    {
        auto xfadeDefaultsVar = rootObj->getProperty("xfade_defaults");
        if (xfadeDefaultsVar.isObject())
        {
            if (auto* xfadeDefaultsObj = xfadeDefaultsVar.getDynamicObject())
            {
                auto ffVar = xfadeDefaultsObj->getProperty("fade_fraction");
                const double ff = (double)ffVar;
                if (ff > 0.0)
                    lastCompFadeFraction = ff;
            }
        }
    }

    auto segmentsVar = rootObj->getProperty("segments");
    if (!segmentsVar.isArray())
        return hasCompedThumbnail;

    auto* segmentsArray = segmentsVar.getArray();
    if (segmentsArray == nullptr)
        return hasCompedThumbnail;

    const int alternativeRank = juce::jlimit(0, 2, activeCompResultIndex);

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

        juce::StringArray rankedTakes;
        auto winnerVar = segObj->getProperty("winner");
        if (winnerVar.isObject())
            if (auto* winnerObj = winnerVar.getDynamicObject())
                rankedTakes.addIfNotAlreadyThere(winnerObj->getProperty("take").toString());

        auto candidatesVar = segObj->getProperty("candidates");
        if (candidatesVar.isArray())
        {
            if (auto* candidatesArr = candidatesVar.getArray())
            {
                for (const auto& candVar : *candidatesArr)
                {
                    if (!candVar.isObject())
                        continue;
                    if (auto* candObj = candVar.getDynamicObject())
                        rankedTakes.addIfNotAlreadyThere(candObj->getProperty("take").toString());
                }
            }
        }

        int takeIndex = -1;
        if (!rankedTakes.isEmpty())
        {
            const int takeChoiceIdx = juce::jmin(alternativeRank, rankedTakes.size() - 1);
            juce::String takeName = rankedTakes[takeChoiceIdx];
            if (takeName.startsWithIgnoreCase("take_"))
            {
                juce::String numStr = takeName.fromFirstOccurrenceOf("take_", false, false);
                takeIndex = numStr.getIntValue();
            }
            else
            {
                takeIndex = takeName.getIntValue();
            }
            if (takeIndex <= 0)
                takeIndex = -1;
        }

        CompSegment seg;
        seg.startSec = startSec;
        seg.endSec = endSec;
        if (segObj->hasProperty("source_offset_s"))
            seg.sourceOffsetSec = (double)segObj->getProperty("source_offset_s");
        else if (segObj->hasProperty("sourceOffsetSec"))
            seg.sourceOffsetSec = (double)segObj->getProperty("sourceOffsetSec");
        else
            seg.sourceOffsetSec = 0.0;
        seg.takeIndex = takeIndex;
        compSegments.add(seg);
        
        sanitizeCompSegments();
    }

    // Build per-boundary crossfade windows
    const int numBoundaries = juce::jmax(0, compSegments.size() - 1);
    compBoundaries.ensureStorageAllocated(numBoundaries);

    auto readDoubleProp = [](juce::DynamicObject* obj, const juce::String& key, double fallback) -> double
        {
            if (obj == nullptr)
                return fallback;
            auto v = obj->getProperty(key);
            if (v.isDouble() || v.isInt() || v.isInt64())
                return (double)v;
            if (v.isString())
                return v.toString().getDoubleValue();
            return fallback;
        };

    bool loadedBoundaries = false;
    auto boundariesVar = rootObj->getProperty("boundaries");

    if (boundariesVar.isArray())
    {
        if (auto* boundariesArray = boundariesVar.getArray())
        {
            if (boundariesArray->size() >= numBoundaries)
            {
                bool allOk = true;

                for (int b = 0; b < numBoundaries; ++b)
                {
                    auto elem = boundariesArray->getUnchecked(b);
                    if (!elem.isObject())
                    {
                        allOk = false;
                        break;
                    }

                    auto* obj = elem.getDynamicObject();
                    const double boundaryS = compSegments.getReference(b).endSec;
                    const double leftStart = compSegments.getReference(b).startSec;
                    const double rightEnd = compSegments.getReference(b + 1).endSec;

                    double xs = readDoubleProp(obj, "xfadeStartSec", boundaryS);
                    double xe = readDoubleProp(obj, "xfadeEndSec", boundaryS);

                    // Clamp: within segments, and include boundary
                    xs = juce::jlimit(leftStart, boundaryS, xs);
                    xe = juce::jlimit(boundaryS, rightEnd, xe);

                    constexpr double minWin = 0.005;
                    if (xe - xs < minWin)
                    {
                        const double half = minWin * 0.5;
                        xs = boundaryS - half;
                        xe = boundaryS + half;
                        xs = juce::jlimit(leftStart, boundaryS, xs);
                        xe = juce::jlimit(boundaryS, rightEnd, xe);
                    }

                    CompBoundary cb;
                    cb.leftSegIndex = b;
                    cb.xfadeStartSec = xs;
                    cb.xfadeEndSec = xe;
                    compBoundaries.add(cb);
                }

                loadedBoundaries = allOk && (compBoundaries.size() == numBoundaries);
                if (!loadedBoundaries)
                    compBoundaries.clear();
            }
        }
    }

    // Backward-compat defaults if boundaries missing/unusable
    if (!loadedBoundaries)
    {
        const double fadeFrac = (lastCompFadeFraction > 0.0) ? lastCompFadeFraction : 0.15;
        constexpr double minSec = 0.030;
        constexpr double maxSec = 0.500;
        constexpr double minWin = 0.005;

        for (int b = 0; b < numBoundaries; ++b)
        {
            const auto& left = compSegments.getReference(b);
            const auto& right = compSegments.getReference(b + 1);

            const double boundaryS = left.endSec;
            const double d1 = juce::jmax(0.0, left.endSec - left.startSec);
            const double d2 = juce::jmax(0.0, right.endSec - right.startSec);

            double fadeS = juce::jlimit(minSec, maxSec, juce::jmin(d1, d2) * fadeFrac);
            fadeS = juce::jmin(fadeS, d1 + d2);

            double xs = boundaryS - fadeS * 0.5;
            double xe = boundaryS + fadeS * 0.5;

            xs = juce::jmax(xs, left.startSec);
            xe = juce::jmin(xe, right.endSec);

            // Force include boundary
            xs = juce::jmin(xs, boundaryS);
            xe = juce::jmax(xe, boundaryS);

            if (xe - xs < minWin)
            {
                const double half = minWin * 0.5;
                xs = boundaryS - half;
                xe = boundaryS + half;
                xs = juce::jlimit(left.startSec, boundaryS, xs);
                xe = juce::jlimit(boundaryS, right.endSec, xe);
            }

            CompBoundary cb;
            cb.leftSegIndex = b;
            cb.xfadeStartSec = xs;
            cb.xfadeEndSec = xe;
            compBoundaries.add(cb);
        }
    }

    DBG("loadLastCompForReview: loaded " << compSegments.size()
        << " segments, boundaries=" << compBoundaries.size());

    prepareCompedAuditionSource();
    syncActiveCompResultFromLegacyState();

    return hasCompedThumbnail || !compSegments.isEmpty();

}



bool MainComponent::prepareCompedAuditionSource()
{
    hasCompedAuditionSource = false;

    // Critical order: detach transport before destroying source.
    takeTransport.stop();
    takeTransport.setSource(nullptr);
    takeReaderSource.reset();
    compedAuditionSource.reset();

    if (compSegments.isEmpty())
        return false;

    // Collect take_*.wav files for this phrase
    juce::Array<juce::File> takeFiles;
    currentPhraseDirectory.findChildFiles(
        takeFiles, juce::File::findFiles, false, "take_*.wav");

    if (takeFiles.isEmpty())
        return false;

    // Ensure formats are registered (safe to call multiple times)
    //formatManager.registerBasicFormats();

    auto src = std::make_unique<CompedAuditionSource>();

    juce::String err;
    if (!src->loadTakesIntoMemory(formatManager, takeFiles, currentSampleRate, err))
    {
        DBG("prepareCompedAuditionSource failed: " + err);
        return false;
    }

    // Convert JUCE arrays -> std::vector for the audition source
    std::vector<CompedAuditionSource::SegmentInfo> segs;
    segs.reserve((size_t)compSegments.size());
    for (const auto& s : compSegments)
    {
        CompedAuditionSource::SegmentInfo si;
        si.startSec = s.startSec;
        si.endSec = s.endSec;
        si.sourceOffsetSec = s.sourceOffsetSec;
        si.takeIndex = s.takeIndex;
        segs.push_back(si);
    }

    std::vector<CompedAuditionSource::BoundaryInfo> bounds;
    bounds.reserve((size_t)compBoundaries.size());
    for (const auto& b : compBoundaries)
    {
        CompedAuditionSource::BoundaryInfo bi;
        bi.leftSegIndex = b.leftSegIndex;
        bi.xfadeStartSec = b.xfadeStartSec;
        bi.xfadeEndSec = b.xfadeEndSec;
        bounds.push_back(bi);
    }

    src->setCompData(segs, bounds);
    src->setXFadeCurve(CompedAuditionSource::XFadeCurve::Linear);

    // Attach to takeTransport (replaces the rendered comped WAV for audition)
    selectedTakeIndex = -1;
    soloTakeIndex = -1;

    compedAuditionSource = std::move(src);
    takeTransport.setSource(compedAuditionSource.get(), 0, nullptr, currentSampleRate);
    takeTransport.setLooping(true);

    hasCompedAuditionSource = true;
    return true;
}


void MainComponent::clampBoundaryWindowToSegments(int boundaryIndex)
{
    if (boundaryIndex < 0 || boundaryIndex >= compBoundaries.size())
        return;
    if (boundaryIndex + 1 >= compSegments.size())
        return;

    auto& cb = compBoundaries.getReference(boundaryIndex);
    const auto& left = compSegments.getReference(boundaryIndex);
    const auto& right = compSegments.getReference(boundaryIndex + 1);

    const double boundaryS = left.endSec;
    double xs = cb.xfadeStartSec;
    double xe = cb.xfadeEndSec;

    constexpr double minWin = 0.005;

    if (xe - xs < minWin)
    {
        const double half = minWin * 0.5;
        xs = boundaryS - half;
        xe = boundaryS + half;
    }

    const double lo1 = juce::jmin(left.startSec, boundaryS);
    const double hi1 = juce::jmax(left.startSec, boundaryS);
    xs = juce::jlimit(lo1, hi1, xs);

    const double lo2 = juce::jmin(boundaryS, right.endSec);
    const double hi2 = juce::jmax(boundaryS, right.endSec);
    xe = juce::jlimit(lo2, hi2, xe);

    if (xe - xs < minWin)
    {
        const double half = minWin * 0.5;
        xs = boundaryS - half;
        xe = boundaryS + half;
        xs = juce::jlimit(left.startSec, boundaryS, xs);
        xe = juce::jlimit(boundaryS, right.endSec, xe);
    }

    cb.xfadeStartSec = xs;
    cb.xfadeEndSec = xe;
}

void MainComponent::updateCompedAuditionSourceFromEdits()
{
    if (compedAuditionSource == nullptr)
        return;

    std::vector<CompedAuditionSource::SegmentInfo> segs;
    segs.reserve((size_t)compSegments.size());
    for (const auto& s : compSegments)
    {
        CompedAuditionSource::SegmentInfo si;
        si.startSec = s.startSec;
        si.endSec = s.endSec;
        si.sourceOffsetSec = s.sourceOffsetSec;
        si.takeIndex = s.takeIndex;
        segs.push_back(si);
    }

    std::vector<CompedAuditionSource::BoundaryInfo> bounds;
    bounds.reserve((size_t)compBoundaries.size());
    for (const auto& b : compBoundaries)
    {
        CompedAuditionSource::BoundaryInfo bi;
        bi.leftSegIndex = b.leftSegIndex;
        bi.xfadeStartSec = b.xfadeStartSec;
        bi.xfadeEndSec = b.xfadeEndSec;
        bounds.push_back(bi);
    }

    compedAuditionSource->setCompData(segs, bounds);
    compedAuditionSource->setXFadeCurve(CompedAuditionSource::XFadeCurve::Linear);
    syncActiveCompResultFromLegacyState();
}

//==============================================================================
// Export helpers (Option 2: render CompedAuditionSource directly)
//==============================================================================

bool MainComponent::buildCompedAuditionSourceForExport(std::unique_ptr<CompedAuditionSource>& outSource,
                                                       double& outLengthSec,
                                                       juce::String& outError)
{
    outSource.reset();
    outLengthSec = 0.0;
    outError.clear();

    if (compSegments.isEmpty())
    {
        outError = "No comp segments available.";
        return false;
    }

    // Collect take_*.wav files for this phrase
    juce::Array<juce::File> takeFiles;
    currentPhraseDirectory.findChildFiles(
        takeFiles, juce::File::findFiles, false, "take_*.wav");

    if (takeFiles.isEmpty())
    {
        outError = "No take_*.wav files found for export.";
        return false;
    }

    auto src = std::make_unique<CompedAuditionSource>();

    const double sr = (currentSampleRate > 0.0) ? currentSampleRate : 44100.0;

    if (!src->loadTakesIntoMemory(formatManager, takeFiles, sr, outError))
        return false;

    // Convert JUCE arrays -> std::vector for the audition source
    std::vector<CompedAuditionSource::SegmentInfo> segs;
    segs.reserve((size_t)compSegments.size());
    for (const auto& s : compSegments)
    {
        CompedAuditionSource::SegmentInfo si;
        si.startSec = s.startSec;
        si.endSec = s.endSec;
        si.sourceOffsetSec = s.sourceOffsetSec;
        si.takeIndex = s.takeIndex;
        segs.push_back(si);
    }

    std::vector<CompedAuditionSource::BoundaryInfo> bounds;
    bounds.reserve((size_t)compBoundaries.size());
    for (const auto& b : compBoundaries)
    {
        CompedAuditionSource::BoundaryInfo bi;
        bi.leftSegIndex = b.leftSegIndex;
        bi.xfadeStartSec = b.xfadeStartSec;
        bi.xfadeEndSec = b.xfadeEndSec;
        bounds.push_back(bi);
    }

    src->setCompData(segs, bounds);
    src->setXFadeCurve(CompedAuditionSource::XFadeCurve::Linear);

    // Compute total length from last segment end
    outLengthSec = compSegments.getLast().endSec;
    if (outLengthSec <= 0.0 && compedThumbnail.getTotalLength() > 0.0)
        outLengthSec = compedThumbnail.getTotalLength();

    if (outLengthSec <= 0.0)
    {
        outError = "Could not determine comped length for export.";
        return false;
    }

    outSource = std::move(src);
    return true;
}

void MainComponent::exportCompedAuditionToFileAsync(const juce::File& targetFile)
{
    juce::Component::SafePointer<MainComponent> safeThis(this);

    std::thread([safeThis, targetFile]() mutable
    {
        if (safeThis == nullptr)
            return;

        double lengthSec = 0.0;
        juce::String err;
        std::unique_ptr<CompedAuditionSource> src;

        if (!safeThis->buildCompedAuditionSourceForExport(src, lengthSec, err))
        {
            juce::MessageManager::callAsync([safeThis, err]()
            {
                if (safeThis == nullptr) return;
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Export failed",
                    err.isNotEmpty() ? err : "Could not build comped audition source.");
            });
            return;
        }

        const double sr = (safeThis->currentSampleRate > 0.0) ? safeThis->currentSampleRate : 44100.0;
        const juce::int64 totalSamples = (juce::int64) std::ceil(lengthSec * sr);

        if (totalSamples <= 0)
        {
            juce::MessageManager::callAsync([safeThis]()
            {
                if (safeThis == nullptr) return;
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Export failed",
                    "Export length is zero.");
            });
            return;
        }

        juce::WavAudioFormat wav;
        juce::File outFile = targetFile;
        if (outFile.existsAsFile())
            outFile.deleteFile();

        std::unique_ptr<juce::FileOutputStream> outStream(outFile.createOutputStream());
        if (outStream == nullptr || !outStream->openedOk())
        {
            juce::MessageManager::callAsync([safeThis, outFile]()
            {
                if (safeThis == nullptr) return;
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Export failed",
                    "Could not create output stream:\n" + outFile.getFullPathName());
            });
            return;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(outStream.release(), sr, 1, 16, {}, 0));

        if (writer == nullptr)
        {
            juce::MessageManager::callAsync([safeThis]()
            {
                if (safeThis == nullptr) return;
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Export failed",
                    "Could not create WAV writer.");
            });
            return;
        }

        constexpr int blockSize = 8192;
        juce::AudioBuffer<float> buffer(1, blockSize);
        juce::AudioSourceChannelInfo info(&buffer, 0, blockSize);

        src->prepareToPlay(blockSize, sr);
        src->setNextReadPosition(0);

        juce::int64 written = 0;
        while (written < totalSamples)
        {
            const juce::int64 remaining = totalSamples - written;
            const int numThis = (int) ((remaining < (juce::int64) blockSize)
                ? remaining
                : (juce::int64) blockSize);
            buffer.clear();
            info.numSamples = numThis;

            src->getNextAudioBlock(info);
            writer->writeFromAudioSampleBuffer(buffer, 0, numThis);

            written += numThis;
        }

        src->releaseResources();

        juce::MessageManager::callAsync([safeThis, outFile]()
        {
            if (safeThis == nullptr) return;
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Export successful",
                "Comped file exported to:\n" + outFile.getFullPathName());
        });
    }).detach();
}




void MainComponent::rebuildCompedFromEditedCompmapAsync()
{
    juce::Component::SafePointer<MainComponent> safeThis(this);

    if (!hasLastCompResult)
        return;
    if (!lastCompmapFile.existsAsFile())
        return;
    if (lastCompedFile.getFullPathName().isEmpty())
        return;

    juce::File projectRoot = AppPaths::root();
    if (!projectRoot.isDirectory())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Comping error",
            "App root not found:\n" + projectRoot.getFullPathName());
        return;
    }



    juce::File pythonExe = AppPaths::pythonExe();

    if (!pythonExe.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Crossfade rebuild error",
            "Python virtual environment not found at:\n" + pythonExe.getFullPathName());
        return;
    }

    juce::File stitchPy = AppPaths::srcDir().getChildFile("stitch_only.py");

    if (!stitchPy.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Crossfade rebuild error",
            "stitch_only.py not found:\n" + stitchPy.getFullPathName());
        return;
    }

    juce::StringArray args;
    args.add(pythonExe.getFullPathName());
    args.add("-u");  // unbuffered output for reliable capture
    args.add(stitchPy.getFullPathName());
    args.add("--compmap_path");
    args.add(lastCompmapFile.getFullPathName());
    args.add("--out_comped_path");
    args.add(lastCompedFile.getFullPathName());

    juce::String cmdLine = AppPaths::buildPythonCommand(args);
    juce::Logger::writeToLog("rebuildComped CMD: " + cmdLine);

    auto projectRootCopy = projectRoot;
    auto argsCopy = args;
    auto cmdLineCopy = cmdLine;
    auto compedCopy = lastCompedFile;

    std::thread([safeThis, projectRootCopy, argsCopy, cmdLineCopy, compedCopy]() mutable
        {
            bool success = false;
            juce::String errorMessage;

            juce::File oldCwd = juce::File::getCurrentWorkingDirectory();
            projectRootCopy.setAsCurrentWorkingDirectory();

            juce::ChildProcess process;
#if JUCE_WINDOWS
            const bool started = process.start(cmdLineCopy);
#else
            const bool started = process.start(argsCopy);
#endif
            if (!started)
            {
                errorMessage = "Could not launch Python stitch_only process.\nCommand:\n" + cmdLineCopy;
                juce::Logger::writeToLog("stitch_only: process.start() FAILED");
            }
            else
            {
                juce::Logger::writeToLog("stitch_only: process started OK, waiting...");

                const int maxMillis = 120000;
                const bool finished = process.waitForProcessToFinish(maxMillis);

                juce::String processOutput = process.readAllProcessOutput();
                juce::uint32 exitCode = process.getExitCode();

                AppPaths::logPythonExecution("stitch_only", cmdLineCopy,
                    projectRootCopy.getFullPathName(), processOutput, exitCode);

                if (!finished)
                {
                    errorMessage = "Stitch-only timed out after "
                        + juce::String(maxMillis / 1000) + " seconds.";
                    process.kill();
                }
                else if (!compedCopy.existsAsFile())
                {
                    errorMessage = "Python finished (exit code " + juce::String((int)exitCode)
                        + ") but the expected comped file was not found:\n"
                        + compedCopy.getFullPathName();

                    if (processOutput.isNotEmpty())
                    {
                        errorMessage += "\n\n--- Python output ---\n"
                            + processOutput.substring(0, 1500);
                    }
                    else
                    {
                        errorMessage += "\n\n(No Python output captured. "
                            "Check python_debug.log in the app folder for details.)";
                    }
                }
                else
                {
                    success = true;
                }
            }

            oldCwd.setAsCurrentWorkingDirectory();

            juce::MessageManager::callAsync([safeThis, success, errorMessage, compedCopy]() mutable
                {
                    if (safeThis == nullptr)
                        return;

                    if (!success)
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::AlertWindow::WarningIcon,
                            "Crossfade rebuild error",
                            errorMessage);
                        return;
                    }

                    safeThis->loadCompedFile(compedCopy);
                    safeThis->loadLastCompForReview(); // refresh thumbnail + boundaries
                    safeThis->repaint();
                });
        }).detach();
}
