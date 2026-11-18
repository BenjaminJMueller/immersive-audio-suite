#pragma once

#include "../common/ParamBinding.h"
#include "SourceProcessor.h"

#include <rk_ui/StageView.h>
#include <rk_ui/Strings.h>
#include <rk_ui/Theme.h>

namespace rkplug {

class SourceEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SourceEditor(SourceProcessor& p);
    ~SourceEditor() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshStage();
    rkui::ParamSlider& addSlider(const char* id, const juce::String& label, const juce::String& unit, int decimals, double origin);

    SourceProcessor& proc_;
    rkui::LookAndFeel lnf_;
    rkui::StageView stage_;
    rkui::StatusCard card_;
    std::vector<std::unique_ptr<rkui::ParamSlider>> sliders_;
    std::vector<std::unique_ptr<ParamBinding>> bindings_;
    rkui::Scene shown_;
    juce::String shownSetupJson_;
    std::vector<float> levels_;
    bool dragGesture_ = false;
};

} // namespace rkplug
