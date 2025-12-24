// ProjectState.cpp
#include "ProjectState.h"

//==============================================================================
// CompSegmentState
//==============================================================================

juce::var CompSegmentState::toVar() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("startSec", startSec);
    obj->setProperty("endSec", endSec);
    obj->setProperty("takeIndex", takeIndex);
    return obj;
}

CompSegmentState CompSegmentState::fromVar(const juce::var& v)
{
    CompSegmentState s;

    if (auto* obj = v.getDynamicObject())
    {
        if (obj->hasProperty("startSec"))   s.startSec = (double)obj->getProperty("startSec");
        if (obj->hasProperty("endSec"))     s.endSec = (double)obj->getProperty("endSec");
        if (obj->hasProperty("takeIndex"))  s.takeIndex = (int)obj->getProperty("takeIndex");
    }

    return s;
}

//==============================================================================
// ProjectState
//==============================================================================

juce::var ProjectState::toVar() const
{
    auto* root = new juce::DynamicObject();

    root->setProperty("version", version);

    // Instrumental / loop
    root->setProperty("instrumentalPath", instrumentalPath);
    root->setProperty("loopStartSec", loopStartSec);
    root->setProperty("loopEndSec", loopEndSec);
    root->setProperty("loopLocked", loopLocked);
    root->setProperty("cachedLoopLengthSec", cachedLoopLengthSec);

    // Tempo
    root->setProperty("bpm", bpm);
    root->setProperty("bpmSet", bpmSet);
    root->setProperty("metronomeOn", metronomeOn);

    // Phrase info
    root->setProperty("currentPhraseIndex", currentPhraseIndex);
    root->setProperty("currentPhraseDirectory", currentPhraseDirectory);

    // Recording indices
    root->setProperty("fullRecordingIndex", fullRecordingIndex);
    root->setProperty("nextTakeIndex", nextTakeIndex);

    // Selection / volume
    root->setProperty("selectedTakeIndex", selectedTakeIndex);
    root->setProperty("soloTakeIndex", soloTakeIndex);
    root->setProperty("takeVolume", takeVolume);

    // Comped
    root->setProperty("hasLastCompResult", hasLastCompResult);
    root->setProperty("lastCompedFilePath", lastCompedFilePath);
    root->setProperty("lastCompmapFilePath", lastCompmapFilePath);
    root->setProperty("lastCompAlphaPct", lastCompAlphaPct);
    root->setProperty("lastCompCrossfadePct", lastCompCrossfadePct);
    root->setProperty("lastCompFadeFraction", lastCompFadeFraction);

    root->setProperty("compedSelected", compedSelected);
    root->setProperty("compedSolo", compedSolo);

    root->setProperty("viewIsCompReview", viewIsCompReview);

    // Segments
    juce::Array<juce::var> segs;
    for (const auto& s : compSegments)
        segs.add(s.toVar());
    root->setProperty("compSegments", segs);

    return root;
}

ProjectState ProjectState::fromVar(const juce::var& v)
{
    ProjectState s;

    if (auto* root = v.getDynamicObject())
    {
        if (root->hasProperty("version"))            s.version = (int)root->getProperty("version");

        s.instrumentalPath = root->getProperty("instrumentalPath").toString();
        s.loopStartSec = (double)root->getProperty("loopStartSec");
        s.loopEndSec = (double)root->getProperty("loopEndSec");
        s.loopLocked = (bool)root->getProperty("loopLocked");
        s.cachedLoopLengthSec = (double)root->getProperty("cachedLoopLengthSec");

        s.bpm = (int)root->getProperty("bpm");
        s.bpmSet = (bool)root->getProperty("bpmSet");
        s.metronomeOn = (bool)root->getProperty("metronomeOn");

        s.currentPhraseIndex = (int)root->getProperty("currentPhraseIndex");
        s.currentPhraseDirectory = root->getProperty("currentPhraseDirectory").toString();

        s.fullRecordingIndex = (int)root->getProperty("fullRecordingIndex");
        s.nextTakeIndex = (int)root->getProperty("nextTakeIndex");

        s.selectedTakeIndex = (int)root->getProperty("selectedTakeIndex");
        s.soloTakeIndex = (int)root->getProperty("soloTakeIndex");
        s.takeVolume = (double)root->getProperty("takeVolume");

        s.hasLastCompResult = (bool)root->getProperty("hasLastCompResult");
        s.lastCompedFilePath = root->getProperty("lastCompedFilePath").toString();
        s.lastCompmapFilePath = root->getProperty("lastCompmapFilePath").toString();
        s.lastCompAlphaPct = (int)root->getProperty("lastCompAlphaPct");
        s.lastCompCrossfadePct = (int)root->getProperty("lastCompCrossfadePct");
        s.lastCompFadeFraction = (double)root->getProperty("lastCompFadeFraction");

        s.compedSelected = (bool)root->getProperty("compedSelected");
        s.compedSolo = (bool)root->getProperty("compedSolo");
        s.viewIsCompReview = (bool)root->getProperty("viewIsCompReview");

        auto segsVar = root->getProperty("compSegments");
        if (segsVar.isArray())
        {
            if (auto* arr = segsVar.getArray())
            {
                for (const auto& item : *arr)
                    s.compSegments.add(CompSegmentState::fromVar(item));
            }
        }
    }

    return s;
}

bool ProjectState::saveToFile(const ProjectState& state,
    const juce::File& file,
    juce::String& errorMessage)
{
    errorMessage.clear();

    juce::var root = state.toVar();
    const auto json = juce::JSON::toString(root, true);

    std::unique_ptr<juce::FileOutputStream> out(file.createOutputStream());
    if (out == nullptr || !out->openedOk())
    {
        errorMessage = "Could not open file for writing: " + file.getFullPathName();
        return false;
    }

    out->setPosition(0);
    out->truncate();
    out->writeText(json, false, false, "\n");
    out->flush();

    return true;
}

bool ProjectState::loadFromFile(ProjectState& state,
    const juce::File& file,
    juce::String& errorMessage)
{
    errorMessage.clear();

    if (!file.existsAsFile())
    {
        errorMessage = "File does not exist: " + file.getFullPathName();
        return false;
    }

    juce::FileInputStream in(file);
    if (!in.openedOk())
    {
        errorMessage = "Could not open file for reading: " + file.getFullPathName();
        return false;
    }

    const auto text = in.readEntireStreamAsString();
    auto parsed = juce::JSON::parse(text);

    if (parsed.isVoid())
    {
        errorMessage = "Invalid JSON in file: " + file.getFullPathName();
        return false;
    }

    state = ProjectState::fromVar(parsed);
    return true;
}
