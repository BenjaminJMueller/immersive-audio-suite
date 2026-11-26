// Raumklang app: main window content.
//   top bar:  project name | 1 Lautsprecher  2 Quellen  3 Raum  4 Ausgabe | undo redo open save help
//   left:     list of speakers or sources
//   centre:   top view of the room
//   right:    inspector for the current step and selection
#pragma once

#include "AudioHost.h"
#include "Updater.h"
#include "Calibration.h"

#include <rk_ui/Pages.h>
#include <rk_ui/ReverbPanel.h>
#include <rk_ui/RoomView.h>
#include <rk_ui/Snapshots.h>
#include <rk_ui/Strings.h>
#include <rk_ui/Visuals.h>
#include <rk_ui/Widgets.h>

namespace rkapp {

class IntroOverlay;

class MainComponent : public juce::Component, public juce::FileDragAndDropTarget, public juce::MenuBarModel,
                      public juce::ApplicationCommandTarget, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    /// Called on quit: writes the autosave immediately.
    void saveNow();

    /// A measurement file from "Raumklang Messen" (iPhone) dropped on the window.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void loadSurvey(const juce::File& file);

    // Menu bar (macOS) and keyboard commands (all systems).
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int index, const juce::String& name) override;
    void menuItemSelected(int, int) override {}
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) override;
    bool perform(const InvocationInfo& info) override;

private:
    enum Step { StepSpeakers = 0, StepSources = 1, StepRoom = 2, StepOutput = 3 };

    void timerCallback() override;
    double lastTick_ = 0.0; // to notice sleep (see timerCallback)
    void setStep(int step);
    void applyScene(bool structural);
    /// Opens the interface inputs once something needs them (asks for microphone access on macOS once).
    void ensureInputs();
    bool micRequested_ = false;
    Updater updater_;
    void commit();
    void undo();
    void redo();
    void loadScene(const rkui::Scene& s, bool resetHistory);
    void refreshAll();
    void refreshSidebar();
    void rebuildInspector();
    juce::String inspectorKey_; // which page the inspector shows, to keep its scroll position
    void buildSourcesPage();
    void buildOutputPage();
    /// Highest device output the routing uses (or 2 for headphones).
    int outputsNeeded() const;
    void addDeviceRows();
    void buildDawTrackPage(int track);
    void refreshDawValues();
    void scanDaw();
    /// The scene plus the DAW tracks (locked, after the scene's own sources) for the stage.
    rkui::Scene stageScene() const;
    void showStage();
    int dawTrackLevelIndex(int track) const;
    void selectSource(int i);
    void selectRoomPreset(int i);
    /// Moves DAW track `j` (index into dawTracks_): sent to its plugin, shown here at once.
    void moveDawTrack(int j, float azimuthDeg, float elevationDeg, float distance);
    /// Level (0), spread (1) or reverb send (2) of DAW track `j`, changed in the app.
    void changeDawMix(int j, int index, float value);
    struct DawMove { rk::Vec3 position; double until = 0.0; };
    std::array<DawMove, rk::bridge::kSlots> dawMoves_{}; // shown until the plugin has caught up
    std::array<double, rk::bridge::kSlots> dawMixUntil_{}; // mix sliders held until the plugin has caught up
    void selectSpeaker(int i);
    void addSource();
    void addSpeaker();
    void speakerMenu(int i, juce::Rectangle<int> area);
    void sourceMenu(int i, juce::Rectangle<int> area);
    void removeSource(int i);
    void startDemo();
    void startEmpty();
    void openProject();
    void saveProjectAs();
    void showHelpMenu();
    void setUiLanguage(bool german);
    void setUiTheme(bool dark);
    /// Replaces the current project after asking, unless it is saved in a file anyway.
    void confirmReplaceProject(std::function<void()> then);
    juce::ApplicationCommandManager commands_;
    juce::ScopedMessageBox replaceBox_;
    void applyTheme();
    void updateCoach();
    void updateTooltips();
    void showIntro(bool keepProject = false);
    rkui::PageContext pageContext();

    // Speaker calibration (microphone) and phone measurements.
    void startCalibration();
    void restartCalibration();
    void endCalibration();
    void buildCalibrationPage();
    void buildPhonePage();
    /// "Mit dem iPhone einmessen": explains the measurement before a file is loaded.
    void startPhoneIntro();
    /// macOS: installs the plugins that ship inside the app (see PluginInstaller.h).
    void installPlugins(bool manual);
    bool installingPlugins_ = false;
    juce::ScopedMessageBox pluginBox_;
    /// A wrapped paragraph in the inspector, always visible.
    void addNote(const juce::String& text);
    /// Numbered steps ("1. …" per line), always visible.
    void addSteps(const juce::String& text);
    void evaluateCalibration();
    void acceptProposal();
    void chooseSurveyFile();
    void updateCalibrationOverlay();
    juce::String calibrationSubtitle(int speaker) const;
    juce::String sourceSubtitle(int i) const;

    AudioHost host_;
    rkui::Scene scene_;
    juce::String committed_;
    std::vector<juce::String> undo_, redo_;
    bool dirty_ = false;
    double lastChange_ = 0.0;
    int step_ = StepSources;
    int selSource_ = 0, selSpeaker_ = -1;
    bool speakerTestRunning_ = false;
    int speakerTestIndex_ = 0;
    double speakerTestNext_ = 0.0;
    double lastOrbitTime_ = 0.0;
    juce::File projectFile_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<juce::PropertiesFile> settings_;

    rkui::LookAndFeel lnf_;
    rkui::Segmented steps_;
    rkui::IconButton undoBtn_{rkui::Icon::Undo, {}}, redoBtn_{rkui::Icon::Redo, {}}, openBtn_{rkui::Icon::Open, {}},
        saveBtn_{rkui::Icon::Save, {}}, helpBtn_{rkui::Icon::Help, {}};
    std::unique_ptr<juce::TextButton> addBtn_;
    rkui::SidebarList sidebar_;
    juce::Viewport sidebarView_;
    rkui::RoomView stage_;
    rkui::ReverbPanel reverbPanel_; // replaces the room view in step 3
    rkui::SnapshotBar snapBar_;      // under the room view
    float reverbEnv_ = 0.0f;       // reverb level for the views, rings out with RT60
    double reverbEnvTime_ = 0.0;
    rkui::ReductionMeter* limiterMeter_ = nullptr; // on the output page while it is shown
    rkui::ReductionMeter* compMeter_ = nullptr;
    rkui::OutputStrip outputStrip_;  // pinned at the bottom of the sidebar

    // Snapshots: storing, recalling with a fade, and the menu of one snapshot.
    void storeSnapshot();
    void recallSnapshot(int i);
    void snapshotMenu(int i);
    std::vector<rkui::DawTrackState> currentDawStates() const;
    void stepSnapshotFade(double now);
    int activeSnapshot_ = -1;
    bool fading_ = false;
    double fadeStart_ = 0.0, fadeMs_ = 0.0;
    rkui::Scene fadeFrom_;
    std::vector<rkui::DawTrackState> fadeDawFrom_;
    rkui::FormPage inspector_;
    std::unique_ptr<IntroOverlay> intro_;
    juce::String deviceError_;
    std::vector<rk::bridge::TrackInfo> dawTracks_; // tracks from "Raumklang Quelle" in the DAW
    juce::String dawKey_;
    double lastDawScan_ = 0.0;
    int dawPageTrack_ = -1;                       // DAW track the inspector shows, -1 none
    std::array<rkui::ParamSlider*, 3> posSliders_{}; // azimuth, elevation, distance of the selected source (owned by inspector_)
    std::vector<rkui::ParamSlider*> dawSliders_;  // owned by inspector_, valid while dawPageTrack_ >= 0
    rkui::StatusCard* dawCard_ = nullptr;
    rkui::SpectrumAnalyzer analyzer_;
    juce::String statusText_;
    juce::String dawStageKey_;   // positions of the DAW tracks last drawn
    juce::String deviceSummary_; // sample rate and latency, under the project name
    double deviceSummaryAt_ = 0.0;
    std::array<bool, rk::kMaxSources> sourceHasSignal_{};
    std::array<double, rk::kMaxSources> signalSeen_{};
    rkui::RoundTooltipWindow tooltips_{this, 600};

    bool calibrating_ = false;
    bool phoneMode_ = false;
    std::unique_ptr<Calibration> calib_;
    CalibrationSettings calibSettings_;
    rkui::Survey survey_;
    int surveyMatch_ = 0;
    std::optional<rkui::Proposal> proposal_;
    rkui::ProposalOptions proposalOpts_;
    int calibSeen_ = -1; // phase, point and progress last shown in the inspector

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace rkapp
