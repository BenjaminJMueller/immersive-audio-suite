// Raumklang core: loudspeaker layouts and presets.
#pragma once

#include "rk/math.hpp"

#include <string>
#include <vector>

namespace rk {

inline constexpr int kMaxSpeakers = 128;
inline constexpr int kMaxSources = 256;
/// Input channels the engine accepts (device inputs, test signals and DAW tracks together).
inline constexpr int kMaxInputs = 512;

struct Speaker {
    std::string name;
    Vec3 position{0.0f, 2.0f, 0.0f};
    /// Direction the speaker points to. Zero vector means "towards the listener origin".
    Vec3 aim{0.0f, 0.0f, 0.0f};
    float gainDb = 0.0f;   // calibration trim, -60..+12 dB
    float delayMs = 0.0f;  // calibration delay, 0..500 ms
    int group = 0;         // free grouping, e.g. 0 = inner ring, 1 = outer ring
    bool isSubwoofer = false;
    bool enabled = true;
    /// Device output (0-based) the speaker plays on. kOutputAuto = same as its number,
    /// kOutputNone = rendered (also for the DAW) but not sent to the audio interface.
    int output = -1;
    static constexpr int kOutputAuto = -1;
    static constexpr int kOutputNone = -2;
    /// The output for speaker number `index` (0-based), -1 for none.
    int outputFor(int index) const noexcept { return output == kOutputNone ? -1 : output >= 0 ? output : index; }

    /// The effective aim direction (unit vector).
    Vec3 aimDirection() const;
};

struct SpeakerGroup {
    std::string name;
};

struct SpeakerLayout {
    std::string name;
    std::vector<Speaker> speakers;
    std::vector<SpeakerGroup> groups;
};

/// Recommended panning method for a layout.
enum class PannerKind : int {
    VBAP = 0,       // vector based amplitude panning (listener in the centre)
    DBAP = 1,       // distance based amplitude panning (any layout, listener anywhere)
    Ambisonics = 2, // higher order ambisonics, AllRAD decoder
    count
};

const char* pannerId(PannerKind k);

struct LayoutInfo {
    int numActive = 0;          // enabled, non-sub speakers
    int numSubs = 0;
    bool isFlat = false;        // all main speakers roughly at one height
    bool surroundsOrigin = false; // speakers enclose the listener position horizontally
    bool facesInwardAndOutward = false; // speakers point towards each other ("donut")
    float maxRadius = 0.0f;
    PannerKind recommended = PannerKind::DBAP;
    /// Plain-language hints (German keys resolved in UI). Kept as stable ids.
    std::vector<std::string> hints;
};

/// Clamps every value into a safe range, fixes NaNs, drops speakers beyond kMaxSpeakers.
/// Returns true if anything had to be changed.
bool sanitize(SpeakerLayout& layout);

/// Analyses geometry and suggests a panning method.
LayoutInfo analyze(const SpeakerLayout& layout);

namespace presets {

SpeakerLayout stereo(float radius = 2.0f);
SpeakerLayout quad(float radius = 2.0f);
SpeakerLayout surround51(float radius = 2.0f);
SpeakerLayout surround714(float radius = 2.0f);
/// N speakers evenly on a circle, first speaker at front (or offset by half a step).
SpeakerLayout ring(int count, float radius, float height = 0.0f, bool halfStepOffset = false);
/// Hemisphere: rings with the given counts from ear level upwards plus optional zenith speaker.
SpeakerLayout dome(const std::vector<int>& ringCounts, float radius, bool zenith);
/// Cube with 8 corners.
SpeakerLayout cube(float halfSize = 2.0f);
/// "Donut": an inner ring facing outwards and an outer ring facing inwards. The audience
/// sits in the ring-shaped area between them (e.g. around a central column).
SpeakerLayout donut(int innerCount, float innerRadius, int outerCount, float outerRadius,
                    float height = 1.5f);

struct PresetInfo {
    const char* id;
    SpeakerLayout (*make)();
};
/// All built-in presets with default parameters, in menu order.
const std::vector<PresetInfo>& all();

} // namespace presets

} // namespace rk
