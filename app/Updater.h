// Automatic updates without an Apple developer account, like Projection Studio:
// read latest.json from the newest GitHub release -> newer build? -> ask -> download
// and check SHA-256 -> macOS: swap the app bundle (and the plugins) after quitting
// and start again; Windows: replace the exe (plugins with administrator rights).
// The update files are made by CI (.github/workflows/ci.yml, job "release").
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rkapp {

class Updater : private juce::Thread {
public:
    Updater();
    ~Updater() override;
    /// `manual`: also report "up to date" and errors (menu item). Message thread.
    void check(juce::Component* parent, bool manual);
    /// Checks at most once a day, a few seconds after start. Message thread.
    void checkInBackgroundIfDue(juce::Component* parent, juce::PropertySet& settings);
    /// Build number of this app (0 = local build: no automatic updates).
    static int currentBuild();

    struct Info {
        juce::String version, notes, fileName, sha256;
        int build = 0;
        juce::int64 size = 0;
    };
    /// Parses latest.json for this platform ("mac" or "win"). Exposed for tests.
    static bool parse(const juce::String& json, const juce::String& platform, Info& out);

private:
    void run() override;
    void finish(bool ok, const juce::String& message);
    juce::Component::SafePointer<juce::Component> parent_;
    bool manual_ = false;
    bool downloading_ = false;
    Info info_;
    juce::File tmp_;
};

} // namespace rkapp
