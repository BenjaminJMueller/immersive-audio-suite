// Raumklang UI: the room in perspective. Speakers on stands, sources as dots with a
// drop line to the listening plane, the lines a source plays on. Dragging turns the
// view, the mouse wheel zooms, a double click returns to the start view.
#pragma once

#include "Scene.h"
#include "Visuals.h"
#include "Theme.h"

#include <functional>

namespace rkui {

class View3D : public juce::Component, private juce::Timer {
public:
    View3D();
    void setScene(const Scene& s);
    void setSelectedSource(int i) { sel_ = i; repaint(); }
    void setSpeakerLevels(const std::vector<float>& l) {
        if (!levelsDiffer(speakerLevels_, l)) return;
        speakerLevels_ = l;
        repaint();
    }
    void setSourceLevels(const std::vector<float>& l) {
        if (!levelsDiffer(sourceLevels_, l)) return;
        sourceLevels_ = l;
        repaint();
    }
    /// Same meaning as StageView::setSourceStyle.
    void setSourceStyle(std::vector<juce::Colour> colours, int dimFrom, bool names);
    void resetView();

    std::function<void(int)> onSourceSelected;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

    /// Screen position and depth (distance along the view direction) of a world point.
    struct Projected { juce::Point<float> p; float depth; };
    Projected project(rk::Vec3 w) const;

private:
    juce::Colour sourceColour(int s) const;
    float extent() const;

    Scene scene_;
    rk::Panner panner_;
    bool panningBuilt_ = false;
    ParticleBatch particles_;
    bool animating_ = false; // sound particles are visible: keep them twinkling
    void timerCallback() override {
        if (animating_ && isShowing()) repaint();
        else stopTimer();
    }
    std::vector<float> speakerLevels_, sourceLevels_;
    std::vector<juce::Colour> sourceColours_;
    int dimFrom_ = 1 << 30;
    bool names_ = false;
    int sel_ = 0;
    float yaw_ = -0.25f, pitch_ = 0.62f, zoom_ = 1.0f;
    float yaw0_ = 0.0f, pitch0_ = 0.0f;
};

} // namespace rkui
