// Raumklang UI: the document model shared by the app and the plugins, and its
// JSON representation. Every value read from disk is validated and clamped, so a
// broken or hand-edited file can never crash the audio engine.
#pragma once

#include <juce_core/juce_core.h>
#include <rk/engine.hpp>

#include <vector>

namespace rkui {

enum class TestSignal { Off = 0, Pink = 1, Sine = 2, Clicks = 3 };

struct SourceInfo {
    juce::String name;
    int inputChannel = 0;           // device input (0-based)
    TestSignal test = TestSignal::Off;
    rk::Vec3 position{0.0f, 2.0f, 0.0f};
    float gainDb = 0.0f;
    float spread = 0.0f;
    float reverbSendDb = -12.0f;
    float distanceAmount = 1.0f;
    bool mute = false;
    bool solo = false;
    bool orbit = false;
    float orbitSpeed = 0.1f;        // revolutions per second
};

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
    bool headphones = false;
    std::vector<SourceInfo> sources;

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
