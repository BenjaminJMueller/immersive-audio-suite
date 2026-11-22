// Raumklang core: headphone preview. Folds loudspeaker feeds to two ears with the
// structural head model of Brown & Duda (1998): interaural time difference and a
// head-shadow filter per ear, plus pinna echoes whose delays depend on azimuth and
// elevation (front/back and height cues) and a gentle treble cut for sounds from
// behind. No measured HRTF data required.
#pragma once

#include "rk/layout.hpp"

#include <vector>

namespace rk {

/// Virtual speakers for binaural listening that does not depend on the real setup:
/// 19 directions around the head (8 at ear level, 6 above, 4 below, 1 overhead).
SpeakerLayout binauralVirtualLayout();

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
    std::vector<Channel> ch_;
    int historyLen_ = 1;
};

} // namespace rk
