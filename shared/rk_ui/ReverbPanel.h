// Raumklang UI: the room (reverb) editor in the middle of app step 3. The reverb time over
// frequency as a curve with three points to drag (low, mid, high), and below it the time
// course: direct sound, early reflections, cluster and tail, drawn from the real parameters.
#pragma once

#include "Theme.h"
#include "Visuals.h"

#include <rk/reverb.hpp>

#include <functional>
#include <vector>

namespace rkui {

/// A room type for the sidebar. `wetDb` is the overall reverb level of the room.
struct RoomPreset {
    const char* name;
    rk::ReverbParams params;
    float wetDb;
};
const std::vector<RoomPreset>& roomPresets();
/// Index of the preset these settings equal, or -1 for a custom room.
int matchRoomPreset(const rk::ReverbParams& p, float wetDb);

/// The three reverb times the user edits: 0 = low (100 Hz), 1 = mid (1 kHz), 2 = high (8 kHz).
float bandFrequency3(int band);
float reverbBandRt(const rk::ReverbParams& p, int band);
/// Changes one band's reverb time and keeps the other two where they were, as far as the
/// network allows (the high band can never ring longer than the mid band).
void setReverbBandRt(rk::ReverbParams& p, int band, float seconds);
/// Room size shown as a volume: size 1 is 1000 m³.
float roomVolume(float size);

class ReverbPanel : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    ReverbPanel();
    void setParams(const rk::ReverbParams& p, float wetDb);
    void setBands(const std::array<float, SpectrumAnalyzer::kBands>& db);
    void setCoach(juce::String badge, juce::String title, juce::String text);
    /// While dragging a point, and once at the end of the gesture.
    std::function<void(const rk::ReverbParams&)> onChange;
    std::function<void()> onCommit;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    juce::Rectangle<float> curveArea() const;
    juce::Rectangle<float> timeArea() const;
    float maxRt() const;
    float xForHz(float hz) const;
    float yForRt(float rt) const;
    juce::Point<float> handlePos(int band) const;
    int hitHandle(juce::Point<float> p) const;
    void paintCurve(juce::Graphics& g);
    void paintTime(juce::Graphics& g);
    void paintCoach(juce::Graphics& g);

    rk::ReverbParams params_;
    float wetDb_ = -6.0f;
    float scaleRt_ = 4.0f; // y range of the curve, held while dragging
    float scaleTarget_ = 4.0f; // the range glides there (a scene fade changes it every frame)
    float rangeMs_ = 0.0f, rangeTarget_ = 0.0f; // time axis of the lower diagram, glides too
    static float timeRangeFor(const rk::ReverbParams& p);
    void animateScaleTo(float target);
    void timerCallback() override;
    int hover_ = -1, drag_ = -1;
    std::vector<std::array<float, SpectrumAnalyzer::kBands>> history_;
    juce::String coachBadge_, coachTitle_, coachText_;
};

} // namespace rkui
