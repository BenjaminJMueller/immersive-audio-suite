// Raumklang app: main window content.
//   top bar:  project name | 1 Lautsprecher  2 Quellen  3 Raum  4 Ausgabe | undo redo open save help
//   left:     list of speakers or sources
//   centre:   top view of the room
//   right:    inspector for the current step and selection
#pragma once

#include "AudioHost.h"

#include <rk_ui/Pages.h>
#include <rk_ui/StageView.h>
#include <rk_ui/Strings.h>
#include <rk_ui/Widgets.h>

namespace rkapp {

class IntroOverlay;

class MainComponent : public juce::Component, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    /// Called on quit: writes the autosave immediately.
    void saveNow();

private:
    enum Step { StepSpeakers = 0, StepSources = 1, StepRoom = 2, StepOutput = 3 };

    void timerCallback() override;
    void setStep(int step);
    void applyScene(bool structural);
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
    void selectSource(int i);
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
    rkui::StageView stage_;
    rkui::FormPage inspector_;
    std::unique_ptr<IntroOverlay> intro_;
    juce::String deviceError_;
    juce::String statusText_;
    std::array<bool, rk::kMaxSources> sourceHasSignal_{};
    std::array<double, rk::kMaxSources> signalSeen_{};
    juce::TooltipWindow tooltips_{this, 600};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace rkapp
