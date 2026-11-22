// Raumklang UI: the room view used everywhere (app and plugins). One switch on the top
// left chooses between the view from above, from the front, both side by side, and 3D.
#pragma once

#include "StageView.h"
#include "View3D.h"

namespace rkui {

class RoomView : public juce::Component {
public:
    enum class Mode { Top = 0, Front = 1, Split = 2, ThreeD = 3 };

    RoomView();
    void setMode(Mode m);
    Mode mode() const noexcept { return mode_; }

    void setScene(const Scene& s);
    void setSelectedSource(int i);
    void setSpeakerLevels(const std::vector<float>& l);
    void setSourceLevels(const std::vector<float>& l);
    void setSourceStyle(const std::vector<juce::Colour>& colours, int dimFrom, bool names);
    void setFirstLockedSource(int i);

    std::function<void(int, rk::Vec3)> onSourceMoved;
    std::function<void(int)> onSourceSelected;
    std::function<void()> onDragEnd;
    std::function<void(Mode)> onModeChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

    StageView& top() noexcept { return top_; }
    StageView& front() noexcept { return front_; }

private:
    Segmented switch_;
    StageView top_, front_;
    View3D view3d_;
    Mode mode_ = Mode::Split;
};

} // namespace rkui
