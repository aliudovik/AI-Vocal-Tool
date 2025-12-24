// MainComponent_AudioAndRecording.cpp
#include "MainComponent.h"

using int64 = juce::int64;

//==============================================================================

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;

    transportSource.prepareToPlay(samplesPerBlockExpected, sampleRate);
    takeTransport.prepareToPlay(samplesPerBlockExpected, sampleRate);
    takeTransport.setGain((float)takeVolumeSlider.getValue());

    if (samplesPerBlockExpected > 0)
        takeMixBuffer.setSize(1, samplesPerBlockExpected, false, false, true);

    if (samplesPerBlockExpected > 0)
    {
        recordingInputBuffer.setSize(1, samplesPerBlockExpected,
            false, false, true);
    }
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    auto* buffer = bufferToFill.buffer;
    const int start = bufferToFill.startSample;
    const int num = bufferToFill.numSamples;

    if (takeMixBuffer.getNumSamples() < num)
        takeMixBuffer.setSize(1, num, false, false, true);

    // Recording: grab input before overwriting buffer
    if (isRecording && recordingWriter != nullptr)
    {
        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            const int numInputChans =
                device->getActiveInputChannels().countNumberOfSetBits();

            if (numInputChans > 0)
            {
                const int bufferCapacity = recordingInputBuffer.getNumSamples();
                if (bufferCapacity > 0)
                {
                    const int samplesToProcess = juce::jmin(num, bufferCapacity);

                    recordingInputBuffer.clear();
                    auto* monoData = recordingInputBuffer.getWritePointer(0);

                    const int chansToCopy =
                        juce::jmin(numInputChans, buffer->getNumChannels());

                    for (int ch = 0; ch < chansToCopy; ++ch)
                    {
                        const float* src = buffer->getReadPointer(ch, start);
                        for (int i = 0; i < samplesToProcess; ++i)
                            monoData[i] += src[i];
                    }

                    if (chansToCopy > 1)
                    {
                        const float scale = 1.0f / (float)chansToCopy;
                        recordingInputBuffer.applyGain(scale);
                    }

                    {
                        const juce::ScopedLock sl(writerLock);
                        recordingWriter->writeFromAudioSampleBuffer(recordingInputBuffer,
                            0, samplesToProcess);
                    }

                    // Visual buffer
                    {
                        const juce::ScopedLock sl(vocalLock);

                        if (vocalBufferCapacitySamples > 0
                            && vocalWaveBuffer.getNumChannels() > 0)
                        {
                            const int remainingCapacity =
                                juce::jmax(0, vocalBufferCapacitySamples - totalRecordedSamples);

                            const int samplesToCopy =
                                juce::jmin(samplesToProcess, remainingCapacity);

                            if (samplesToCopy > 0)
                            {
                                vocalWaveBuffer.copyFrom(0, totalRecordedSamples,
                                    recordingInputBuffer, 0, 0,
                                    samplesToCopy);

                                totalRecordedSamples += samplesToCopy;

                                if (loopLengthSamples > 0)
                                {
                                    const int completedLoops =
                                        totalRecordedSamples / loopLengthSamples;
                                    const int remainder =
                                        totalRecordedSamples % loopLengthSamples;

                                    // Number of lanes we want to show:
                                    //
                                    // - all *completed* loops
                                    // - plus 1 extra lane for the *current* loop
                                    //   while it is still being recorded
                                    int loopsToRepresent = completedLoops;
                                    if (remainder > 0)
                                        ++loopsToRepresent;      // show in-progress loop

                                    // Safety: if we have *some* samples but less than one loop,
                                    // still show at least 1 lane (first loop in progress).
                                    if (totalRecordedSamples > 0 && loopsToRepresent == 0)
                                        loopsToRepresent = 1;

                                    while (takeTracks.size() < loopsToRepresent)
                                    {
                                        const int idx = takeTracks.size();

                                        TakeTrack t;
                                        t.startSample = idx * loopLengthSamples;
                                        t.numSamples = loopLengthSamples;   // full loop span
                                        t.name = "Take " + juce::String(idx + 1);

                                        takeTracks.add(t);
                                    }
                                }

                            }
                        }
                    }
                }
            }
        }
    }

    // 2) Start from silence
    bufferToFill.clearActiveBufferRegion();

    // 2a) Render instrumental if available
    if (readerSource.get() != nullptr)
    {
        transportSource.getNextAudioBlock(bufferToFill);

        const bool soloRecording =
            (!isRecording && viewMode == ViewMode::Recording && soloTakeIndex >= 0);
        const bool soloComped =
            (!isRecording && viewMode == ViewMode::CompReview && compedSolo);

        if (soloRecording || soloComped)
            bufferToFill.clearActiveBufferRegion();
    }

    // 3) Decide if take / comped should be heard
    bool playTake = false;

    if (!isRecording)
    {
        if (viewMode == ViewMode::Recording)
        {
            if (soloTakeIndex >= 0 || selectedTakeIndex >= 0)
                playTake = true;
        }
        else if (viewMode == ViewMode::CompReview)
        {
            if (compedSelected || compedSolo)
                playTake = true;
        }
    }

    if (playTake && takeReaderSource != nullptr)
    {
        takeMixBuffer.clear();

        juce::AudioSourceChannelInfo takeInfo(&takeMixBuffer, 0, num);
        takeTransport.getNextAudioBlock(takeInfo);

        for (int ch = 0; ch < buffer->getNumChannels(); ++ch)
            buffer->addFrom(ch, start, takeMixBuffer, 0, 0, num);
    }

    // 4) Metronome placeholder
    if (metronomeOn)
    {
		// FUTURE METRONOME, need quantization to beat grid
    }
}

void MainComponent::releaseResources()
{
    transportSource.releaseResources();
    takeTransport.releaseResources();

    const juce::ScopedLock sl(writerLock);
    recordingWriter.reset();

    takeTransport.setSource(nullptr);
    takeReaderSource.reset();
}

//==============================================================================
// Stop recording logic
//==============================================================================

void MainComponent::stopRecording()
{
    if (!isRecording)
        return;

    isRecording = false;
    recordButton.setButtonText("Record");

    transportSource.stop();

    int missingSamplesToPad = 0;

    {
        const juce::ScopedLock sl(vocalLock);

        if (loopLengthSamples > 0 && totalRecordedSamples > 0)
        {
            const int remainder = totalRecordedSamples % loopLengthSamples;

            if (remainder > 0)
            {
                missingSamplesToPad = loopLengthSamples - remainder;
                const int neededSamples = totalRecordedSamples + missingSamplesToPad;

                if (neededSamples > vocalBufferCapacitySamples)
                {
                    const int extra =
                        (currentSampleRate > 0.0
                            ? (int)(currentSampleRate * 10.0)
                            : 44100 * 10);

                    vocalBufferCapacitySamples = neededSamples + extra;

                    if (vocalWaveBuffer.getNumChannels() < 1)
                        vocalWaveBuffer.setSize(1, vocalBufferCapacitySamples,
                            false, false, false);
                    else
                        vocalWaveBuffer.setSize(1, vocalBufferCapacitySamples,
                            true, false, false);
                }

                vocalWaveBuffer.clear(0, totalRecordedSamples, missingSamplesToPad);
                totalRecordedSamples = neededSamples;
            }

            const int completedLoops = totalRecordedSamples / loopLengthSamples;

            while (takeTracks.size() < completedLoops)
            {
                const int idx = takeTracks.size();
                TakeTrack t;
                t.startSample = idx * loopLengthSamples;
                t.numSamples = loopLengthSamples;
                t.name = "Take " + juce::String(idx + 1);
                takeTracks.add(t);
            }
        }
    }

    int numLoopsForExport = 0;
    if (loopLengthSamples > 0 && totalRecordedSamples > 0)
        numLoopsForExport = totalRecordedSamples / loopLengthSamples;

    {
        const juce::ScopedLock sl(writerLock);

        if (recordingWriter != nullptr)
            recordingWriter->flush();

        recordingWriter.reset();
    }

    repaint();

    if (missingSamplesToPad > 0
        && loopLengthSamples > 0
        && currentFullRecordingFile.existsAsFile())
    {
        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager.createReaderFor(currentFullRecordingFile));

        if (reader != nullptr)
        {
            const double fileSampleRate = reader->sampleRate > 0.0
                ? reader->sampleRate
                : currentSampleRate;

            const int numChans = 1;

            juce::File paddedFile = currentFullRecordingFile.getSiblingFile(
                currentFullRecordingFile.getFileNameWithoutExtension()
                + "_padded.wav");

            std::unique_ptr<juce::FileOutputStream> outStream(
                paddedFile.createOutputStream());

            if (outStream != nullptr && outStream->openedOk())
            {
                std::unique_ptr<juce::AudioFormatWriter> padWriter(
                    wavFormat.createWriterFor(outStream.release(),
                        fileSampleRate,
                        (unsigned int)numChans,
                        16,
                        {},
                        0));

                if (padWriter != nullptr)
                {
                    const int64 originalSamples = reader->lengthInSamples;
                    const int   blockSize = 4096;

                    juce::AudioSampleBuffer copyBuffer(numChans, blockSize);

                    int64 pos = 0;
                    while (pos < originalSamples)
                    {
                        const int64 samplesThisBlock =
                            juce::jmin<int64>(blockSize, originalSamples - pos);
                        copyBuffer.clear();

                        reader->read(&copyBuffer,
                            0,
                            (int)samplesThisBlock,
                            pos,
                            true,
                            false);

                        padWriter->writeFromAudioSampleBuffer(copyBuffer, 0,
                            (int)samplesThisBlock);
                        pos += samplesThisBlock;
                    }

                    if (missingSamplesToPad > 0)
                    {
                        juce::AudioSampleBuffer silenceBuffer(numChans,
                            missingSamplesToPad);
                        silenceBuffer.clear();
                        padWriter->writeFromAudioSampleBuffer(silenceBuffer, 0,
                            missingSamplesToPad);
                    }

                    padWriter.reset();
                    paddedFile.moveFileTo(currentFullRecordingFile);
                }

                if (numLoopsForExport > 0 && currentFullRecordingFile.existsAsFile())
                    splitFullRecordingIntoTakes(currentFullRecordingFile,
                        numLoopsForExport);
            }
        }
    }
    else
    {
        if (numLoopsForExport > 0 && currentFullRecordingFile.existsAsFile())
            splitFullRecordingIntoTakes(currentFullRecordingFile, numLoopsForExport);
    }
    syncTakeLanesWithTakeTracks();
}

//==============================================================================
// Take selection / solo
//==============================================================================

void MainComponent::setSelectedTake(int newIndex)
{
    takeTransport.stop();
    takeTransport.setLooping(false);
    takeTransport.setSource(nullptr);
    takeReaderSource.reset();

    selectedTakeIndex = -1;
    soloTakeIndex = -1;

    if (newIndex < 0 || newIndex >= takeTracks.size())
    {
        repaint();
        return;
    }

    const int fileIndex = newIndex + 1;

    juce::File takeFile =
        currentPhraseDirectory.getChildFile("take_" + juce::String(fileIndex) + ".wav");

    if (!takeFile.existsAsFile())
    {
        repaint();
        return;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(takeFile));

    if (reader == nullptr)
    {
        repaint();
        return;
    }

    auto* rawReader = reader.release();
    takeReaderSource = std::make_unique<juce::AudioFormatReaderSource>(rawReader, true);

    takeTransport.setSource(takeReaderSource.get(),
        0,
        nullptr,
        currentSampleRate);

    takeTransport.setLooping(true);

    selectedTakeIndex = newIndex;

    if (transportSource.isPlaying() && hasValidLoop())
    {
        transportSource.setPosition(loopStartSec);
        takeTransport.setPosition(0.0);
        takeTransport.start();
    }
    else if (readerSource == nullptr)
    {
        takeTransport.setPosition(0.0);
        takeTransport.start();
    }

    refreshTakeLaneSelectionStates();
    repaint();
}

void MainComponent::setSoloTake(int newIndex)
{
    takeTransport.stop();
    takeTransport.setLooping(false);
    takeTransport.setSource(nullptr);
    takeReaderSource.reset();

    soloTakeIndex = -1;
    selectedTakeIndex = -1;

    if (newIndex < 0 || newIndex >= takeTracks.size())
    {
        repaint();
        return;
    }

    const int fileIndex = newIndex + 1;

    juce::File takeFile =
        currentPhraseDirectory.getChildFile("take_" + juce::String(fileIndex) + ".wav");

    if (!takeFile.existsAsFile())
    {
        repaint();
        return;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(takeFile));

    if (reader == nullptr)
    {
        repaint();
        return;
    }

    auto* rawReader = reader.release();
    takeReaderSource = std::make_unique<juce::AudioFormatReaderSource>(rawReader, true);

    takeTransport.setSource(takeReaderSource.get(),
        0,
        nullptr,
        currentSampleRate);

    takeTransport.setLooping(true);

    soloTakeIndex = newIndex;

    if (transportSource.isPlaying() && hasValidLoop())
    {
        transportSource.setPosition(loopStartSec);
        takeTransport.setPosition(0.0);
        takeTransport.start();
    }
    else if (readerSource == nullptr)
    {
        takeTransport.setPosition(0.0);
        takeTransport.start();
    }

    refreshTakeLaneSelectionStates();
    repaint();
}

//==============================================================================
// Import instrumental
//==============================================================================

void MainComponent::importInstrumental()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select an instrumental audio file...",
        juce::File(),
        "*.wav;*.aiff;*.aif;*.flac");

    auto flags = juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (!file.existsAsFile())
                return;

            currentInstrumentalFile = file;

            std::unique_ptr<juce::AudioFormatReader> reader(
                formatManager.createReaderFor(file));

            if (reader.get() == nullptr)
                return;

            auto newSource =
                std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);

            transportSource.stop();
            transportSource.setSource(nullptr);

            const double sr = newSource->getAudioFormatReader()->sampleRate;
            const double totalLengthSec =
                (double)newSource->getAudioFormatReader()->lengthInSamples / sr;

            transportSource.setSource(newSource.get(),
                0,
                nullptr,
                sr);
            transportSource.setLooping(false);

            readerSource = std::move(newSource);

            thumbnail.clear();
            thumbnail.setSource(new juce::FileInputSource(file));

            loopStartSec = 0.0;
            loopEndSec = totalLengthSec;
            minLoopLengthSec = juce::jmin(5.0, totalLengthSec);

            promptForBpm();

            playButton.setEnabled(true);
            stopButton.setEnabled(true);
            metronomeToggle.setEnabled(true);
            recordButton.setEnabled(true);

            repaint();
            fileChooser.reset();
        });
}

//==============================================================================
// Import takes from files
//==============================================================================

void MainComponent::importTakesFromFiles()
{
    if (!bpmSet)
        promptForBpm();

    if (isRecording)
        return;

    fileChooser = std::make_unique<juce::FileChooser>(
        "Select take_*.wav files to import...",
        juce::File(),
        "*.wav;*.aiff;*.aif;*.flac");

    auto flags = juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::canSelectMultipleItems;

    fileChooser->launchAsync(flags,
        [this](const juce::FileChooser& fc)
        {
            auto files = fc.getResults();

            if (files.isEmpty())
            {
                fileChooser.reset();
                return;
            }

            std::unique_ptr<juce::AudioFormatReader> firstReader(
                formatManager.createReaderFor(files[0]));

            if (firstReader == nullptr)
            {
                fileChooser.reset();
                return;
            }

            const double fileSampleRate = firstReader->sampleRate;
            const int64  fileNumSamples = firstReader->lengthInSamples;

            if (fileSampleRate <= 0.0 || fileNumSamples <= 0)
            {
                fileChooser.reset();
                return;
            }

            for (int i = 1; i < files.size(); ++i)
            {
                std::unique_ptr<juce::AudioFormatReader> r(
                    formatManager.createReaderFor(files[i]));

                if (r == nullptr
                    || r->sampleRate != fileSampleRate
                    || r->lengthInSamples != fileNumSamples)
                {
                    fileChooser.reset();
                    return;
                }
            }

            const int numImportedTakes = files.size();
            const int loopLenSamplesInt = (int)fileNumSamples;

            {
                const juce::ScopedLock sl(vocalLock);

                totalRecordedSamples = 0;
                takeTracks.clear();

                loopLengthSamples = loopLenSamplesInt;
                cachedLoopLengthSec = (double)fileNumSamples / fileSampleRate;

                vocalBufferCapacitySamples = numImportedTakes * loopLengthSamples;
                vocalWaveBuffer.setSize(1,
                    vocalBufferCapacitySamples,
                    false,
                    false,
                    false);

                juce::AudioSampleBuffer temp(1, loopLengthSamples);
                int writePos = 0;

                for (int i = 0; i < numImportedTakes; ++i)
                {
                    std::unique_ptr<juce::AudioFormatReader> r(
                        formatManager.createReaderFor(files[i]));

                    if (r == nullptr)
                        continue;

                    temp.clear();

                    r->read(&temp,
                        0,
                        loopLengthSamples,
                        0,
                        true,
                        false);

                    vocalWaveBuffer.copyFrom(0,
                        writePos,
                        temp,
                        0,
                        0,
                        loopLengthSamples);

                    TakeTrack t;
                    t.startSample = writePos;
                    t.numSamples = loopLengthSamples;
                    t.name = "Take " + juce::String(i + 1);

                    takeTracks.add(t);

                    writePos += loopLengthSamples;
                }

                totalRecordedSamples = writePos;
            }

            takeTransport.stop();
            takeTransport.setSource(nullptr);
            takeReaderSource.reset();
            selectedTakeIndex = -1;
            soloTakeIndex = -1;

            juce::File baseDir = currentPhraseDirectory;
            baseDir.createDirectory();

            nextTakeIndex = numImportedTakes + 1;

            for (int i = 0; i < files.size(); ++i)
            {
                const int fileIndex = i + 1;
                juce::File dest =
                    baseDir.getChildFile("take_" + juce::String(fileIndex) + ".wav");

                files[i].copyFileTo(dest);
            }

            syncTakeLanesWithTakeTracks();

            repaint();
            fileChooser.reset();
        });
}

//==============================================================================
// Phrase directory initialisation
//==============================================================================

void MainComponent::initialiseUserPhraseDirectory()
{
    juce::File exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory();

    juce::File projectRoot = exeDir;
    const juce::String wantedRootName{ "Vocal Comping Tool" };

    while (!projectRoot.isRoot()
        && projectRoot.getFileName() != wantedRootName)
    {
        auto parent = projectRoot.getParentDirectory();
        if (parent == projectRoot)
            break;

        projectRoot = parent;
    }

    if (projectRoot.getFileName() != wantedRootName)
    {
        projectRoot = exeDir.getParentDirectory().getParentDirectory();
    }

    juce::File dataPilotDir = projectRoot.getChildFile("data_pilot");
    juce::File singerDir = dataPilotDir.getChildFile("singer_user");

    dataPilotDir.createDirectory();
    singerDir.createDirectory();

    const int maxPhrases = 999;
    currentPhraseDirectory = juce::File();

    for (int idx = 1; idx <= maxPhrases; ++idx)
    {
        const juce::String phraseName = "phrase"
            + juce::String(idx).paddedLeft('0', 2);
        juce::File phraseDir = singerDir.getChildFile(phraseName);

        bool useThis = false;

        if (!phraseDir.exists())
        {
            phraseDir.createDirectory();
            useThis = true;
        }
        else
        {
            bool hasFiles = false;
            juce::DirectoryIterator it(phraseDir, false, "*", juce::File::findFiles);

            if (it.next())
                hasFiles = true;

            if (!hasFiles)
                useThis = true;
        }

        if (useThis)
        {
            currentPhraseDirectory = phraseDir;
            currentPhraseIndex = idx;
            break;
        }
    }

    if (!currentPhraseDirectory.exists())
    {
        currentPhraseDirectory = singerDir.getChildFile("phrase01");
        currentPhraseDirectory.createDirectory();
        currentPhraseIndex = 1;
    }
}

//==============================================================================
// Rebuild takes from phrase directory
//==============================================================================

namespace
{
    struct TakeFileComparator
    {
        static int getIndex(const juce::File& f)
        {
            auto name = f.getFileNameWithoutExtension();
            if (name.startsWithIgnoreCase("take_"))
                return name.fromFirstOccurrenceOf("take_", false, false).getIntValue();
            return 0;
        }

        int compareElements(const juce::File& a, const juce::File& b) const
        {
            const int ia = getIndex(a);
            const int ib = getIndex(b);
            return ia - ib;
        }
    };
}

void MainComponent::rebuildTakesFromPhraseDirectory()
{
    const juce::ScopedLock sl(vocalLock);

    vocalWaveBuffer.setSize(0, 0);
    takeTracks.clear();
    totalRecordedSamples = 0;
    loopLengthSamples = 0;
    vocalBufferCapacitySamples = 0;

    if (!currentPhraseDirectory.isDirectory())
        return;

    juce::Array<juce::File> takeFiles;
    currentPhraseDirectory.findChildFiles(takeFiles,
        juce::File::findFiles,
        false,
        "take_*.wav");

    if (takeFiles.isEmpty())
        return;

    takeFiles.sort(TakeFileComparator(), true);

    std::unique_ptr<juce::AudioFormatReader> firstReader(
        formatManager.createReaderFor(takeFiles[0]));

    if (firstReader == nullptr)
        return;

    const double sr = firstReader->sampleRate;
    const int    samplesPerTake = (int)firstReader->lengthInSamples;

    if (samplesPerTake <= 0)
        return;

    loopLengthSamples = samplesPerTake;
    cachedLoopLengthSec = (sr > 0.0) ? (double)samplesPerTake / sr : 0.0;

    const int numTakes = takeFiles.size();
    vocalBufferCapacitySamples = numTakes * samplesPerTake;

    vocalWaveBuffer.setSize(1,
        vocalBufferCapacitySamples,
        false, false, false);

    juce::AudioSampleBuffer temp(1, samplesPerTake);

    int writePos = 0;
    int maxIndexFound = 0;

    for (const auto& f : takeFiles)
    {
        std::unique_ptr<juce::AudioFormatReader> r(
            formatManager.createReaderFor(f));

        if (r == nullptr)
            continue;

        temp.clear();
        r->read(&temp,
            0,
            samplesPerTake,
            0,
            true,
            false);

        vocalWaveBuffer.copyFrom(0,
            writePos,
            temp,
            0,
            0,
            samplesPerTake);

        TakeTrack t;
        t.startSample = writePos;
        t.numSamples = samplesPerTake;

        int takeIdx = TakeFileComparator::getIndex(f);
        if (takeIdx <= 0)
            takeIdx = takeTracks.size() + 1;

        t.name = "Take " + juce::String(takeIdx);

        takeTracks.add(t);

        writePos += samplesPerTake;
        totalRecordedSamples = writePos;

        if (takeIdx > maxIndexFound)
            maxIndexFound = takeIdx;
    }

    if (maxIndexFound > 0)
        nextTakeIndex = maxIndexFound + 1;
    else
        nextTakeIndex = takeTracks.size() + 1;
}

//==============================================================================
// Split full recording into take_N.wav
//==============================================================================

void MainComponent::splitFullRecordingIntoTakes(const juce::File& fullFile, int numLoops)
{
    if (numLoops <= 0 || !fullFile.existsAsFile())
        return;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(fullFile));
    if (reader == nullptr)
        return;

    const int64 loopLenSamples = (int64)loopLengthSamples;
    if (loopLenSamples <= 0)
        return;

    const int64 totalFileSamples = reader->lengthInSamples;
    const int64 neededSamples = loopLenSamples * (int64)numLoops;

    const int64 usableSamples = juce::jmin(totalFileSamples, neededSamples);

    const int   blockSize = 4096;
    juce::AudioSampleBuffer tempBuffer(1, blockSize);

    juce::File baseDir = fullFile.getParentDirectory();

    for (int takeIdx = 0; takeIdx < numLoops; ++takeIdx)
    {
        const int64 takeStart = (int64)takeIdx * loopLenSamples;
        const int64 takeSamples = juce::jmin(loopLenSamples, usableSamples - takeStart);

        if (takeSamples <= 0)
            break;

        juce::File takeFile =
            baseDir.getChildFile("take_" + juce::String(nextTakeIndex++) + ".wav");

        std::unique_ptr<juce::FileOutputStream> outStream(takeFile.createOutputStream());
        if (outStream == nullptr || !outStream->openedOk())
            continue;

        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(outStream.release(),
                reader->sampleRate,
                1,
                16,
                {},
                0));

        if (writer == nullptr)
            continue;

        int64 remaining = takeSamples;
        int64 srcPos = takeStart;

        while (remaining > 0)
        {
            const int64 thisBlock = juce::jmin<int64>(blockSize, remaining);
            tempBuffer.clear();

            reader->read(&tempBuffer,
                0,
                (int)thisBlock,
                srcPos,
                true,
                false);

            writer->writeFromAudioSampleBuffer(tempBuffer, 0, (int)thisBlock);

            remaining -= thisBlock;
            srcPos += thisBlock;
        }
    }

    fullFile.deleteFile();
}
