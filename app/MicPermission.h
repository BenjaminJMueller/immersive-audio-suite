// Microphone permission (macOS asks before an app may open audio inputs).
// Raumklang opens inputs only when a source or the calibration needs them, so
// that playing test signals or DAW tracks never triggers the system prompt.
#pragma once

#include <functional>

namespace rkapp {

enum class MicAccess { Granted, Denied, NotAsked };

/// Current state; always Granted on systems without such a permission.
MicAccess micAccess();
/// Asks the system once (shows the macOS prompt if not asked yet). `done` runs on the message thread.
void requestMicAccess(std::function<void(bool granted)> done);

} // namespace rkapp
