// macOS: the plugins ship inside Raumklang.app (Contents/PlugIns), each signed on its own.
// Raumklang copies them into ~/Library/Audio/Plug-Ins itself, so nobody has to
// open a script in Terminal or get past Gatekeeper a second time. It does so on the first
// start of each new version and from the menu ("Plugins installieren").
// Windows keeps "Plugins installieren.bat" (the VST3 folder needs administrator rights).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rkapp {

struct PluginInstallResult {
    bool attempted = false; // false: nothing bundled (local build, Windows, Linux)
    bool ok = false;
    juce::StringArray installed; // bundle names
    juce::StringArray removed;   // plugins of earlier versions that were deleted
    juce::String error;
};

/// True if this app carries plugins it can install (macOS release builds).
bool hasBundledPlugins();
/// True if the bundled plugins should be (re)installed now: a new build, or one is missing.
bool pluginInstallDue(juce::PropertySet& settings, int build);
/// Copies the bundled plugins and removes obsolete ones of earlier versions (blocking, a few hundred milliseconds). Any thread.
PluginInstallResult installBundledPlugins();
/// Remembers that this build's plugins are installed.
void markPluginsInstalled(juce::PropertySet& settings, int build);

} // namespace rkapp
