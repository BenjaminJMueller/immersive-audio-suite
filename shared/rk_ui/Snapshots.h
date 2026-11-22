// Raumklang UI: snapshots (stored scenes of the mix) and the fade between two of them.
#pragma once

#include "Scene.h"
#include "Theme.h"

#include <functional>

namespace rkui {

/// Takes the current mix as a snapshot. `daw` = the DAW tracks as they are now.
Snapshot captureSnapshot(const Scene& s, const std::vector<DawTrackState>& daw, const juce::String& name, float fadeSec);

/// Position between two points, moving around the listener (azimuth on the shorter
/// side, elevation and distance straight), so a fade never cuts through the middle.
rk::Vec3 blendPosition(rk::Vec3 from, rk::Vec3 to, float t);
DawTrackState blendDaw(const DawTrackState& from, const DawTrackState& to, float t);

/// Writes into `out` the mix at fraction `t` (0..1) of the way from `from` to `to`.
/// Sources are matched by index; extra sources on either side stay as they are in `from`.
/// Switches (mute, solo, circling, input) take the target value right away.
void blendSnapshot(Scene& out, const Scene& from, const Snapshot& to, float t);

/// Smooth start and stop for fades (0..1 -> 0..1).
float fadeCurve(float t);

/// The strip of snapshots under the room view: one button per snapshot, "+" stores
/// the current mix. Click recalls, right-click opens rename / overwrite / fade / delete.
class SnapshotBar : public juce::Component, public juce::SettableTooltipClient {
public:
    SnapshotBar();
    void setSnapshots(const std::vector<Snapshot>& list);
    /// The snapshot last recalled (-1 = none) and how far its fade is (1 = done).
    void setActive(int index, float progress);
    std::function<void(int)> onRecall;
    std::function<void()> onStore;
    std::function<void()> onUpdate; // overwrite the active snapshot with the current mix
    std::function<void(int)> onMenu;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    struct Chip { juce::String name; juce::String fade; };
    // Hit targets: chips 0..n-1, then the fixed buttons.
    enum { kPrev = 1000, kNext, kUpdate, kNew };
    struct Layout {
        std::vector<juce::Rectangle<float>> chips;
        juce::Rectangle<float> prev, next, update, add;
    };
    Layout layout() const;
    int hit(juce::Point<float> p) const;
    std::vector<Chip> chips_;
    int active_ = -1, hover_ = -1;
    float progress_ = 1.0f;
};

} // namespace rkui
