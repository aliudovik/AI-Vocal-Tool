#pragma once
#include <JuceHeader.h>

#include <atomic>
#include <unordered_map>
#include <vector>

// Realtime audition source that renders the comp on-the-fly from take_*.wav buffers,
// using segment winners + boundary crossfade windows.
class CompedAuditionSource : public juce::PositionableAudioSource
{
public:
    struct SegmentInfo
    {
        double startSec = 0.0;
        double endSec = 0.0;
        double sourceOffsetSec = 0.0; // sourceTime = timelineTime + offset
        int    takeIndex = -1;   // e.g. 3 for take_3.wav
    };

    struct BoundaryInfo
    {
        int    leftSegIndex = -1;     // boundary b is between seg b and b+1
        double xfadeStartSec = 0.0;
        double xfadeEndSec = 0.0;
    };

    enum class XFadeCurve { Linear, EqualPower };

    CompedAuditionSource() = default;
    ~CompedAuditionSource() override = default;

    // Loads ALL take files (take_*.wav) into memory, resampled to targetSampleRate if needed.
    // Must be called on message thread (not audio thread).
    bool loadTakesIntoMemory(juce::AudioFormatManager& fm,
        const juce::Array<juce::File>& takeFiles,
        double targetSampleRate,
        juce::String& outError);

    // Sets comp structure (segments + boundaries). Must be called on message thread.
    void setCompData(const std::vector<SegmentInfo>& newSegments,
        const std::vector<BoundaryInfo>& newBoundaries);

    void setXFadeCurve(XFadeCurve c) noexcept { curve = c; }

    // Real-time safe (audio-thread safe): updates one boundary window live.
    void setBoundaryWindow(int boundaryIndex, double xfadeStartSec, double xfadeEndSec) noexcept;

    // PositionableAudioSource
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    
    // --- NEW: allow UI renderer to access samples ---
    double getSampleRateHz() const noexcept { return sourceSampleRateHz; }

    // Read a mono sample from a given take at absolute time
    float readSampleAtTime(int takeIndex, double tSec) const noexcept;

    void setNextReadPosition(juce::int64 newPosition) override;
    juce::int64 getNextReadPosition() const override;
    juce::int64 getTotalLength() const override;
    bool isLooping() const override;
    void setLooping(bool shouldLoop) override;

private:
    struct TakeBuffer
    {
        int takeIndex = -1;
        juce::AudioBuffer<float> mono; // 1ch, aligned timeline
    };

    struct SegmentState
    {
        double startSec = 0.0;
        double endSec = 0.0;
        double sourceOffsetSec = 0.0;
        int    takeSlot = -1;    // index into takes[]
    };

    struct BoundaryState
    {
        int leftSegIndex = -1;
        std::atomic<double> xfadeStartSec{ 0.0 };
        std::atomic<double> xfadeEndSec{ 0.0 };

        BoundaryState() = default;

        BoundaryState(const BoundaryState&) = delete;
        BoundaryState& operator=(const BoundaryState&) = delete;

        BoundaryState(BoundaryState&& other) noexcept
            : leftSegIndex(other.leftSegIndex),
            xfadeStartSec(other.xfadeStartSec.load()),
            xfadeEndSec(other.xfadeEndSec.load())
        {
        }

        BoundaryState& operator=(BoundaryState&& other) noexcept
        {
            leftSegIndex = other.leftSegIndex;
            xfadeStartSec.store(other.xfadeStartSec.load());
            xfadeEndSec.store(other.xfadeEndSec.load());
            return *this;
        }
    };
    
    // Boundaries sanitization
    juce::SpinLock compDataLock;

    // Loaded takes
    std::vector<TakeBuffer> takes;
    std::unordered_map<int, int> takeIndexToSlot;

    // Comp structure (segments immutable-ish, boundaries atomically updatable)
    std::vector<SegmentState> segments;
    std::vector<BoundaryState> boundaries; // size = segments.size()-1

    double sourceSampleRateHz = 44100.0;   // target sample rate we resample all takes to
    double totalLengthSeconds = 0.0;
    juce::int64 totalLengthSamples = 0;

    juce::int64 positionSamples = 0;       // read pos in source samples
    bool looping = true;
    XFadeCurve curve = XFadeCurve::Linear;

    int cachedSegIndex = 0;

private:
    static int parseTakeIndexFromFilename(const juce::File& f);
    static juce::AudioBuffer<float> downmixToMono(const juce::AudioBuffer<float>& in);
    static juce::AudioBuffer<float> resampleMono(const juce::AudioBuffer<float>& inMono,
        double inRate,
        double outRate);

    int findSegmentIndexForTime(double tSec) const noexcept;
    double mapTimelineToSourceTimeSec(const SegmentState& seg, double timelineSec) const noexcept;
    inline float readSampleFromTakeSlot(int takeSlot, juce::int64 sampleIndex) const noexcept;
};
