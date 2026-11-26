// Raumklang core: headphone preview. Folds loudspeaker feeds to two ears with the
// structural head model of Brown & Duda (1998): interaural time difference and a
// head-shadow filter per ear, plus pinna echoes whose delays depend on azimuth and
// elevation (front/back and height cues) and a gentle treble cut for sounds from
// behind. No measured HRTF data required.
//
// With a measured HRTF set (for example from a SOFA file) each speaker is convolved with
// the measured head-related impulse responses of the nearest measured direction instead.
// The convolution runs in blocks of kBlock samples (uniformly partitioned, overlap-save),
// which adds kBlock samples of latency.
#pragma once

#include "rk/layout.hpp"

#include <complex>
#include <string>
#include <vector>

namespace rk {

/// Virtual speakers for binaural listening that does not depend on the real setup:
/// 19 directions around the head (8 at ear level, 6 above, 4 below, 1 overhead).
SpeakerLayout binauralVirtualLayout();

/// Measured head-related impulse responses, one pair per direction.
struct HrirSet {
    double sampleRate = 48000.0;
    int length = 0;                 // samples per impulse response
    std::vector<Vec3> directions;   // unit vectors in Raumklang coordinates
    std::vector<float> left, right; // directions.size() * length samples each
    std::string name;
    bool valid() const noexcept;
    /// Call once after filling: removes the silence before the earliest onset (keeping the
    /// time differences between directions and ears), limits the length to 20 ms and
    /// scales the set to an average energy of 1 per ear, so it plays about as loud as the
    /// head model. Returns false if the data is unusable (empty, silent or not finite).
    bool finalize();
    /// Index of the measured direction closest to `direction`.
    int nearest(Vec3 direction) const noexcept;
};

class BinauralFold {
public:
    static constexpr int kBlock = 64;

    /// Off the audio thread. With `hrirs` (valid, not null) the measured responses are used;
    /// the fold keeps its own copy, so the set may be released afterwards.
    void prepare(const SpeakerLayout& layout, double sampleRate, const HrirSet* hrirs = nullptr);
    /// Samples of delay the measured mode adds (0 with the head model).
    int latency() const noexcept { return measured_ ? kBlock : 0; }
    bool measured() const noexcept { return measured_; }
    /// Realtime-safe. `speakers[m]` holds n samples for each layout speaker.
    /// Overwrites left/right.
    void process(const float* const* speakers, int numSpeakers, float* left, float* right, int n) noexcept;
    void reset() noexcept;

private:
    struct Ear {
        float b0 = 1, b1 = 0, a1 = 0; // first-order head shadow
        float z = 0;                  // filter state
        float delay = 0;              // samples (fractional)
        float gain = 1;
        float pinnaDelay[5] = {};     // extra samples after `delay`
        float rearA = 0, rearG = 0;   // one-pole split and treble gain change (0 = none)
        float rearLp = 0;             // filter state
    };
    struct Channel {
        Ear ear[2];
        std::vector<float> history; // ring buffer of the speaker feed
        int pos = 0;
        bool active = false;
    };
    float tap(const Channel& c, float delay) const noexcept;
    void prepareMeasured(const SpeakerLayout& layout, double sampleRate, const HrirSet& set);
    void processMeasured(const float* const* speakers, int numSpeakers, float* left, float* right, int n) noexcept;
    void runBlock() noexcept;
    std::vector<Channel> ch_;
    int historyLen_ = 1;

    // ---- measured HRIRs (uniformly partitioned convolution) ----
    using cpx = std::complex<float>;
    static constexpr int kFft = 2 * kBlock;
    static constexpr int kBins = kBlock + 1;
    struct Conv {
        std::vector<float> time;  // last two blocks of input (kFft)
        std::vector<cpx> h[2];    // partitions * kBins, per ear
        bool active = false;
    };
    bool measured_ = false;
    int partitions_ = 0;
    int fill_ = 0;
    int fdlPos_ = 0;
    std::vector<Conv> conv_;
    std::vector<cpx> fdl_;        // speakers * partitions * kBins: past input spectra
    std::vector<cpx> work_;       // kFft
    std::vector<cpx> acc_[2];     // kBins
    std::vector<float> out_[2];   // kBlock, played during the next block
    std::vector<cpx> twiddle_;    // kFft / 2
    std::vector<int> bitrev_;     // kFft
    void fft(cpx* x, bool inverse) const noexcept;
};

} // namespace rk
