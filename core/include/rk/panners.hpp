// Raumklang core: panning laws (VBAP, DBAP, higher order Ambisonics).
//
// All panners follow the same contract:
//   * prepare(...) is called off the audio thread and may allocate.
//   * compute(...) is realtime-safe: no allocation, no locks, bounded time.
//   * compute writes one amplitude gain per layout speaker (disabled speakers and
//     subwoofers get 0) and the gains are energy-normalised (sum of squares == 1),
//     unless every candidate speaker is disabled, in which case all gains are 0.
#pragma once

#include "rk/layout.hpp"

#include <array>
#include <vector>

namespace rk {

struct PanInput {
    Vec3 position{0.0f, 1.0f, 0.0f}; // metres, listener at origin
    float spread = 0.0f;             // 0 = point source, 1 = maximally diffuse
};

/// Vector Base Amplitude Panning (Pulkki 1997) with automatic 2D/3D selection,
/// imaginary speakers for incomplete domes and MDAP spread.
class VbapPanner {
public:
    void prepare(const SpeakerLayout& layout);
    /// `dir` does not need to be normalised. Writes layout.speakers.size() gains.
    void compute(const PanInput& in, float* gains) const noexcept;

    /// Direction-only panning without spread; used internally and by the Ambisonics decoder.
    void computeDirection(Vec3 unitDir, float* gains) const noexcept;

    bool is2D() const noexcept { return flat_; }
    int numTriangles() const noexcept { return static_cast<int>(tris_.size()); }
    int numPairs() const noexcept { return static_cast<int>(pairs_.size()); }

private:
    struct Pair { int a, b; float inv[4]; };
    struct Tri { int a, b, c; float inv[9]; };

    void addUniqueGain(int dirIndex, float g, float* gains) const noexcept;
    void build2D();
    void build3D();

    int numSpeakers_ = 0;
    bool flat_ = true;
    std::vector<Vec3> dirs_;               // unique real directions, then imaginary ones
    int numReal_ = 0;
    std::vector<std::vector<int>> owners_; // speakers sharing each real direction
    std::vector<float> azimuths_;          // 2D: azimuth (rad) per real dir
    std::vector<Pair> pairs_;
    std::vector<Tri> tris_;
};

/// Distance Based Amplitude Panning (Lossius 2009) extended by speaker orientation,
/// so that rings of speakers facing each other ("donut") work naturally.
class DbapPanner {
public:
    struct Params {
        float rolloffDb = 6.0f;      // attenuation per doubling of distance (3..12)
        float blur = 0.3f;           // spatial blur in metres
        float directivity = 0.7f;    // 0 = ignore speaker orientation, 1 = full cardioid weighting
    };

    void prepare(const SpeakerLayout& layout, const Params& params);
    void compute(const PanInput& in, float* gains) const noexcept;

private:
    struct Spk { Vec3 pos; Vec3 aim; int index; };
    std::vector<Spk> spk_;
    int numSpeakers_ = 0;
    Params params_;
    float exponent_ = 1.0f;
    float layoutRadius_ = 1.0f;
};

inline constexpr int kMaxAmbiOrder = 3;
inline constexpr int kMaxAmbiChannels = (kMaxAmbiOrder + 1) * (kMaxAmbiOrder + 1);

/// Real spherical harmonics, ACN channel order, SN3D normalisation (AmbiX).
/// `dir` must be a unit vector in Raumklang coordinates (x right, y front, z up).
void encodeSN3D(Vec3 dir, int order, float* out) noexcept;

/// max-rE weight for degree n of an order-N decoder.
float maxReWeight(int n, int order) noexcept;

/// Higher order Ambisonics, rendered via an AllRAD decoder (Zotter & Frank 2012).
/// Per-source gains are D * Y(dir), i.e. encoder and decoder collapsed into one step.
class AmbisonicsPanner {
public:
    void prepare(const SpeakerLayout& layout, int order);
    void compute(const PanInput& in, float* gains) const noexcept;
    /// Decodes one frame of ACN/SN3D ambisonics to speakers (for ambisonic inputs).
    void decode(const float* ambi, float* speakers) const noexcept;
    int order() const noexcept { return order_; }
    int numChannels() const noexcept { return (order_ + 1) * (order_ + 1); }

private:
    int order_ = 1;
    int numSpeakers_ = 0;
    std::vector<float> decoder_; // numSpeakers x kMaxAmbiChannels, row-major
    VbapPanner vbap_;
};

/// Holds every panner kind and dispatches to the selected one.
class Panner {
public:
    void prepare(const SpeakerLayout& layout, PannerKind kind, const DbapPanner::Params& dbap = {},
                 int ambiOrder = 3);
    void compute(const PanInput& in, float* gains) const noexcept;
    PannerKind kind() const noexcept { return kind_; }
    int numSpeakers() const noexcept { return numSpeakers_; }

private:
    PannerKind kind_ = PannerKind::VBAP;
    int numSpeakers_ = 0;
    VbapPanner vbap_;
    DbapPanner dbap_;
    AmbisonicsPanner ambi_;
};

} // namespace rk
