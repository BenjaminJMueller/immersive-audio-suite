// Raumklang UI: inspector pages shared between the app and the plugins.
#pragma once

#include "Scene.h"
#include "Widgets.h"

namespace rkui {

/// Speaker direction as (rotation, tilt) in degrees: rotation 0 = towards the centre,
/// 180 = outwards, positive = to the left; tilt positive = up.
std::pair<float, float> aimAngles(const rk::Speaker& sp);
void setAimAngles(rk::Speaker& sp, float rotDeg, float tiltDeg);

struct PageContext {
    Scene* scene = nullptr;
    /// Something changed. `structural` = layout or panner changed (engine must rebuild).
    std::function<void(bool structural)> changed;
    /// End of a user gesture: record an undo step and autosave.
    std::function<void()> commit;
    /// The page must be rebuilt (e.g. different controls are needed now).
    std::function<void()> rebuild;
    /// Optional (app only): measure the speakers with a microphone / load a phone measurement.
    std::function<void()> calibrate;
    std::function<void()> importSurvey;
};

/// Preset, analysis card, panning method and the selected speaker.
void buildLayoutPage(FormPage& page, PageContext ctx, int selectedSpeaker,
                     std::function<void(int)> selectSpeaker);
/// Reverb ("Raum").
void buildRoomPage(FormPage& page, PageContext ctx);
/// Fills a status card from the layout analysis in plain language.
/// Fills the card and returns true when the layout needs attention; false means all is fine.
bool describeLayout(const Scene& s, StatusCard& card);

} // namespace rkui
