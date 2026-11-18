// Raumklang UI: inspector pages shared between the app and the plugins.
#pragma once

#include "Scene.h"
#include "Widgets.h"

namespace rkui {

struct PageContext {
    Scene* scene = nullptr;
    /// Something changed. `structural` = layout or panner changed (engine must rebuild).
    std::function<void(bool structural)> changed;
    /// End of a user gesture: record an undo step and autosave.
    std::function<void()> commit;
    /// The page must be rebuilt (e.g. different controls are needed now).
    std::function<void()> rebuild;
};

/// Preset, analysis card, panning method and the selected speaker.
void buildLayoutPage(FormPage& page, PageContext ctx, int selectedSpeaker,
                     std::function<void(int)> selectSpeaker);
/// Reverb ("Raum").
void buildRoomPage(FormPage& page, PageContext ctx);
/// Fills a status card from the layout analysis in plain language.
void describeLayout(const Scene& s, StatusCard& card);

} // namespace rkui
