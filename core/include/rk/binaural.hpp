// Raumklang core: headphone preview. Folds loudspeaker feeds to two ears with a
// spherical head model (Brown & Duda 1998): interaural time difference plus a
// head-shadow shelving filter per ear. No measured HRTF data required.
#pragma once

#include "rk/layout.hpp"

#include <vector>

namespace rk {

class BinauralFold {
public:
    /// Off the audio thread.
    void prepare(const SpeakerLayout& layout, double sampleRate);
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
    };
    struct Channel {
        Ear ear[2];
        std::vector<float> history; // ring buffer of the speaker feed
        int pos = 0;
        bool active = false;
    };
    std::vector<Channel> ch_;
    int historyLen_ = 1;
};

} // namespace rk
