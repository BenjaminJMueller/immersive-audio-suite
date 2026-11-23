#pragma once

#include "../common/ParamBinding.h"
#include "ReturnProcessor.h"

#include <rk/bridge.hpp>
#include <rk/math.hpp>
#include <rk_ui/RoomView.h>
#include <rk_ui/Strings.h>
#include <rk_ui/Theme.h>
#include <rk_ui/Widgets.h>

namespace rkplug {

/// One vertical meter per returned channel, labelled with the speaker number.
class ReturnMeters : public juce::Component {
public:
    void set(int first, int channels, int appChannels, const std::vector<float>& levels);
    void paint(juce::Graphics& g) override;

private:
    int first_ = 1, channels_ = 0, appChannels_ = 0;
    std::vector<float> levels_;
};

class ReturnEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit ReturnEditor(ReturnProcessor& p);
    ~ReturnEditor() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshStatus();
    void refreshRoom();

    ReturnProcessor& proc_;
    rkui::LookAndFeel lnf_;
    rkui::RoomView room_;
    ReturnMeters meters_;
    rkui::FormPage page_;
    rkui::IconButton help_{rkui::Icon::Help, {}};
    bool noticeWarn_ = false;
    juce::String notice_, rangeText_;
    rkui::Scene scene_;
    rk::bridge::Receiver tracks_; // to show where the tracks are (read only)
    int roomCountdown_ = 0;
    std::vector<std::unique_ptr<ParamBinding>> bindings_;
    rkui::RoundTooltipWindow tooltips_{this, 600};
    std::vector<float> levels_;
    uint32_t recentDropouts_ = 0;
    int dropoutHold_ = 0;
};

} // namespace rkplug
