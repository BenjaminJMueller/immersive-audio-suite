#pragma once

#include "../common/ParamBinding.h"
#include "SourceProcessor.h"

#include <rk_ui/StageView.h>
#include <rk_ui/Strings.h>
#include <rk_ui/Theme.h>
#include <rk_ui/Widgets.h>

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
    void refreshStatus();
    void buildPage();
    rkui::ParamSlider& addSlider(const char* id, const juce::String& label, const juce::String& unit, int decimals,
                                 double origin, const juce::String& tip);

    SourceProcessor& proc_;
    rkui::LookAndFeel lnf_;
    rkui::StageView stage_;
    rkui::FormPage page_;
    rkui::StatusCard* card_ = nullptr;     // owned by page_
    rkui::ChoiceRow* routeRow_ = nullptr;  // owned by page_
    juce::String cardKey_;
    // Declared after page_ so the bindings go first (they point at sliders in the page).
    std::vector<std::unique_ptr<ParamBinding>> bindings_;
    juce::TooltipWindow tooltips_{this, 600};
    rkui::Scene shown_;
    juce::String shownSetupJson_;
    std::vector<float> levels_;
    float inputLevel_ = 0.0f;
    bool dragGesture_ = false;
};

} // namespace rkplug
