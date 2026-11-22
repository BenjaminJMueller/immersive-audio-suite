// Raumklang UI: view of the room with speakers and sources, from above (azimuth),
// from the front or from the side (elevation).
#pragma once

#include "Scene.h"
#include "Theme.h"
#include "Widgets.h"

#include <array>
#include <functional>
#include <optional>

namespace rkui {

class StageView : public juce::Component, private juce::Timer {
public:
    enum class EditTarget { Sources, Speakers, None };
    /// Top: x right, y front (azimuth). Front: x right, z up. Side: y front (to the right), z up (elevation).
    enum class Projection { Top = 0, Front = 1, Side = 2 };

    StageView();
    void setScene(const Scene& s);
    void setEditTarget(EditTarget t) { target_ = t; repaint(); }
    void setSelectedSource(int i) { selSource_ = i; repaint(); }
    void setSelectedSpeaker(int i) { selSpeaker_ = i; repaint(); }
    void setSpeakerLevels(const std::vector<float>& l) {
        if (!levelsDiffer(speakerLevels_, l)) return;
        speakerLevels_ = l;
        repaint();
    }
    /// 0..1 per source, drawn as a ring around the source.
    void setSourceLevels(const std::vector<float>& l);
    /// 0..1: how much reverb fills the room right now (a soft glow over the stage).
    void setAmbience(float a) { ambience_ = a; }
    /// Coach bar at the bottom centre: the next step in plain words (Projection Studio style).
    /// `badge` is a step number or empty; `ok` turns the badge green.
    void setCoach(juce::String badge, juce::String title, juce::String text, bool ok = false);
    /// Sources from this index on are shown but cannot be dragged (e.g. tracks from the DAW).
    /// Sources from `i` on cannot be dragged. Sources from `firstDaw` on (default: the same
    /// index) carry a small "DAW" marker.
    void setFirstLockedSource(int i, int firstDaw = -1) { firstLocked_ = i; firstDaw_ = firstDaw < 0 ? i : firstDaw; repaint(); }
    void setProjection(Projection p);
    /// Look of the sources: their colours (transparent or missing = list colour), from which
    /// index on they are drawn dimmed (other tracks seen from a plugin), and whether they
    /// carry their name instead of a number.
    void setSourceStyle(std::vector<juce::Colour> colours, int dimFrom, bool names);
    /// Embedded in a RoomView, which has its own view switch and draws one legend.
    void setShowViewSwitch(bool on);
    void setShowLegend(bool on) { showLegend_ = on; repaint(); }
    /// Legend at the bottom left instead of the top left (no coach bar in this view).
    void setLegendAtBottom(bool on) { legendBottom_ = on; repaint(); }
    /// Small caption at the top left ("Oben", "Vorne") when two views sit side by side.
    void setCaption(juce::String c) { caption_ = std::move(c); repaint(); }

    /// Calibration overlay: measuring points for the microphone and, after a measurement, where
    /// the speakers were before (thin dashed outline joined to the new position).
    struct CalibrationOverlay {
        std::vector<rk::Vec3> points;
        std::vector<int> pointState;  // 0 open, 1 measured, 2 problem
        int activePoint = -1;
        std::vector<rk::Vec3> before; // per speaker, previous position
        std::vector<char> moved;      // per speaker: draw the "before" marker
        int playingSpeaker = -1;      // speaker that plays the sweep right now
    };
    void setCalibration(CalibrationOverlay o);
    void clearCalibration();
    bool hasCalibration() const noexcept { return calib_.has_value(); }
    Projection projection() const noexcept { return proj_; }

    std::function<void(int, rk::Vec3)> onSourceMoved;
    std::function<void(int, rk::Vec3)> onSpeakerMoved;
    std::function<void(int)> onSourceSelected;
    std::function<void(int)> onSpeakerSelected;
    std::function<void()> onDragEnd;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseMove(const juce::MouseEvent& e) override;

    juce::Point<float> toScreen(rk::Vec3 p) const;
    /// Screen point to world position: the two coordinates of the view plane come from
    /// the screen, the third is kept from `keep`.
    rk::Vec3 toWorld(juce::Point<float> s, rk::Vec3 keep) const;

private:
    void updateScale();
    void paintLegend(juce::Graphics& g);
    void paintCalibrationBelow(juce::Graphics& g);
    void paintCalibrationAbove(juce::Graphics& g);
    void paintCoach(juce::Graphics& g);
    void paintGrid(juce::Graphics& g, float step);
    juce::Colour sourceColour(int s) const;
    juce::Point<float> plane(rk::Vec3 p) const; // view-plane coordinates in metres
    int hitSource(juce::Point<float> p) const;
    int hitSpeaker(juce::Point<float> p) const;

    Scene scene_;
    rk::Panner panner_;
    EditTarget target_ = EditTarget::Sources;
    int selSource_ = -1, selSpeaker_ = -1;
    int dragSource_ = -1, dragSpeaker_ = -1;
    bool dragging_ = false;
    bool animating_ = false; // sound particles are visible: keep them twinkling
    void timerCallback() override {
        if (animating_ && isShowing()) repaint();
        else stopTimer();
    }
    bool panningBuilt_ = false, panningStale_ = false;
    float scale_ = 40.0f; // pixels per metre
    float zoom_ = 1.0f;
    juce::Point<float> centre_;
    std::vector<float> speakerLevels_, sourceLevels_;
    float ambience_ = 0.0f;
    std::vector<juce::Colour> sourceColours_;
    int dimFrom_ = 1 << 30;
    bool sourceNames_ = false, showLegend_ = true, legendBottom_ = false;
    juce::String caption_;
    juce::String coachBadge_, coachTitle_, coachText_;
    bool coachOk_ = false;
    int firstLocked_ = 1 << 30;
    int firstDaw_ = 1 << 30;
    IconButton fit_{Icon::Fit, "Alles zeigen"};
    Projection proj_ = Projection::Top;
    std::optional<CalibrationOverlay> calib_;
    Segmented views_;
};

} // namespace rkui
