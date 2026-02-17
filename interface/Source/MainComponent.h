#pragma once

#include <JuceHeader.h>
#include "ProjectState.h"
#include "NeonUI.h"
#include <array>


// Main component:
// - Load instrumental
// - Loop playback between loopStart/loopEnd
// - Draw waveform with playhead and loop region
// - BPM display + metronome toggle + vertical-drag BPM control

class CompedAuditionSource;

class CompingProgressComponent : public juce::Component
{
public:
    explicit CompingProgressComponent(NeonLookAndFeel& lf);
    ~CompingProgressComponent() override;

    NeonProgressBar& getProgressBar() noexcept;
    
    void startCompingProgress(const juce::File& features,
                              const juce::File& segments,
                              const juce::File& compmap,
                              const juce::File& comped);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    NeonLookAndFeel& lookAndFeel;
    juce::Label      titleLabel;
    NeonProgressBar  progressBar;
};

class MainComponent : public juce::AudioAppComponent,
    public juce::Button::Listener,
    public juce::Timer,
    public juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    //==============================================================================
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    //==============================================================================
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Button::Listener
    void buttonClicked(juce::Button* button) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Timer (for moving playhead & handling loop wrap)
    void timerCallback() override;

    // ChangeListener (for thumbnail finished/updated)
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Mouse for loop handles & BPM dragging
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;
    
    void promptSaveOnExit();
    
    void restartPlaybackForCompEdit();
    
    void restartCompPlaybackIfPlaying();
    
    void showWelcomePopup();
    void loadExampleProject();
    
    void launchMLServer();
    
    bool quitAfterSave = false;
    
    

private:
    // === UI ===
    enum class ViewMode { Recording, CompReview };
    ViewMode viewMode = ViewMode::Recording;

    NeonLookAndFeel neonLookAndFeel;

    juce::TextButton recordingTabButton{ "Recording" };
    juce::TextButton compedTabButton{ "Comped" };
    juce::TextButton importButton{ "IMPORT" };
    juce::TextButton playButton{ "PLAY" };
    juce::TextButton stopButton{ "STOP" };
    juce::TextButton resetButton{ "Start over" };
    juce::TextButton recordButton{ "REC" };
    juce::TextButton ioButton{ "IN/OUT" };  
    juce::TextButton saveProjectButton{ "Save Project" };
    juce::TextButton loadProjectButton{ "Load Project" };

    juce::Label        bpmLabel;
    juce::Label        keyLabel;
    juce::ToggleButton metronomeToggle{ "Metronome" };
    juce::Label        takeVolumeLabel;
    juce::Slider       takeVolumeSlider;

    juce::Label        accuracyEmotionLabel;
    AccuracyEmotionSlider accuracyEmotionSlider;  

    juce::Label        crossfadeLabel;
    CrossfadeKnob      crossfadeSlider;         
    juce::Label  styleLeftLabel;     
    juce::Label  styleRightLabel;   
    juce::Label  crossfadeLeftLabel; 
    juce::Label  crossfadeRightLabel;



    juce::TextButton compingButton{ "COMPING" };
    juce::TextButton   exportCompedButton{ "EXPORT SELECTED" };
    juce::ToggleButton mlModeToggle; // ml support

    // Comping progress pop-up
    CompingProgressComponent* compingProgressComponent = nullptr;
    juce::DialogWindow* compingDialogWindow = nullptr;

    bool getCompWaveAreaForInteraction(juce::Rectangle<int>& outCompWaveArea) const;
    double compedXToTime(int x, const juce::Rectangle<int>& compWaveArea) const;


    void onCompingFinished(bool success);

    // Layout areas for track label + waveform + bpm
    juce::Rectangle<int> instrumentalLabelBounds;
    juce::Rectangle<int> instrumentalWaveformBounds;
    juce::Rectangle<int> bpmBounds;
    juce::Rectangle<int> keyBounds;
    juce::Rectangle<int> takesAreaBounds;
    juce::Rectangle<int> compExportArea;

    // === Audio / thumbnail ===
    juce::AudioFormatManager  formatManager;
    juce::AudioThumbnailCache thumbnailCache{ 10 };
    juce::AudioThumbnail      thumbnail{ 512, formatManager, thumbnailCache };

    // Comped review state
    juce::AudioThumbnail compedThumbnail{ 512, formatManager, thumbnailCache }; // NEW
    bool hasCompedThumbnail = false;
    
    friend class CompedAuditionSource;

    struct CompSegment       // NEW
    {                        // NEW
        double startSec = 0.0;  // NEW
        double endSec = 0.0;  // NEW
        double sourceOffsetSec = 0.0;
        int    takeIndex = -1;  // NEW  e.g. 3 for "take_3" // NEW
    };                       // NEW
    juce::Array<CompSegment> compSegments;   // active comp result only

    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<CompedAuditionSource> compedAuditionSource;

    juce::AudioTransportSource transportSource;
    juce::File currentInstrumentalFile;

    // Manual crossfade editing (Comped tab)
// boundary b corresponds to between compSegments[b] and compSegments[b+1]
    struct CompBoundary
    {
        double xfadeStartSec = 0.0;
        double xfadeEndSec = 0.0;
        int    leftSegIndex = -1;
    };

    juce::Array<CompBoundary> compBoundaries; // active comp result only
    
    juce::ChildProcess mlServerProcess;

    // Cache full compmap JSON so we can edit without losing fields
    juce::var compmapJsonCache;

    // Drag state for Comped-tab crossfade handles (separate from loop handles)
    enum class CompDragMode { None, XFadeStart, XFadeEnd, SegmentBoundary, SegmentSlip };
    int activeBoundaryIndex = -1;
    int activeSegmentIndex = -1;
    CompDragMode compDragMode = CompDragMode::None;
    double segmentSlipDragStartMouseSec = 0.0;
    double segmentSlipDragStartOffsetSec = 0.0;


    // Recording writer for full_N.wav
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> recordingWriter;
    juce::CriticalSection writerLock;
    double currentSampleRate = 44100.0;
    juce::AudioSampleBuffer recordingInputBuffer;

    // === Grid Tooltip State ===
    bool gridTooltipDismissed = false; // Has user performed the action yet?
    bool isHoveringInstrumental = false;
    juce::Point<int> lastMousePosition;
    bool segmentTimingTipShownOnce = false;
    double segmentTimingTipShowUntilMs = 0.0;
    bool zoomScaleTipShownOnce = false;
    double zoomScaleTipShowUntilMs = 0.0;

    // Last/active comping result (for the Comped tab)
    juce::File lastCompedFile;
    juce::File lastCompmapFile;
    int        lastCompAlphaPct = 0;       // 0..100 (Accuracy)
    int        lastCompCrossfadePct = 0;   // 0..100 (Crossfade slider value)
    double     lastCompFadeFraction = 0.0;
    bool       hasLastCompResult = false;
    bool hasCompedAuditionSource = false;

    // State for the single comped row in the CompReview view
    bool       compedSelected = true;      // play with instrumental
    bool       compedSolo = false;         // play comped only

    struct CompResult
    {
        juce::File compedFile;
        juce::File compmapFile;
        int alphaPct = 0;
        int crossfadePct = 0;
        double fadeFraction = 0.0;

        juce::Array<CompSegment> segments;
        juce::Array<CompBoundary> boundaries;
        juce::var compmapJson;

        bool selected = true;
        bool solo = false;
    };

    juce::Array<CompResult> compResults;
    int activeCompResultIndex = -1;
    static constexpr int kMaxCompResultRows = 3;
    std::array<std::unique_ptr<juce::AudioThumbnail>, kMaxCompResultRows> compResultThumbnails;

    // Comped view helpers
    juce::Colour getColourForTake(int takeIndex) const;
    void drawCompedTopBar(juce::Graphics& g,
                          const juce::Rectangle<int>& topBar,
                          const juce::Rectangle<int>& compWaveArea,
                          int compResultIndex);

    void drawZebraStripes(juce::Graphics& g,
                          const juce::Rectangle<int>& r,
                          juce::Colour c1,
                          juce::Colour c2) const;

    void drawCompedWaveformRealtime(juce::Graphics& g,
                                    const juce::Rectangle<int>& area,
                                    int compResultIndex);

    float renderCompSampleAtTime(double tSec, int& cachedSeg, int compResultIndex) const;
    
    // MainComponent.h  (add to private section)
    bool buildCompedAuditionSourceForExport(std::unique_ptr<CompedAuditionSource>& outSource,
                                            double& outLengthSec,
                                            juce::String& outError);

    void exportCompedAuditionToFileAsync(const juce::File& targetFile);
    
    // --- Segmentation point manipulation ---
    bool getCompWaveAndTopBarForInteraction(juce::Rectangle<int>& compWaveArea,
                                            juce::Rectangle<int>& topBarRect) const;

    void clampBoundaryWindowToSegments(int boundaryIndex);
    void updateCompedAuditionSourceFromEdits();

    // For correct clamping
    void sanitizeCompSegments();
    void rebuildCompResultThumbnails();
    void syncActiveCompResultFromLegacyState();
    void syncLegacyStateFromActiveCompResult();
    bool setActiveCompResult(int index, bool prepareAudition);
    juce::String getCompResultTitle(int index) const;
    int getCompResultCount() const noexcept { return compResults.size(); }

    // Loop selection in seconds (Ableton-style arrangement loop)
    double loopStartSec = 0.0;
    double loopEndSec = 0.0;
    double minLoopLengthSec = 5.0;  // minimum loop length
    double visibleStartSec = 0.0;   // shared horizontal zoom start (project time)
    double visibleEndSec = 0.0;     // shared horizontal zoom end (project time)

    enum class DragMode { none, leftHandle, rightHandle, bpmAdjust, gridAdjust };
    DragMode dragMode = DragMode::none;

    double gridOffsetSec = 0.0;     // The time shift for the grid
    double snapToGrid(double time); // Helper function
    double gridDragLastMouseTime = 0.0;
    bool   gridDragHasLast = false;

    // === Vocal recording visual state ===
    struct TakeTrack
    {
        int startSample = 0;   // index in vocalWaveBuffer
        int numSamples = 0;   // length in samples for this take (one loop)
        juce::String name;     // "Take 1", "Take 2", ...
    };

    juce::AudioSampleBuffer vocalWaveBuffer;      // mono buffer with all recorded samples
    int totalRecordedSamples = 0;                 // how many samples we've appended so far
    int loopLengthSamples = 0;                 // cachedLoopLengthSec * currentSampleRate
    juce::Array<TakeTrack> takeTracks;            // completed loop segments
    juce::CriticalSection vocalLock;
    int  vocalBufferCapacitySamples = 0;
    juce::File currentFullRecordingFile;

    // === Take playback (selected take alongside instrumental) ===
    juce::AudioTransportSource takeTransport;
    std::unique_ptr<juce::AudioFormatReaderSource> takeReaderSource;
    juce::AudioSampleBuffer takeMixBuffer;
    int selectedTakeIndex = -1; // for take sleecion
    int soloTakeIndex = -1; // for oslo
    
    void startPlaybackForSelection(bool playInstrumental, bool playTake);
    void stopAllPlayback();
    bool isSoloPlayback() const;
    double getPlayheadPositionSec() const;
    double getEffectiveLoopStartSec() const;
    double getEffectiveLoopEndSec() const;
    double getEffectiveCompLoopStartSec() const;
    double getEffectiveCompLoopEndSec() const;
    void enforceLoopWrap();
    
    

    bool compedAuditionReady = false;

    bool ensureCompedAuditionSourceReady(); // loads take_*.wav and attaches takeTransport to audition source
    bool isCompedAuditionReady() const noexcept { return compedAuditionReady && compedAuditionSource != nullptr; }




    // --- Scrollable takes view (Recording tab) ---
    juce::Viewport takesViewport;
    juce::Component takesContainer;
    juce::OwnedArray<TakeLaneComponent> takeLaneComponents;

    // Helpers for the takes view
    void syncTakeLanesWithTakeTracks();
    void layoutTakeLanes();
    void refreshTakeLaneSelectionStates();
    void updateTakeLanePlayhead(double globalTimeSeconds);
    void refreshCompedButtons();
    void getCompRowLayout(int rowIndex,
        int rowCount,
        juce::Rectangle<int>& row,
        juce::Rectangle<int>& labelRect,
        juce::Rectangle<int>& waveRect,
        juce::Rectangle<int>& controlsRect) const;
    void getCompRowLayout(juce::Rectangle<int>& row,
        juce::Rectangle<int>& labelRect,
        juce::Rectangle<int>& waveRect,
        juce::Rectangle<int>& controlsRect) const;

    void drawBeatGrid(juce::Graphics& g,
        const juce::Rectangle<int>& innerBounds,
        double totalLengthSec) const;

    void rebuildCompedFromEditedCompmapAsync();




    // BPM / metronome state
    int  bpm = 120;
    bool bpmSet = false;
    bool metronomeOn = false;
    juce::String selectedKeyMode = "chromatic"; // major/minor/chromatic
    juce::String selectedKeyRoot = "C";

    // For vertical-drag BPM adjust
    int bpmDragStartY = 0;
    int bpmDragStartValue = 120;

    // Recording / loop lock state
    bool   isRecording = false;
    bool   loopLocked = false;
    int    fullRecordingIndex = 0;   // full_1, full_2, ...
    int    nextTakeIndex = 1;   // take_1, take_2, ...
    double cachedLoopLengthSec = 0.0; // first record length

    juce::File currentPhraseDirectory;
    int        currentPhraseIndex = 1;

    // --- Comped-tab lane controls ---
    juce::OwnedArray<NeonButton> compedSelectButtons;
    juce::OwnedArray<NeonButton> compedSoloButtons;


    // Async file chooser
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Helpers
    double xToTime(float x) const;
    int    timeToX(double t) const;
    int    compedTimeToX(double t, const juce::Rectangle<int>& area) const;

    void splitFullRecordingIntoTakes(const juce::File& fullFile, int numLoops);
    void setSelectedTake(int newIndex);
    void setSoloTake(int newIndex);
    void stopRecording();                       // NEW
    void importInstrumental();                  // extracted from old button handler
    void importTakesFromFiles();
    void initialiseUserPhraseDirectory();
    void runCompingFromGui();
    void resetProjectState();
    void launchProjectLoadChooser();
    bool loadCompedFile(const juce::File& compedFile);
    bool loadLastCompForReview();
    bool prepareCompedAuditionSource();




    bool hasValidLoop() const noexcept
    {
        return thumbnail.getTotalLength() > 0.0
            && loopEndSec > loopStartSec + 0.0001;
    }

    // Prompt for BPM after importing
    void promptForBpm();
    void promptForKeySelection();

    // Update BPM label text
    void refreshBpmLabel();
    void refreshKeyLabel();

    // View-specific painting/layout helpers
    void paintRecordingView(juce::Graphics& g);
    void paintCompReviewView(juce::Graphics& g);

    void layoutRecordingView(juce::Rectangle<int> area);
    void layoutCompReviewView(juce::Rectangle<int> area);

    // Update tab button colours / enabled state
    void updateTabButtonStyles();

    // --- Project state helpers (save/load) ---
    ProjectState createProjectState() const;
    void applyProjectState(const ProjectState& state);
    void saveProjectToFile();
    void loadProjectFromFile();

    // Rebuild visual takes (vocalWaveBuffer + takeTracks) from take_*.wav files
    void rebuildTakesFromPhraseDirectory();



    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
