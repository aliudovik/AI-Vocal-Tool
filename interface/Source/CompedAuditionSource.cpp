#include "CompedAuditionSource.h"

#include <cmath>

int CompedAuditionSource::parseTakeIndexFromFilename(const juce::File& f)
{
    // take_3.wav -> 3
    auto base = f.getFileNameWithoutExtension(); // "take_3"
    if (base.startsWithIgnoreCase("take_"))
        base = base.fromFirstOccurrenceOf("take_", false, false);

    return base.getIntValue(); // returns 0 if invalid
}

juce::AudioBuffer<float> CompedAuditionSource::downmixToMono(const juce::AudioBuffer<float>& in)
{
    const int n = in.getNumSamples();
    juce::AudioBuffer<float> mono(1, n);
    mono.clear();

    if (in.getNumChannels() <= 0 || n <= 0)
        return mono;

    if (in.getNumChannels() == 1)
    {
        mono.copyFrom(0, 0, in, 0, 0, n);
        return mono;
    }

    // Average first 2 channels (or all channels if you want)
    const int chCount = in.getNumChannels();
    for (int ch = 0; ch < chCount; ++ch)
        mono.addFrom(0, 0, in, ch, 0, n, 1.0f);

    mono.applyGain(1.0f / (float)chCount);
    return mono;
}

juce::AudioBuffer<float> CompedAuditionSource::resampleMono(const juce::AudioBuffer<float>& inMono,
    double inRate,
    double outRate)
{
    if (inMono.getNumChannels() != 1)
        return downmixToMono(inMono);

    if (inRate <= 0.0 || outRate <= 0.0 || std::abs(inRate - outRate) < 1.0e-6)
        return inMono;

    const int inN = inMono.getNumSamples();
    if (inN <= 0)
        return inMono;

    // LagrangeInterpolator: speedRatio = inputRate / outputRate
    const double speed = inRate / outRate;
    const int outN = juce::jmax(1, (int)std::llround((double)inN * (outRate / inRate)));

    juce::AudioBuffer<float> out(1, outN);
    out.clear();

    // Pad input a little so the interpolator can safely read ahead
    std::vector<float> padded((size_t)inN + 8, 0.0f);
    std::memcpy(padded.data(), inMono.getReadPointer(0), (size_t)inN * sizeof(float));

    juce::LagrangeInterpolator interp;
    interp.reset();

    interp.process(speed, padded.data(), out.getWritePointer(0), outN);
    return out;
}

bool CompedAuditionSource::loadTakesIntoMemory(juce::AudioFormatManager& fm,
    const juce::Array<juce::File>& takeFiles,
    double targetSampleRate,
    juce::String& outError)
{
    takes.clear();
    takeIndexToSlot.clear();
    segments.clear();
    boundaries.clear();

    if (targetSampleRate <= 0.0)
    {
        outError = "Target sample rate is invalid.";
        return false;
    }

    sourceSampleRateHz = targetSampleRate;

    if (takeFiles.isEmpty())
    {
        outError = "No take_*.wav files provided.";
        return false;
    }

    for (const auto& f : takeFiles)
    {
        if (!f.existsAsFile())
            continue;

        const int takeIndex = parseTakeIndexFromFilename(f);
        if (takeIndex <= 0)
            continue;

        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(f));
        if (reader == nullptr)
        {
            outError = "Could not open take file: " + f.getFullPathName();
            return false;
        }

        const auto inRate = reader->sampleRate;
        const auto length64 = reader->lengthInSamples;
        const int length = (length64 > (juce::int64)std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : (int)length64;

        if (length <= 0)
            continue;

        juce::AudioBuffer<float> tmp(reader->numChannels, length);
        tmp.clear();

        reader->read(&tmp, 0, length, 0, true, true);

        auto mono = downmixToMono(tmp);
        auto monoResampled = resampleMono(mono, inRate, sourceSampleRateHz);

        TakeBuffer tb;
        tb.takeIndex = takeIndex;
        tb.mono = std::move(monoResampled);

        const int slot = (int)takes.size();
        takes.push_back(std::move(tb));
        takeIndexToSlot[takeIndex] = slot;
    }

    if (takes.empty())
    {
        outError = "No valid take buffers loaded (check take_*.wav files).";
        return false;
    }

    return true;
}

void CompedAuditionSource::setCompData(const std::vector<SegmentInfo>& newSegments,
    const std::vector<BoundaryInfo>& newBoundaries)
{
    const juce::SpinLock::ScopedLockType sl(compDataLock);
    
    segments.clear();
    boundaries.clear();
    totalLengthSeconds = 0.0;
    totalLengthSamples = 0;
    cachedSegIndex = 0;
    positionSamples = 0;

    if (newSegments.empty())
        return;

    segments.reserve(newSegments.size());

    for (const auto& s : newSegments)
    {
        SegmentState ss;
        ss.startSec = s.startSec;
        ss.endSec = s.endSec;
        ss.sourceOffsetSec = s.sourceOffsetSec;

        auto it = takeIndexToSlot.find(s.takeIndex);
        ss.takeSlot = (it != takeIndexToSlot.end()) ? it->second : -1;

        segments.push_back(ss);
        totalLengthSeconds = juce::jmax(totalLengthSeconds, ss.endSec);
    }

    totalLengthSamples = (juce::int64)std::llround(totalLengthSeconds * sourceSampleRateHz);

    // Boundaries: allocate exactly (segments.size()-1) in boundary-index order
    const int nb = juce::jmax(0, (int)segments.size() - 1);
    boundaries.resize((size_t)nb);

    // Fill with defaults centered at the boundary time as a safe fallback
    for (int b = 0; b < nb; ++b)
    {
        const double boundaryS = segments[(size_t)b].endSec;
        boundaries[(size_t)b].leftSegIndex = b;
        boundaries[(size_t)b].xfadeStartSec.store(boundaryS);
        boundaries[(size_t)b].xfadeEndSec.store(boundaryS);
    }

    // Apply provided boundary windows (expected b == leftSegIndex)
    for (const auto& bi : newBoundaries)
    {
        const int b = bi.leftSegIndex;
        if (b < 0 || b >= nb)
            continue;

        boundaries[(size_t)b].leftSegIndex = b;
        boundaries[(size_t)b].xfadeStartSec.store(bi.xfadeStartSec);
        boundaries[(size_t)b].xfadeEndSec.store(bi.xfadeEndSec);
    }
}

void CompedAuditionSource::setBoundaryWindow(int boundaryIndex, double xfadeStartSec, double xfadeEndSec) noexcept
{
    
    const juce::SpinLock::ScopedLockType sl(compDataLock);
    
    if (boundaryIndex < 0 || boundaryIndex >= (int)boundaries.size())
        return;

    boundaries[(size_t)boundaryIndex].xfadeStartSec.store(xfadeStartSec);
    boundaries[(size_t)boundaryIndex].xfadeEndSec.store(xfadeEndSec);
}

void CompedAuditionSource::prepareToPlay(int /*samplesPerBlockExpected*/, double /*sampleRate*/)
{
    // Nothing to allocate here; we run from memory buffers.
}

void CompedAuditionSource::releaseResources()
{
    // Nothing.
}

void CompedAuditionSource::setNextReadPosition(juce::int64 newPosition)
{
    positionSamples = juce::jmax((juce::int64)0, newPosition);
}

juce::int64 CompedAuditionSource::getNextReadPosition() const
{
    return positionSamples;
}

juce::int64 CompedAuditionSource::getTotalLength() const
{
    return totalLengthSamples;
}

bool CompedAuditionSource::isLooping() const
{
    return looping;
}

void CompedAuditionSource::setLooping(bool shouldLoop)
{
    looping = shouldLoop;
}

int CompedAuditionSource::findSegmentIndexForTime(double tSec) const noexcept
{
    if (segments.empty())
        return 0;

    int lo = 0;
    int hi = (int)segments.size() - 1;

    while (lo <= hi)
    {
        const int mid = (lo + hi) / 2;
        const auto& s = segments[(size_t)mid];

        if (tSec < s.startSec)
            hi = mid - 1;
        else if (tSec >= s.endSec)
            lo = mid + 1;
        else
            return mid;
    }

    // Clamp to nearest
    return juce::jlimit(0, (int)segments.size() - 1, lo);
}

double CompedAuditionSource::mapTimelineToSourceTimeSec(const SegmentState& seg, double timelineSec) const noexcept
{
    return timelineSec + seg.sourceOffsetSec;
}

inline float CompedAuditionSource::readSampleFromTakeSlot(int takeSlot, juce::int64 sampleIndex) const noexcept
{
    if (takeSlot < 0 || takeSlot >= (int)takes.size())
        return 0.0f;

    const auto& tb = takes[(size_t)takeSlot];
    const int n = tb.mono.getNumSamples();
    if (sampleIndex < 0 || sampleIndex >= (juce::int64)n)
        return 0.0f;

    return tb.mono.getSample(0, (int)sampleIndex);
}

void CompedAuditionSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    
    const juce::SpinLock::ScopedTryLockType sl(compDataLock);
    if (!sl.isLocked())
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }
    
    auto* out = bufferToFill.buffer;
    const int start = bufferToFill.startSample;
    const int num = bufferToFill.numSamples;

    if (out == nullptr || num <= 0)
        return;

    out->clear(start, num);

    if (segments.empty() || takes.empty() || totalLengthSamples <= 0)
    {
        positionSamples += num;
        return;
    }

    // Keep position inside range if looping
    const juce::int64 lenSamp = totalLengthSamples;

    // Establish initial segment index for this block
    juce::int64 blockPos = positionSamples;
    if (looping)
        blockPos = (lenSamp > 0) ? (blockPos % lenSamp) : blockPos;

    const double t0 = (double)blockPos / sourceSampleRateHz;
    cachedSegIndex = findSegmentIndexForTime(t0);

    for (int i = 0; i < num; ++i)
    {
        juce::int64 sPos = positionSamples + i;
        if (looping)
            sPos = (lenSamp > 0) ? (sPos % lenSamp) : sPos;

        const double tSec = (double)sPos / sourceSampleRateHz;

        // Advance cached segment if needed (sequential-friendly)
        while (cachedSegIndex + 1 < (int)segments.size()
            && tSec >= segments[(size_t)cachedSegIndex].endSec)
            ++cachedSegIndex;

        while (cachedSegIndex > 0
            && tSec < segments[(size_t)cachedSegIndex].startSec)
            --cachedSegIndex;

        cachedSegIndex = juce::jlimit(0, (int)segments.size() - 1, cachedSegIndex);

        const int segIdx = cachedSegIndex;

        // Default: winner for current segment, with optional slip offset.
        const auto& currentSeg = segments[(size_t)segIdx];
        const double sourceSec = mapTimelineToSourceTimeSec(currentSeg, tSec);
        const juce::int64 sourceSample = (juce::int64)std::llround(sourceSec * sourceSampleRateHz);
        float sampleOut = readSampleFromTakeSlot(currentSeg.takeSlot, sourceSample);

        // Boundary mixing check (only adjacent boundaries can cover current segment)
        auto doMix = [&](int bIndex) -> bool
            {
                if (bIndex < 0 || bIndex >= (int)boundaries.size())
                    return false;

                const double xs = boundaries[(size_t)bIndex].xfadeStartSec.load();
                const double xe = boundaries[(size_t)bIndex].xfadeEndSec.load();

                if (!(xe > xs))
                    return false;

                if (tSec < xs || tSec >= xe)
                    return false;

                const int leftSeg = bIndex;
                const int rightSeg = bIndex + 1;
                if (rightSeg >= (int)segments.size())
                    return false;

                const auto& leftState = segments[(size_t)leftSeg];
                const auto& rightState = segments[(size_t)rightSeg];

                const double leftSourceSec = mapTimelineToSourceTimeSec(leftState, tSec);
                const double rightSourceSec = mapTimelineToSourceTimeSec(rightState, tSec);
                const juce::int64 leftSourceSample = (juce::int64)std::llround(leftSourceSec * sourceSampleRateHz);
                const juce::int64 rightSourceSample = (juce::int64)std::llround(rightSourceSec * sourceSampleRateHz);

                const float a = readSampleFromTakeSlot(leftState.takeSlot, leftSourceSample);
                const float b = readSampleFromTakeSlot(rightState.takeSlot, rightSourceSample);

                const double u = juce::jlimit(0.0, 1.0, (tSec - xs) / (xe - xs));

                float wA = 1.0f, wB = 0.0f;

                if (curve == XFadeCurve::EqualPower)
                {
                    const double theta = u * juce::MathConstants<double>::halfPi;
                    wA = (float)std::cos(theta);
                    wB = (float)std::sin(theta);
                }
                else
                {
                    wA = (float)(1.0 - u);
                    wB = (float)(u);
                }

                sampleOut = a * wA + b * wB;
                return true;
            };

        // Try boundary between segIdx-1 and segIdx first (can bleed into segIdx start)
        if (!doMix(segIdx - 1))
        {
            // Then boundary between segIdx and segIdx+1 (can bleed into segIdx end)
            doMix(segIdx);
        }

        for (int ch = 0; ch < out->getNumChannels(); ++ch)
            out->setSample(ch, start + i, sampleOut);
    }

    positionSamples += num;
}

float CompedAuditionSource::readSampleAtTime(int takeIndex, double tSec) const noexcept
{
    auto it = takeIndexToSlot.find(takeIndex);
    if (it == takeIndexToSlot.end())
        return 0.0f; 

    const int takeSlot = it->second;
    const juce::int64 sampleIndex = (juce::int64)std::llround(tSec * sourceSampleRateHz);
    return readSampleFromTakeSlot(takeSlot, sampleIndex);
}
