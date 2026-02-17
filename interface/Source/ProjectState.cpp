// ProjectState.cpp
#include "ProjectState.h"
#include "AppPaths.h"

//==============================================================================
// Helpers
//==============================================================================

static juce::File normaliseProjectRoot(const juce::File& candidate)
{
    if (candidate.isDirectory())
        return candidate;

    auto root = AppPaths::root();
    if (root.isDirectory())
        return root;

    auto cwd = juce::File::getCurrentWorkingDirectory();
    if (cwd.isDirectory())
        return cwd;

    return candidate;
}

static juce::String resolvePath(const juce::String& p, const juce::File& projectRoot)
{
    if (p.isEmpty())
        return {};

    const auto resolvedRoot = normaliseProjectRoot(projectRoot);

#if JUCE_WINDOWS
    // If someone saved "/data_pilot/..." or "\data_pilot\..." treat it as RELATIVE to projectRoot,
    // not as "C:\data_pilot".
    const bool looksLikeDriveRootRelative =
        (p.startsWithChar('/') || p.startsWithChar('\\')) && !p.startsWith("\\\\"); // keep UNC absolute

    if (looksLikeDriveRootRelative)
    {
        juce::String rel = p.trimCharactersAtStart("/\\");
        return resolvedRoot.getChildFile(rel).getFullPathName();
    }
#endif

    if (juce::File::isAbsolutePath(p))
        return juce::File(p).getFullPathName();

    return resolvedRoot.getChildFile(p).getFullPathName();
}



static juce::String makeRelativePath(const juce::String& p, const juce::File& projectRoot)
{
    if (p.isEmpty()) return {};

    const auto resolvedRoot = normaliseProjectRoot(projectRoot);

    if (!juce::File::isAbsolutePath(p))
        return p;

    juce::File f(p);

    // Only store relative if it is inside the app root
    if (f.isAChildOf(resolvedRoot))
        return f.getRelativePathFrom(resolvedRoot);

    // Otherwise keep absolute (e.g. user picked an instrumental somewhere else)
    return f.getFullPathName();
}

//==============================================================================
// CompSegmentState
//==============================================================================

juce::var CompSegmentState::toVar() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("startSec", startSec);
    obj->setProperty("endSec", endSec);
    obj->setProperty("sourceOffsetSec", sourceOffsetSec);
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
        if (obj->hasProperty("sourceOffsetSec")) s.sourceOffsetSec = (double)obj->getProperty("sourceOffsetSec");
        if (obj->hasProperty("takeIndex"))  s.takeIndex = (int)obj->getProperty("takeIndex");
    }

    return s;
}

//==============================================================================
// CompBoundaryState
//==============================================================================

juce::var CompBoundaryState::toVar() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("leftSegIndex", leftSegIndex);
    obj->setProperty("xfadeStartSec", xfadeStartSec);
    obj->setProperty("xfadeEndSec", xfadeEndSec);
    return obj;
}

CompBoundaryState CompBoundaryState::fromVar(const juce::var& v)
{
    CompBoundaryState b;
    if (auto* obj = v.getDynamicObject())
    {
        if (obj->hasProperty("leftSegIndex"))  b.leftSegIndex = (int)obj->getProperty("leftSegIndex");
        if (obj->hasProperty("xfadeStartSec")) b.xfadeStartSec = (double)obj->getProperty("xfadeStartSec");
        if (obj->hasProperty("xfadeEndSec"))   b.xfadeEndSec = (double)obj->getProperty("xfadeEndSec");
    }
    return b;
}

//==============================================================================
// CompResultState
//==============================================================================

juce::var CompResultState::toVar() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("hasResult", hasResult);
    obj->setProperty("compedFilePath", compedFilePath);
    obj->setProperty("compmapFilePath", compmapFilePath);
    obj->setProperty("alphaPct", alphaPct);
    obj->setProperty("crossfadePct", crossfadePct);
    obj->setProperty("fadeFraction", fadeFraction);
    obj->setProperty("selected", selected);
    obj->setProperty("solo", solo);

    juce::Array<juce::var> segs;
    for (const auto& s : segments)
        segs.add(s.toVar());
    obj->setProperty("segments", segs);

    juce::Array<juce::var> bounds;
    for (const auto& b : boundaries)
        bounds.add(b.toVar());
    obj->setProperty("boundaries", bounds);

    return obj;
}

CompResultState CompResultState::fromVar(const juce::var& v)
{
    CompResultState r;
    if (auto* obj = v.getDynamicObject())
    {
        r.hasResult = (bool)obj->getProperty("hasResult");
        r.compedFilePath = obj->getProperty("compedFilePath").toString();
        r.compmapFilePath = obj->getProperty("compmapFilePath").toString();
        r.alphaPct = (int)obj->getProperty("alphaPct");
        r.crossfadePct = (int)obj->getProperty("crossfadePct");
        r.fadeFraction = (double)obj->getProperty("fadeFraction");
        r.selected = (bool)obj->getProperty("selected");
        r.solo = (bool)obj->getProperty("solo");

        auto segsVar = obj->getProperty("segments");
        if (segsVar.isArray())
            if (auto* arr = segsVar.getArray())
                for (const auto& item : *arr)
                    r.segments.add(CompSegmentState::fromVar(item));

        auto boundsVar = obj->getProperty("boundaries");
        if (boundsVar.isArray())
            if (auto* arr = boundsVar.getArray())
                for (const auto& item : *arr)
                    r.boundaries.add(CompBoundaryState::fromVar(item));
    }
    return r;
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
    root->setProperty("selectedKeyMode", selectedKeyMode);
    root->setProperty("selectedKeyRoot", selectedKeyRoot);

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

    root->setProperty("gridOffsetSec", gridOffsetSec);

    juce::Array<juce::var> bounds;
    for (const auto& b : compBoundaries)
        bounds.add(b.toVar());
    root->setProperty("compBoundaries", bounds);

    juce::Array<juce::var> compRows;
    for (const auto& c : compResults)
        compRows.add(c.toVar());
    root->setProperty("compResults", compRows);
    root->setProperty("activeCompResultIndex", activeCompResultIndex);

    return root;
}

ProjectState ProjectState::fromVar(const juce::var& v, const juce::File& projectRoot)
{
    ProjectState s;

    if (auto* root = v.getDynamicObject())
    {
        if (root->hasProperty("version"))            s.version = (int)root->getProperty("version");

        s.instrumentalPath = resolvePath(root->getProperty("instrumentalPath").toString(), projectRoot);
        s.loopStartSec = (double)root->getProperty("loopStartSec");
        s.loopEndSec = (double)root->getProperty("loopEndSec");
        s.loopLocked = (bool)root->getProperty("loopLocked");
        s.cachedLoopLengthSec = (double)root->getProperty("cachedLoopLengthSec");

        s.bpm = (int)root->getProperty("bpm");
        s.bpmSet = (bool)root->getProperty("bpmSet");
        s.metronomeOn = (bool)root->getProperty("metronomeOn");
        s.selectedKeyMode = root->getProperty("selectedKeyMode").toString();
        s.selectedKeyRoot = root->getProperty("selectedKeyRoot").toString();

        s.currentPhraseIndex = (int)root->getProperty("currentPhraseIndex");
        s.currentPhraseDirectory = resolvePath(root->getProperty("currentPhraseDirectory").toString(), projectRoot);

        s.fullRecordingIndex = (int)root->getProperty("fullRecordingIndex");
        s.nextTakeIndex = (int)root->getProperty("nextTakeIndex");

        s.selectedTakeIndex = (int)root->getProperty("selectedTakeIndex");
        s.soloTakeIndex = (int)root->getProperty("soloTakeIndex");
        s.takeVolume = (double)root->getProperty("takeVolume");

        s.hasLastCompResult = (bool)root->getProperty("hasLastCompResult");
        s.lastCompedFilePath = resolvePath(root->getProperty("lastCompedFilePath").toString(), projectRoot);
        s.lastCompmapFilePath = resolvePath(root->getProperty("lastCompmapFilePath").toString(), projectRoot);
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

        s.gridOffsetSec = (double)root->getProperty("gridOffsetSec");

        auto boundsVar = root->getProperty("compBoundaries");
        if (boundsVar.isArray())
        {
            if (auto* arr = boundsVar.getArray())
            {
                for (const auto& item : *arr)
                    s.compBoundaries.add(CompBoundaryState::fromVar(item));
            }
        }

        auto compResultsVar = root->getProperty("compResults");
        if (compResultsVar.isArray())
        {
            if (auto* arr = compResultsVar.getArray())
            {
                for (const auto& item : *arr)
                {
                    auto r = CompResultState::fromVar(item);
                    r.compedFilePath = resolvePath(r.compedFilePath, projectRoot);
                    r.compmapFilePath = resolvePath(r.compmapFilePath, projectRoot);
                    s.compResults.add(r);
                }
            }
        }
        s.activeCompResultIndex = (int)root->getProperty("activeCompResultIndex");

        // Migrate legacy v1 single result into v2 compResults if needed.
        if (s.compResults.isEmpty() && s.hasLastCompResult)
        {
            CompResultState legacy;
            legacy.hasResult = s.hasLastCompResult;
            legacy.compedFilePath = s.lastCompedFilePath;
            legacy.compmapFilePath = s.lastCompmapFilePath;
            legacy.alphaPct = s.lastCompAlphaPct;
            legacy.crossfadePct = s.lastCompCrossfadePct;
            legacy.fadeFraction = s.lastCompFadeFraction;
            legacy.selected = s.compedSelected;
            legacy.solo = s.compedSolo;
            legacy.segments = s.compSegments;
            legacy.boundaries = s.compBoundaries;
            s.compResults.add(legacy);
            s.activeCompResultIndex = 0;
        }
    }

    return s;
}

ProjectState ProjectState::fromVar(const juce::var& v)
{
    juce::File projectRoot = normaliseProjectRoot(AppPaths::root());
    return fromVar(v, projectRoot);
}

bool ProjectState::saveToFile(const ProjectState& state,
    const juce::File& file,
    juce::String& errorMessage)
{
    errorMessage.clear();

    juce::File projectRoot = normaliseProjectRoot(AppPaths::root());

    // Make a temporary copy with relative paths
    ProjectState tmp = state;
    tmp.instrumentalPath      = makeRelativePath(state.instrumentalPath, projectRoot);
    tmp.currentPhraseDirectory = makeRelativePath(state.currentPhraseDirectory, projectRoot);
    tmp.lastCompedFilePath    = makeRelativePath(state.lastCompedFilePath, projectRoot);
    tmp.lastCompmapFilePath   = makeRelativePath(state.lastCompmapFilePath, projectRoot);
    for (auto& r : tmp.compResults)
    {
        r.compedFilePath = makeRelativePath(r.compedFilePath, projectRoot);
        r.compmapFilePath = makeRelativePath(r.compmapFilePath, projectRoot);
    }

    juce::var root = tmp.toVar();
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

    juce::File projectRoot = normaliseProjectRoot(AppPaths::root());
    state = ProjectState::fromVar(parsed, projectRoot);
    return true;
}
