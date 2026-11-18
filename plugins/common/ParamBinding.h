// Connects an rkui::ParamSlider to a host-automatable parameter (with proper
// begin/end gestures so DAWs record automation cleanly).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <rk_ui/Widgets.h>

namespace rkplug {

class ParamBinding {
public:
    ParamBinding(rkui::ParamSlider& slider, juce::RangedAudioParameter& param) : slider_(slider), param_(param) {
        slider_.setValue(param_.convertFrom0to1(param_.getValue()));
        slider_.onChange = [this](double v) {
            if (!inGesture_) { param_.beginChangeGesture(); inGesture_ = true; }
            param_.setValueNotifyingHost(param_.convertTo0to1((float)v));
        };
        slider_.onCommit = [this] {
            if (inGesture_) { param_.endChangeGesture(); inGesture_ = false; }
        };
    }
    ~ParamBinding() {
        if (inGesture_) param_.endChangeGesture();
    }
    /// Call from a UI timer: shows automation coming from the host.
    void refresh() {
        if (inGesture_) return;
        const double v = param_.convertFrom0to1(param_.getValue());
        if (std::abs(v - slider_.getValue()) > 1e-6) slider_.setValue(v);
    }

private:
    rkui::ParamSlider& slider_;
    juce::RangedAudioParameter& param_;
    bool inGesture_ = false;
};

} // namespace rkplug
