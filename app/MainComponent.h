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
#include <rk_ui/Strings.h>
#include <rk_ui/Visuals.h>
#include <rk_ui/Widgets.h>

namespace rkapp {

class IntroOverlay;

class MainComponent : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer {
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

private:
    enum Step { StepSpeakers = 0, StepSources = 1, StepRoom = 2, StepOutput = 3 };

    void timerCallback() override;
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
    void buildSourcesPage();
    void buildOutputPage();
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
    void selectSpeaker(int i);
    void addSource();
    void startDemo();
    void startEmpty();
    void openProject();
    void saveProjectAs();
    void showHelpMenu();
    void applyTheme();
    void updateCoach();
    void updateTooltips();
    void showIntro();
    rkui::PageContext pageContext();

    // Speaker calibration (microphone) and phone measurements.
    void startCalibration();
    void restartCalibration();
    void endCalibration();
    void buildCalibrationPage();
    void buildPhonePage();
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
    rkui::FormPage inspector_;
    std::unique_ptr<IntroOverlay> intro_;
    juce::String deviceError_;
    std::vector<rk::bridge::TrackInfo> dawTracks_; // tracks from "Raumklang Quelle" in the DAW
    juce::String dawKey_;
    double lastDawScan_ = 0.0;
    int dawPageTrack_ = -1;                       // DAW track the inspector shows, -1 none
    std::vector<rkui::ParamSlider*> dawSliders_;  // owned by inspector_, valid while dawPageTrack_ >= 0
    rkui::StatusCard* dawCard_ = nullptr;
    rkui::SpectrumAnalyzer analyzer_;
    juce::String statusText_;
    std::array<bool, rk::kMaxSources> sourceHasSignal_{};
    std::array<double, rk::kMaxSources> signalSeen_{};
    juce::TooltipWindow tooltips_{this, 600};

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
