// Raumklang UI: top view of the room with speakers and sources.
#pragma once

#include "Scene.h"
#include "Theme.h"

#include <functional>

namespace rkui {

class StageView : public juce::Component {
public:
    enum class EditTarget { Sources, Speakers, None };

    StageView();
    void setScene(const Scene& s);
    void setEditTarget(EditTarget t) { target_ = t; repaint(); }
    void setSelectedSource(int i) { selSource_ = i; repaint(); }
    void setSelectedSpeaker(int i) { selSpeaker_ = i; repaint(); }
    void setSpeakerLevels(const std::vector<float>& l) { speakerLevels_ = l; repaint(); }
    void setSourceLevels(const std::vector<float>& l) { sourceLevels_ = l; repaint(); }
    void setHint(juce::String h) { hint_ = std::move(h); repaint(); }

    std::function<void(int, rk::Vec3)> onSourceMoved;
    std::function<void(int, rk::Vec3)> onSpeakerMoved;
    std::function<void(int)> onSourceSelected;
    std::function<void(int)> onSpeakerSelected;
    std::function<void()> onDragEnd;

    void paint(juce::Graphics& g) override;
    void resized() override { updateScale(); }
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

    juce::Point<float> toScreen(rk::Vec3 p) const;
    rk::Vec3 toWorld(juce::Point<float> s, float z) const;

private:
    void updateScale();
    int hitSource(juce::Point<float> p) const;
    int hitSpeaker(juce::Point<float> p) const;

    Scene scene_;
    rk::Panner panner_;
    EditTarget target_ = EditTarget::Sources;
    int selSource_ = -1, selSpeaker_ = -1;
    int dragSource_ = -1, dragSpeaker_ = -1;
    bool dragging_ = false;
    float scale_ = 40.0f; // pixels per metre
    float zoom_ = 1.0f;
    juce::Point<float> centre_;
    std::vector<float> speakerLevels_, sourceLevels_;
    juce::String hint_;
};

} // namespace rkui
