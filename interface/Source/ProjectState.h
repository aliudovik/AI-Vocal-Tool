#pragma once

#include <JuceHeader.h>

// Lightweight serialisable copy of CompSegment from MainComponent
struct CompSegmentState
{
    double startSec = 0.0;
    double endSec = 0.0;
    int    takeIndex = -1; // 1-based (same as JSON "take_3" -> 3)

    juce::var toVar() const;
    static CompSegmentState fromVar(const juce::var& v);
};

// Full project state that can be saved/loaded as JSON
struct ProjectState
{
    int version = 1;

    // Instrumental & loop
    juce::String instrumentalPath;
    double loopStartSec = 0.0;
    double loopEndSec = 0.0;
    bool   loopLocked = false;
    double cachedLoopLengthSec = 0.0;

    // Tempo / metronome
    int  bpm = 120;
    bool bpmSet = false;
    bool metronomeOn = false;

    // Phrase folder
    int         currentPhraseIndex = 1;
    juce::String currentPhraseDirectory;   // absolute path

    // Recording indices / naming
    int fullRecordingIndex = 0;  // last full_N.wav index
    int nextTakeIndex = 1;  // next take_N.wav index

    // Take selection / volume
    int    selectedTakeIndex = -1;
    int    soloTakeIndex = -1;
    double takeVolume = 1.0; // same units as slider (0..1.5)

    // Comped state
    bool         hasLastCompResult = false;
    juce::String lastCompedFilePath;     // absolute path
    juce::String lastCompmapFilePath;    // absolute path
    int          lastCompAlphaPct = 0;
    int          lastCompCrossfadePct = 0;
    double       lastCompFadeFraction = 0.0;

    bool compedSelected = true;
    bool compedSolo = false;

    // Which tab was active
    bool viewIsCompReview = false;

    // Optional cached segments (in addition to compmap JSON)
    juce::Array<CompSegmentState> compSegments;

    // Serialisation helpers
    juce::var      toVar() const;
    static ProjectState fromVar(const juce::var& v);

    static bool saveToFile(const ProjectState& state,
        const juce::File& file,
        juce::String& errorMessage);

    static bool loadFromFile(ProjectState& state,
        const juce::File& file,
        juce::String& errorMessage);
};
