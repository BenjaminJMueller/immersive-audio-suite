// macOS system hooks: keep the audio path responsive and skip drawing while hidden.
#pragma once

namespace juce { class Component; }

namespace rkapp {

/// Asks the system not to throttle the app (App Nap, timer coalescing) while audio runs.
/// Call once at start; harmless on other systems.
void keepAudioResponsive();
/// False while the component's window is fully hidden (another Space, covered or minimised).
/// Animations then pause, which keeps the GPU and the message thread free during a Space swipe.
bool windowOnScreen(const juce::Component& c);

} // namespace rkapp
