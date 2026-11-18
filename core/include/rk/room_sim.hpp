// Raumklang core: a simple simulated room for testing the speaker calibration end to end
// (and for the app's test mode without hardware). Each speaker reaches the microphone over a
// direct path and a floor reflection, with 1/r attenuation, speaker directivity, the latency of
// an audio interface and a little noise. process() is realtime safe.
#pragma once

#include "rk/math.hpp"

#include <array>
#include <atomic>
#include <vector>

namespace rk {

class RoomSimulator {
public:
    struct Speaker {
        Vec3 position;
        Vec3 aim; // unit vector, zero = omnidirectional
    };

    /// Message thread. `floorZ`: height of the floor relative to the listener's ears.
    void prepare(double sampleRate, const std::vector<Speaker>& speakers, double latencySeconds, float floorZ = -1.2f,
                 float noiseRms = 2.0e-5f, float speedOfSound = 343.0f);
    /// Any thread: where the microphone stands now.
    void setMicrophone(Vec3 p) noexcept;
    /// Audio thread: speaker signals in, microphone signal out. Never allocates.
    void process(const float* const* speakerSignals, int numSpeakers, float* mic, int numSamples) noexcept;

    static constexpr int kHistory = 1 << 16; // samples of history per speaker
    static constexpr int kTaps = 24;

private:
    void updateTaps() noexcept;

    double sr_ = 48000.0;
    double latency_ = 0.0;
    float floorZ_ = -1.2f, noise_ = 0.0f, c_ = 343.0f;
    std::vector<Speaker> speakers_;
    std::vector<float> history_; // speakers * kHistory
    int write_ = 0;
    struct Path {
        int base = 0; // delay in samples (integer part, minus half the taps)
        std::array<float, kTaps> taps{};
    };
    std::vector<Path> paths_; // 2 per speaker
    std::atomic<float> mx_{0.0f}, my_{0.0f}, mz_{0.0f};
    std::atomic<int> micVersion_{0};
    int seenVersion_ = -1;
    uint32_t rng_ = 0x12345678u;
};

} // namespace rk
