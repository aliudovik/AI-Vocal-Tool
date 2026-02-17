// MainComponent.cpp
#include "MainComponent.h"
#include "CompedAuditionSource.h"
#include "AppPaths.h"
#if JUCE_WINDOWS
#include <Windows.h>
#endif




//==============================================================================
// CompingProgressComponent: content for the neon pop-up window
//==============================================================================

//class CompingProgressComponent : public juce::Component
//{
//public:
//    explicit CompingProgressComponent(NeonLookAndFeel& lf)
//        : lookAndFeel(lf)
//    {
//        setLookAndFeel(&lookAndFeel);
//
//        addAndMakeVisible(titleLabel);
//        addAndMakeVisible(progressBar);
//
//        titleLabel.setText("AI Comping in progress…", juce::dontSendNotification);
//        titleLabel.setJustificationType(juce::Justification::centred);
//        titleLabel.setInterceptsMouseClicks(false, false);
//
//        progressBar.startComping();  // start animation immediately
//
//        setSize(380, 140);
//    }
//
//    ~CompingProgressComponent() override
//    {
//        setLookAndFeel(nullptr);
//    }
//
//    NeonProgressBar& getProgressBar() noexcept { return progressBar; }
//
//    void paint(juce::Graphics& g) override
//    {
//        auto* neonLF = dynamic_cast<NeonLookAndFeel*>(&getLookAndFeel());
//        const NeonTheme* t = neonLF ? &neonLF->getTheme() : nullptr;
//
//        auto bg = t ? t->background : juce::Colours::black;
//        g.fillAll(bg);
//    }
//
//    void resized() override
//    {
//        auto area = getLocalBounds().reduced(12);
//
//        auto titleArea = area.removeFromTop(32);
//        titleLabel.setBounds(titleArea);
//
//        progressBar.setBounds(area);
//    }
//
//private:
//    NeonLookAndFeel& lookAndFeel;
//    juce::Label      titleLabel;
//    NeonProgressBar  progressBar;
//};

//==============================================================================
// CompingProgressComponent implementation
//==============================================================================

CompingProgressComponent::CompingProgressComponent(NeonLookAndFeel& lf)
    : lookAndFeel(lf)
{
    setLookAndFeel(&lookAndFeel);

    addAndMakeVisible(titleLabel);
    addAndMakeVisible(progressBar);

    titleLabel.setText("AI Comping in progress", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setInterceptsMouseClicks(false, false);
    
    


    setSize(380, 140);
}

CompingProgressComponent::~CompingProgressComponent()
{
    setLookAndFeel(nullptr);
}

NeonProgressBar& CompingProgressComponent::getProgressBar() noexcept
{
    return progressBar;
}

void CompingProgressComponent::paint(juce::Graphics& g)
{
    auto* neonLF = dynamic_cast<NeonLookAndFeel*>(&getLookAndFeel());
    const NeonTheme* t = neonLF ? &neonLF->getTheme() : nullptr;

    auto bg = t ? t->background : juce::Colours::black;
    g.fillAll(bg);
}

void CompingProgressComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    auto titleArea = area.removeFromTop(32);
    titleLabel.setBounds(titleArea);

    progressBar.setBounds(area);
}

void setupPythonEnv()
{
    auto pyDir = AppPaths::pythonDir().getFullPathName();
    auto rootDir = AppPaths::root().getFullPathName();   // <-- the parent of src/
    auto srcDir = AppPaths::srcDir().getFullPathName();
    auto mlDir = AppPaths::mlDir().getFullPathName();

#if JUCE_WINDOWS
    ::SetEnvironmentVariableW(L"PYTHONPATH",
        (rootDir + ";" + srcDir + ";" + mlDir).toWideCharPointer());

    juce::String oldPath = juce::SystemStats::getEnvironmentVariable("PATH", "");
    juce::String newPath = pyDir + ";" + oldPath;
    ::SetEnvironmentVariableW(L"PATH", newPath.toWideCharPointer());
#else
    setenv("PYTHONPATH", (rootDir + ":" + srcDir + ":" + mlDir).toRawUTF8(), 1);
    #endif
}

MainComponent::MainComponent()
{
    setLookAndFeel(&neonLookAndFeel);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);



    setupPythonEnv();

    // Debug: log what PYTHONPATH is set to
    juce::String pypath = juce::SystemStats::getEnvironmentVariable("PYTHONPATH", "(not set)");
    juce::Logger::writeToLog("PYTHONPATH = " + pypath);

    neonLookAndFeel.setUsingNativeAlertWindows(true);

    static bool loggerSet = false;
    if (!loggerSet)
    {
        loggerSet = true;
        juce::Logger::setCurrentLogger(new juce::FileLogger(
            AppPaths::root().getChildFile("debug_log.txt"),
            "Log", 1024 * 1024));
    }
    ;

    // Set window size
    auto area = juce::Desktop::getInstance().getDisplays()
        .getMainDisplay().userArea;

    int w = juce::jmin(1600, area.getWidth());
    int h = juce::jmin(900, area.getHeight());
    setSize(w, h);

    // --- Audio setup ---
    formatManager.registerBasicFormats(); // WAV, AIFF, etc.

    // 1 input (for mic), 2 outputs
    setAudioChannels(1, 2);

    AppPaths::ensureFoldersExist();
    if (!AppPaths::workingDataDir().setAsCurrentWorkingDirectory())
        AppPaths::root().setAsCurrentWorkingDirectory();

    // Initialise data_pilot/singer_user/phraseXX for this session
    initialiseUserPhraseDirectory();

    // --- UI setup ---
    addAndMakeVisible(recordingTabButton);
    addAndMakeVisible(compedTabButton);
    addAndMakeVisible(importButton);
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(resetButton);
    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(keyLabel);
    addAndMakeVisible(metronomeToggle);
    addAndMakeVisible(recordButton);
    addAndMakeVisible(ioButton);
    addAndMakeVisible(takeVolumeLabel);
    addAndMakeVisible(takeVolumeSlider);
    addAndMakeVisible(accuracyEmotionLabel);
    addAndMakeVisible(accuracyEmotionSlider);
    addAndMakeVisible(crossfadeLabel);
    addAndMakeVisible(crossfadeSlider);
    addAndMakeVisible(compingButton);
    addAndMakeVisible(exportCompedButton);
    addAndMakeVisible(saveProjectButton);
    addAndMakeVisible(loadProjectButton);
    addAndMakeVisible(styleLeftLabel);
    addAndMakeVisible(styleRightLabel);
    addAndMakeVisible(crossfadeLeftLabel);
    addAndMakeVisible(crossfadeRightLabel);
    // Scrollable takes view (Recording tab)
    addAndMakeVisible(takesViewport);
    for (int i = 0; i < 3; ++i)
    {
        auto* selectBtn = new NeonButton("Select");
        auto* soloBtn = new NeonButton("Solo");

        selectBtn->setClickingTogglesState(true);
        soloBtn->setClickingTogglesState(true);
        selectBtn->addListener(this);
        soloBtn->addListener(this);

        compedSelectButtons.add(selectBtn);
        compedSoloButtons.add(soloBtn);
        addAndMakeVisible(selectBtn);
        addAndMakeVisible(soloBtn);
    }
    // MainComponent_Views.cpp  (inside MainComponent constructor)
    addAndMakeVisible(mlModeToggle);
    mlModeToggle.setButtonText("Use ML Scoring (experimental)");
    mlModeToggle.setToggleState(false, juce::dontSendNotification);

    takesViewport.setViewedComponent(&takesContainer, false);
    takesViewport.setScrollBarsShown(true, false);
    takesViewport.setScrollOnDragEnabled(true);
    takesViewport.setVisible(false);   // only visible in Recording view


    importButton.addListener(this);
    playButton.addListener(this);
    stopButton.addListener(this);
    resetButton.addListener(this);
    metronomeToggle.addListener(this);
    recordButton.addListener(this);
    ioButton.addListener(this);
    recordingTabButton.addListener(this);
    compedTabButton.addListener(this);
    exportCompedButton.addListener(this);
    saveProjectButton.addListener(this);
    loadProjectButton.addListener(this);

    // Comped tab is disabled until we have a comping result
    compedTabButton.setEnabled(false);

    playButton.setEnabled(false);
    stopButton.setEnabled(false);
    metronomeToggle.setEnabled(false);
    recordButton.setEnabled(false);

    metronomeToggle.setClickingTogglesState(true);

    bpmLabel.setJustificationType(juce::Justification::centredLeft);
    bpmLabel.setInterceptsMouseClicks(false, false);
    refreshBpmLabel();
    keyLabel.setJustificationType(juce::Justification::centredLeft);
    keyLabel.setInterceptsMouseClicks(false, false);
    refreshKeyLabel();

    // "Select Take" label
    //selectTakeLabel.setText("Select", juce::dontSendNotification);
    //selectTakeLabel.setJustificationType(juce::Justification::centredRight);
    //selectTakeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    //selectTakeLabel.setInterceptsMouseClicks(false, false);

    //soloLabel.setText("Solo", juce::dontSendNotification);
    //soloLabel.setJustificationType(juce::Justification::centredRight);
    //soloLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    //soloLabel.setInterceptsMouseClicks(false, false);

    takeVolumeLabel.setText("Take Volume", juce::dontSendNotification);
    takeVolumeLabel.setJustificationType(juce::Justification::centredLeft);
    takeVolumeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    takeVolumeLabel.setInterceptsMouseClicks(false, false);

    takeVolumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    takeVolumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    takeVolumeSlider.setRange(0.0, 1.5, 0.01);
    takeVolumeSlider.setValue(1.0);

    takeVolumeSlider.onValueChange = [this]
        {
            takeTransport.setGain((float)takeVolumeSlider.getValue());
        };
    takeTransport.setGain((float)takeVolumeSlider.getValue());

    // --- Comping UI (STYLE / CROSSFADE knobs) ---

// Top titles
    accuracyEmotionLabel.setText("STYLE", juce::dontSendNotification);
    accuracyEmotionLabel.setJustificationType(juce::Justification::centred);
    accuracyEmotionLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    accuracyEmotionLabel.setInterceptsMouseClicks(false, false);

    crossfadeLabel.setText("CROSSFADE", juce::dontSendNotification);
    crossfadeLabel.setJustificationType(juce::Justification::centred);
    crossfadeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    crossfadeLabel.setInterceptsMouseClicks(false, false);

    // STYLE knob (Accuracy <-> Emotion)
    accuracyEmotionSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    accuracyEmotionSlider.setRange(0.0, 100.0, 1.0);
    accuracyEmotionSlider.setValue(50.0);
    // we draw value ourselves in LookAndFeel
    accuracyEmotionSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    // CROSSFADE knob (Short <-> Long)
    crossfadeSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    crossfadeSlider.setRange(0.0, 100.0, 1.0);
    crossfadeSlider.setValue(50.0);
    crossfadeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    // Side labels for STYLE knob
    styleLeftLabel.setText("ACCURACY", juce::dontSendNotification);
    styleLeftLabel.setJustificationType(juce::Justification::centredRight);
    styleLeftLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    styleLeftLabel.setInterceptsMouseClicks(false, false);

    styleRightLabel.setText("EMOTION", juce::dontSendNotification);
    styleRightLabel.setJustificationType(juce::Justification::centredLeft);
    styleRightLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    styleRightLabel.setInterceptsMouseClicks(false, false);

    // Side labels for CROSSFADE knob
    crossfadeLeftLabel.setText("SHORT", juce::dontSendNotification);
    crossfadeLeftLabel.setJustificationType(juce::Justification::centredRight);
    crossfadeLeftLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    crossfadeLeftLabel.setInterceptsMouseClicks(false, false);

    crossfadeRightLabel.setText("LONG", juce::dontSendNotification);
    crossfadeRightLabel.setJustificationType(juce::Justification::centredLeft);
    crossfadeRightLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    crossfadeRightLabel.setInterceptsMouseClicks(false, false);



    compingButton.addListener(this);
    compingButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
    compingButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    exportCompedButton.setColour(juce::TextButton::buttonColourId,
        juce::Colours::darkgrey.darker(0.2f));
    exportCompedButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    // Timer for moving playhead / loop wrap (60 FPS)
    startTimerHz(60);

    // Listen for thumbnail changes so we repaint when it finishes loading
    thumbnail.addChangeListener(this);
    compedThumbnail.addChangeListener(this);

    updateTabButtonStyles();
    refreshCompedButtons();

    juce::Timer::callAfterDelay(337, [this]
        {
            showWelcomePopup();
        });

    juce::MessageManager::callAsync([this]()
        {
            launchMLServer();
        });

}

void MainComponent::launchMLServer()
{
    // Don't start twice
    if (mlServerProcess.isRunning())
        return;

    juce::File projectRoot = AppPaths::root();

    if (!projectRoot.isDirectory())
        projectRoot = juce::File::getCurrentWorkingDirectory();

    const bool isWindows = juce::SystemStats::getOperatingSystemType() & juce::SystemStats::Windows;

    juce::File pythonExe = AppPaths::pythonExe();

    if (!pythonExe.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "ML server error",
            "Bundled Python not found:\n" + pythonExe.getFullPathName());
        return;
    }

    juce::File serverPy = AppPaths::mlDir().getChildFile("server.py");

    if (!serverPy.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "ML server error",
            "server.py not found:\n" + serverPy.getFullPathName());
        return;
    }

    juce::StringArray args;
    args.add(pythonExe.getFullPathName());
    args.add(serverPy.getFullPathName());

    // JUCE has no cwd argument, so temporarily set it and restore
    struct CwdRestorer
    {
        juce::File old;
        CwdRestorer() : old(juce::File::getCurrentWorkingDirectory()) {}
        ~CwdRestorer() { old.setAsCurrentWorkingDirectory(); }
    } restore;

    projectRoot.setAsCurrentWorkingDirectory();

    if (!mlServerProcess.start(args))
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "ML server error",
            "Could not launch ML server.\nCommand:\n" + args.joinIntoString(" "));
        return;
    }

    // Success: server is running in background
}

void MainComponent::showWelcomePopup()
{
    auto options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::InfoIcon)
        .withTitle("Welcome")
        .withMessage(
            "Welcome to your Vocal Comping Assistant Alpha! "
            "If you find any bugs, drop a message to pr.ae@outlook.com. "
            "Note that there is a slight delay with recorded takes playback - "
            "will be fixed in the very next version.")
        .withButton("Load Example")
        .withButton("OK")
        .withAssociatedComponent(getTopLevelComponent());

    juce::NativeMessageBox::showAsync(options,
        [this](int result)
        {
            // NativeMessageBox returns 0 for the first button
            if (result == 0)
                loadExampleProject();
        });
}

void MainComponent::loadExampleProject()
{
    // find project root by walking up until we see "data_pilot"
    juce::File root = AppPaths::root();

    while (!root.isRoot() && !root.getChildFile("data_pilot").isDirectory())
        root = root.getParentDirectory();

    juce::File exampleFile = root.getChildFile(
        "data_pilot/singer_user/phrase78/project_example.json");

    if (!exampleFile.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Example not found",
            "Could not find:\n" + exampleFile.getFullPathName());
        return;
    }

    ProjectState state;
    juce::String error;

    if (!ProjectState::loadFromFile(state, exampleFile, error))
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Load failed",
            "Could not load example:\n" + error);
        return;
    }

    resetProjectState();
    applyProjectState(state);

    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon,
        "Example loaded",
        "Loaded example project:\n" + exampleFile.getFullPathName());
}

void MainComponent::restartPlaybackForCompEdit()
{
    const bool wasPlaying = transportSource.isPlaying() || takeTransport.isPlaying();

    transportSource.stop();
    takeTransport.stop();

    // reset positions
    transportSource.setPosition(getEffectiveLoopStartSec());
    takeTransport.setPosition(getEffectiveCompLoopStartSec());

    if (wasPlaying)
    {
        transportSource.start();
        takeTransport.start();
    }
}

void MainComponent::restartCompPlaybackIfPlaying()
{
    const bool wasPlaying = transportSource.isPlaying() || takeTransport.isPlaying();

    transportSource.stop();
    takeTransport.stop();

    transportSource.setPosition(getEffectiveLoopStartSec());
    takeTransport.setPosition(getEffectiveCompLoopStartSec());

    if (wasPlaying)
    {
        transportSource.start();
        takeTransport.start();
    }
}

MainComponent::~MainComponent()
{
    thumbnail.removeChangeListener(this);
    compedThumbnail.removeChangeListener(this);

    if (compingDialogWindow != nullptr)
    {
        compingDialogWindow->setVisible(false);
        compingDialogWindow = nullptr;
        compingProgressComponent = nullptr;
    }

    if (mlServerProcess.isRunning())
        mlServerProcess.kill();

    setLookAndFeel(nullptr);
    shutdownAudio();
}


void MainComponent::onCompingFinished(bool /*success*/)
{
    // 1) Jump bar to 100%
    if (compingProgressComponent != nullptr)
    {
        compingProgressComponent->getProgressBar().setBackendFinished();
    }

    // 2) Close and reset the dialog
    if (compingDialogWindow != nullptr)
    {
        compingDialogWindow->setVisible(false); // DialogWindow is deleteOnClose by default
        compingDialogWindow = nullptr;
        compingProgressComponent = nullptr;
    }


}

void CompingProgressComponent::startCompingProgress(const juce::File& features,
    const juce::File& segments,
    const juce::File& compmap,
    const juce::File& comped)
{
    progressBar.startCompingWithFiles(features, segments, compmap, comped);
}




void MainComponent::updateTabButtonStyles()
{
    auto activeColour = juce::Colours::darkgrey.brighter(0.4f);
    auto inactiveColour = juce::Colours::darkgrey.darker(0.4f);
    auto disabledColour = juce::Colours::darkgrey.darker(0.8f);

    // Recording tab
    recordingTabButton.setColour(
        juce::TextButton::buttonColourId,
        (viewMode == ViewMode::Recording ? activeColour : inactiveColour));

    // Comped tab
    juce::Colour compColour = hasLastCompResult ? inactiveColour : disabledColour;
    if (viewMode == ViewMode::CompReview && hasLastCompResult)
        compColour = activeColour;

    compedTabButton.setColour(juce::TextButton::buttonColourId, compColour);

    recordingTabButton.repaint();
    compedTabButton.repaint();
}