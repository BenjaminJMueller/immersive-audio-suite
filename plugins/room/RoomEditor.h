#pragma once

#include "RoomProcessor.h"

#include <rk_ui/Pages.h>
#include <rk_ui/StageView.h>
#include <rk_ui/Strings.h>

namespace rkplug {

class RoomEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit RoomEditor(RoomProcessor& p);
    ~RoomEditor() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuild();
    void buildOutputPage();
    rkui::PageContext context();

    RoomProcessor& proc_;
    rkui::LookAndFeel lnf_;
    rkui::Segmented tabs_;
    rkui::StageView stage_;
    rkui::FormPage page_;
    int tab_ = 0;
    int selSpeaker_ = -1;
    bool testRunning_ = false;
    int testIndex_ = 0;
    double testNext_ = 0.0;
    std::vector<float> levels_;
};

} // namespace rkplug
