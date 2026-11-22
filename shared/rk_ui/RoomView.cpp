#include "RoomView.h"

#include "Strings.h"

namespace rkui {

RoomView::RoomView() : switch_(juce::StringArray{tr("Oben"), tr("Vorne"), tr("Geteilt"), "3D"}, false) {
    setOpaque(true);
    switch_.setTooltip(tr("Ansicht: von oben, von vorne, beides nebeneinander oder räumlich"));
    switch_.onChange = [this](int i) {
        setMode(static_cast<Mode>(i));
        if (onModeChanged) onModeChanged(mode_);
    };
    top_.setProjection(StageView::Projection::Top);
    front_.setProjection(StageView::Projection::Front);
    for (auto* v : {&top_, &front_}) {
        v->setShowViewSwitch(false);
        v->setLegendAtBottom(true);
        v->setCoach({}, {}, {});
        v->onSourceMoved = [this](int i, rk::Vec3 p) {
            if (onSourceMoved) onSourceMoved(i, p);
        };
        v->onSourceSelected = [this](int i) {
            setSelectedSource(i);
            if (onSourceSelected) onSourceSelected(i);
        };
        v->onDragEnd = [this] {
            if (onDragEnd) onDragEnd();
        };
        addChildComponent(*v);
    }
    view3d_.onSourceSelected = [this](int i) {
        setSelectedSource(i);
        if (onSourceSelected) onSourceSelected(i);
    };
    addChildComponent(view3d_);
    addAndMakeVisible(switch_);
    setMode(Mode::Split);
}

void RoomView::setMode(Mode m) {
    mode_ = m;
    switch_.setSelected(static_cast<int>(m));
    top_.setVisible(m == Mode::Top || m == Mode::Split);
    front_.setVisible(m == Mode::Front || m == Mode::Split);
    view3d_.setVisible(m == Mode::ThreeD);
    // One legend only: in the split view it belongs to the view from above.
    front_.setShowLegend(m == Mode::Front);
    top_.setCaption(m == Mode::Split ? tr("Oben") : juce::String());
    front_.setCaption(m == Mode::Split ? tr("Vorne") : juce::String());
    resized();
}

void RoomView::setScene(const Scene& s) {
    top_.setScene(s);
    front_.setScene(s);
    view3d_.setScene(s);
}

void RoomView::setSelectedSource(int i) {
    top_.setSelectedSource(i);
    front_.setSelectedSource(i);
    view3d_.setSelectedSource(i);
}

void RoomView::setSpeakerLevels(const std::vector<float>& l) {
    if (top_.isVisible()) top_.setSpeakerLevels(l);
    if (front_.isVisible()) front_.setSpeakerLevels(l);
    if (view3d_.isVisible()) view3d_.setSpeakerLevels(l);
}

void RoomView::setSourceLevels(const std::vector<float>& l) {
    if (top_.isVisible()) top_.setSourceLevels(l);
    if (front_.isVisible()) front_.setSourceLevels(l);
    if (view3d_.isVisible()) view3d_.setSourceLevels(l);
}

void RoomView::setSourceStyle(const std::vector<juce::Colour>& colours, int dimFrom, bool names) {
    top_.setSourceStyle(colours, dimFrom, names);
    front_.setSourceStyle(colours, dimFrom, names);
    view3d_.setSourceStyle(colours, dimFrom, names);
}

void RoomView::setFirstLockedSource(int i) {
    top_.setFirstLockedSource(i);
    front_.setFirstLockedSource(i);
}

void RoomView::paint(juce::Graphics& g) {
    const Palette& p = palette();
    g.fillAll(p.stage);
    if (mode_ == Mode::Split) {
        g.setColour(p.line);
        g.fillRect(top_.getRight(), top_.getY() + 8, 1, top_.getHeight() - 16);
    }
}

void RoomView::resized() {
    auto r = getLocalBounds();
    auto head = r.removeFromTop(46);
    switch_.setBounds(head.reduced(12, 8).withWidth(juce::jmin(switch_.idealWidth(), head.getWidth() - 24)));
    if (mode_ == Mode::Split) {
        // The view from above gets the larger share: it is the one you work in most.
        auto left = r.removeFromLeft(juce::roundToInt(r.getWidth() * 0.55f));
        r.removeFromLeft(1);
        top_.setBounds(left);
        front_.setBounds(r);
    } else {
        top_.setBounds(r);
        front_.setBounds(r);
    }
    view3d_.setBounds(getLocalBounds().withTrimmedTop(46));
}

} // namespace rkui
