// Raumklang UI: the document model shared by the app and the plugins, and its
// JSON representation. Every value read from disk is validated and clamped, so a
// broken or hand-edited file can never crash the audio engine.
#pragma once

#include <juce_core/juce_core.h>
#include <rk/engine.hpp>

#include <vector>

namespace rkui {

enum class TestSignal { Off = 0, Pink = 1, Sine = 2, Clicks = 3 };

/// Automatic movement of a source. Every pattern starts at the source's own position and
/// comes back to it after one cycle; the tempo sets the cycles per minute.
enum class Motion { Off = 0, Circle = 1, Figure8 = 2, Pendulum = 3, Spiral = 4, UpDown = 5, Wander = 6 };
constexpr int kNumMotions = 7;

struct SourceInfo {
    juce::String name;
    int inputChannel = 0;           // device input (0-based)
    TestSignal test = TestSignal::Off;
    rk::Vec3 position{0.0f, 2.0f, 0.0f};
    float gainDb = 0.0f;
    float spread = 0.0f;
    float reverbSendDb = -12.0f;
    float distanceAmount = 1.0f;
    float yawDeg = 0.0f;            // where it faces: 0 = towards the listener
    float directivity = 0.0f;       // 0 = all directions alike, 1 = strongly directed
    bool doppler = false;
    bool mute = false;
    bool solo = false;
    Motion motion = Motion::Off;
    float orbitSpeed = 0.1f;        // cycles per second (revolutions for the circle)
};

/// Where a moving source is at `phase` (radians, one cycle = 2 pi), starting from `anchor`.
/// At phase 0 every pattern is exactly at the anchor, so switching a motion on never jumps.
/// The circle is not handled here: it turns the current position, so the azimuth slider
/// and dragging keep working while it runs.
rk::Vec3 motionPosition(Motion m, rk::Vec3 anchor, double phase);

/// Position of a DAW track as stored in a snapshot (tracks are matched by name).
struct DawTrackState {
    juce::String name;
    float azimuthDeg = 0.0f, elevationDeg = 0.0f, distance = 2.0f;
};

/// A stored state of the mix: every source, the room and the master level, plus the
/// positions of the DAW tracks. Recalling it fades there over `fadeSec`. The speaker
/// layout is not part of it.
struct Snapshot {
    juce::String name;
    float fadeSec = 2.0f;
    std::vector<SourceInfo> sources;
    rk::ReverbParams reverb;
    float reverbWetDb = -6.0f;
    float masterGainDb = 0.0f;
    std::vector<DawTrackState> daw;
};

constexpr int kMaxSnapshots = 64;

struct Scene {
    juce::String name;              // empty = untitled
    rk::SpeakerLayout layout = rk::presets::ring(8, 3.0f, 0.0f, true);
    juce::String presetId = "ring";
    // Parameters of the generated presets.
    int ringCount = 8;
    float radius = 3.0f;
    float height = 0.0f;
    int innerCount = 4;
    float innerRadius = 1.0f;
    int outerCount = 8;
    float outerRadius = 5.0f;
    bool autoPanner = true;
    rk::PannerKind panner = rk::PannerKind::VBAP;
    float dbapRolloffDb = 6.0f;
    float dbapBlur = 0.3f;
    float dbapDirectivity = 0.7f;
    rk::ReverbParams reverb;
    float reverbWetDb = -6.0f;
    float masterGainDb = 0.0f;
    bool limiter = true;
    float limiterCeilingDb = -0.1f; // dBFS
    float limiterReleaseMs = 80.0f;
    // Compressor over all speakers together (one gain for all, the image stays in place).
    bool compressor = false;
    float compThresholdDb = -18.0f;
    float compRatio = 3.0f;
    float compAttackMs = 10.0f;
    float compReleaseMs = 150.0f;
    float compKneeDb = 6.0f;
    float compMakeupDb = 0.0f;
    bool headphones = false;
    /// With headphones: true renders the sources binaurally around the head, independent of
    /// the speaker setup; false lets you listen to the speaker setup itself.
    bool binaural = false;
    /// With headphones: measured head responses from a SOFA file (full path) instead of the
    /// built-in head model. The app loads the file; the scene only remembers which one.
    bool hrtfMeasured = false;
    juce::String hrtfFile;
    std::vector<SourceInfo> sources;
    std::vector<Snapshot> snapshots;

    /// True when the speakers and panner settings equal those of `o` (what the views' panners
    /// are built from). Cheap; lets views skip rebuilding when only sources moved.
    bool samePanning(const Scene& o) const;
    /// The panner actually used (resolves "automatic").
    rk::PannerKind effectivePanner() const;
    rk::EngineConfig engineConfig() const;
};

constexpr int kMaxSceneSources = 64;

/// Preset ids in menu order and their display names (German, translate with tr()).
const juce::StringArray& presetIds();
juce::String presetName(const juce::String& id);
/// Regenerates scene.layout from scene.presetId and the preset parameters.
/// Returns false for "custom" (layout left untouched).
bool regenerateLayout(Scene& s);
/// Delay and level so that all speakers sound as if they were as far away as the farthest.
void alignDistances(rk::SpeakerLayout& l);

juce::var layoutToVar(const rk::SpeakerLayout& l);
rk::SpeakerLayout layoutFromVar(const juce::var& v);
juce::var sceneToVar(const Scene& s);
Scene sceneFromVar(const juce::var& v);

juce::String sceneToJson(const Scene& s);
/// Returns false (and leaves `out` untouched) if the text is not a Raumklang scene.
bool sceneFromJson(const juce::String& json, Scene& out);

/// Folder for settings, autosave and the setup shared between app and plugins.
juce::File appDataFolder();
/// Speaker setup published by the app, read by the plugins.
juce::File sharedSetupFile();
bool writeSharedSetup(const Scene& s);
/// Reads layout, panner and reverb from the shared file into `s` (sources untouched).
bool readSharedSetup(Scene& s);

/// Writes a file atomically (temp file + rename) so a crash never leaves half a file.
bool writeFileAtomically(const juce::File& f, const juce::String& text);

} // namespace rkui
