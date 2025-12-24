#pragma once

#include <JuceHeader.h>
#include "ProjectState.h"
#include "NeonUI.h"


// Main component:
// - Load instrumental
// - Loop playback between loopStart/loopEnd
// - Draw waveform with playhead and loop region
// - BPM display + metronome toggle + vertical-drag BPM control

class CompingProgressComponent : public juce::Component
{
public:
    explicit CompingProgressComponent(NeonLookAndFeel& lf);
    ~CompingProgressComponent() override;

    NeonProgressBar& getProgressBar() noexcept;

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

    // Timer (for moving playhead & handling loop wrap)
    void timerCallback() override;

    // ChangeListener (for thumbnail finished/updated)
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Mouse for loop handles & BPM dragging
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;

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

    // Comping progress pop-up
    CompingProgressComponent* compingProgressComponent = nullptr;
    juce::DialogWindow* compingDialogWindow = nullptr;

    void onCompingFinished(bool success);

    // Layout areas for track label + waveform + bpm
    juce::Rectangle<int> instrumentalLabelBounds;
    juce::Rectangle<int> instrumentalWaveformBounds;
    juce::Rectangle<int> bpmBounds;
    juce::Rectangle<int> takesAreaBounds;
    juce::Rectangle<int> compExportArea;

    // === Audio / thumbnail ===
    juce::AudioFormatManager  formatManager;
    juce::AudioThumbnailCache thumbnailCache{ 10 };
    juce::AudioThumbnail      thumbnail{ 512, formatManager, thumbnailCache };

    // Comped review state  // NEW
    juce::AudioThumbnail compedThumbnail{ 512, formatManager, thumbnailCache }; // NEW
    bool hasCompedThumbnail = false;                                             // NEW

    struct CompSegment       // NEW
    {                        // NEW
        double startSec = 0.0;  // NEW
        double endSec = 0.0;  // NEW
        int    takeIndex = -1;  // NEW  e.g. 3 for "take_3" // NEW
    };                       // NEW
    juce::Array<CompSegment> compSegments;   // NEW

    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transportSource;
    juce::File currentInstrumentalFile;

    // Recording writer for full_N.wav
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> recordingWriter;
    juce::CriticalSection writerLock;
    double currentSampleRate = 44100.0;
    juce::AudioSampleBuffer recordingInputBuffer;

    // Last comping result (for the Comped tab later)
        // Last comping result (for the Comped tab later)
    juce::File lastCompedFile;
    juce::File lastCompmapFile;
    int        lastCompAlphaPct = 0;       // 0..100 (Accuracy)
    int        lastCompCrossfadePct = 0;   // 0..100 (Crossfade slider value)
    double     lastCompFadeFraction = 0.0;
    bool       hasLastCompResult = false;

    // State for the single comped row in the CompReview view
    bool       compedSelected = true;      // play with instrumental
    bool       compedSolo = false;     // play comped only



    // Loop selection in seconds (Ableton-style arrangement loop)
    double loopStartSec = 0.0;
    double loopEndSec = 0.0;
    double minLoopLengthSec = 5.0;  // minimum loop length

    enum class DragMode { none, leftHandle, rightHandle, bpmAdjust };
    DragMode dragMode = DragMode::none;

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
    void getCompRowLayout(juce::Rectangle<int>& row,
        juce::Rectangle<int>& labelRect,
        juce::Rectangle<int>& waveRect,
        juce::Rectangle<int>& controlsRect) const;



    // BPM / metronome state
    int  bpm = 120;
    bool bpmSet = false;
    bool metronomeOn = false;

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
    NeonButton compedSelectButton{ "Select" };
    NeonButton compedSoloButton{ "Solo" };


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




    bool hasValidLoop() const noexcept
    {
        return thumbnail.getTotalLength() > 0.0
            && loopEndSec > loopStartSec + 0.0001;
    }

    // Prompt for BPM after importing
    void promptForBpm();

    // Update BPM label text
    void refreshBpmLabel();

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
