#pragma once

#include "../common/ParamBinding.h"
#include "SourceProcessor.h"

#include <rk/bridge.hpp>
#include <rk_ui/RoomView.h>
#include <rk_ui/Strings.h>
#include <rk_ui/Theme.h>
#include <rk_ui/Widgets.h>

namespace rkplug {

/// The plugin window: the track's name at the top, the room on the left (with the other
/// tracks dimmed), the settings on the right. A line in the top bar only appears when
/// something needs attention.
class SourceEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SourceEditor(SourceProcessor& p);
    ~SourceEditor() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshStage();
    void refreshNotice();
    void buildPage();
    void scanOtherTracks();
    juce::Colour ownColour() const;
    rkui::ParamSlider& addSlider(const char* id, const juce::String& label, const juce::String& unit, int decimals,
                                 double origin, const juce::String& tip);

    SourceProcessor& proc_;
    rkui::LookAndFeel lnf_;
    rkui::RoomView room_;
    rkui::FormPage page_;
    juce::Label name_;
    rkui::IconButton help_{rkui::Icon::Help, {}};
    rkui::ChoiceRow* routeRow_ = nullptr; // owned by page_, only while "Erweitert" is open
    bool advancedOpen_ = false;
    // Declared after page_ so the bindings go first (they point at sliders in the page).
    std::vector<std::unique_ptr<ParamBinding>> bindings_;
    juce::TooltipWindow tooltips_{this, 600};

    rkui::Scene shown_;
    juce::String shownSetupJson_;
    rk::Panner panner_;
    std::vector<float> levels_;
    float inputLevel_ = 0.0f;
    bool dragGesture_ = false;

    enum class NoticeKind { None, Info, Warn };
    NoticeKind noticeKind_ = NoticeKind::None;
    juce::String notice_;

    // Other tracks with a Raumklang Quelle, read from the bridge (only while the app runs).
    rk::bridge::Receiver others_;
    std::vector<rk::bridge::TrackInfo> otherTracks_;
    int scanCountdown_ = 0;
};

} // namespace rkplug
