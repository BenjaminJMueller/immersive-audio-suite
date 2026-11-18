// Raumklang core: automatic loudspeaker calibration with a measuring microphone.
//
// How it works, in short:
//   1. Every speaker plays an exponential sine sweep, one after the other. A microphone
//      records it at one or more measuring points ("Messpunkte").
//   2. Deconvolution turns each recording into an impulse response. Its first strong peak is
//      the direct sound: the arrival time gives the distance, its energy gives the level.
//   3. With one measuring point, delays and levels are aligned at that point.
//      With four or more points (not all at one height) the speaker positions are found by
//      multilateration: a robust least-squares fit that also solves the unknown latency of
//      the audio interface and corrects slightly misplaced measuring points.
//
// Everything here is plain C++ without allocations in SweepRunner::process(), so the audio
// thread part is realtime safe; the analysis runs on any other thread.
#pragma once

#include "rk/layout.hpp"

#include <atomic>
#include <complex>
#include <optional>
#include <string>
#include <vector>

namespace rk::calib {

inline constexpr int kMaxPoints = 16;

/// Speed of sound in m/s for an air temperature in °C (clamped to -20..50 °C).
float speedOfSound(float celsius);

// ---------------------------------------------------------------------------------------------
// Measurement signal

struct SweepParams {
    double sampleRate = 48000.0;
    double f1 = 50.0;         // start frequency (Hz)
    double f2 = 20000.0;      // end frequency (Hz), limited to 0.45 * sample rate
    double seconds = 1.5;     // sweep length
    double fadeSeconds = 0.03;
    double tailSeconds = 0.9; // recorded after the sweep: room decay + interface latency
    float levelDb = -18.0f;   // peak level of the sweep (dBFS)
};

/// Clamps every field into a safe range (finite, f1 < f2 < Nyquist, ...).
SweepParams sanitize(SweepParams p);
/// Exponential (logarithmic) sine sweep with raised-cosine fades.
std::vector<float> makeSweep(const SweepParams& p);
/// Number of samples to record for one sweep (sweep + tail).
int recordLength(const SweepParams& p);

// ---------------------------------------------------------------------------------------------
// Audio thread: plays the sweep on one output and records one or two inputs.

class SweepRunner {
public:
    enum class State : int { Idle = 0, Armed = 1, Running = 2, Done = 3 };

    /// Message thread, while Idle or Done. Allocates the buffers.
    void prepare(const std::vector<float>& sweep, int recordSamples);
    /// Message thread. Starts playing `sweep` on output `outChannel` and recording input
    /// `inChannel` (and `loopbackChannel`, -1 = none). Returns false if not prepared or busy.
    bool start(int outChannel, int inChannel, int loopbackChannel = -1);
    /// Message thread: abandons a running measurement (the audio thread stops at its next block).
    void cancel();

    /// Audio thread. Overwrites all outputs while a measurement runs (silence + sweep) and
    /// returns true; returns false (and touches nothing) when idle. No allocations, no locks.
    bool process(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                 int numSamples) noexcept;

    State state() const noexcept { return static_cast<State>(state_.load(std::memory_order_acquire)); }
    bool busy() const noexcept { const State s = state(); return s == State::Armed || s == State::Running; }
    /// Message thread, when Done: the recordings. Resets the runner to Idle.
    bool take(std::vector<float>& recording, std::vector<float>& loopback);
    /// Highest absolute input sample of the last run (clipping check).
    float inputPeak() const noexcept { return inputPeak_.load(std::memory_order_relaxed); }
    /// 0..1 while running.
    float progress() const noexcept;

private:
    std::vector<float> sweep_, rec_, loop_;
    int recLen_ = 0;
    int out_ = 0, in_ = 0, loopIn_ = -1;
    int pos_ = 0;
    std::atomic<int> state_{0};
    std::atomic<int> posPublic_{0};
    std::atomic<bool> cancel_{false};
    std::atomic<float> inputPeak_{0.0f};
};

// ---------------------------------------------------------------------------------------------
// Analysis

/// Impulse response from a recording of the sweep (regularised spectral division).
/// The result has `length` samples; sample k is the response k samples after the sweep started.
std::vector<float> deconvolve(const std::vector<float>& recording, const std::vector<float>& sweep,
                              const SweepParams& p, int length);

struct Arrival {
    bool valid = false;
    double seconds = 0.0;   // arrival of the direct sound after the sweep started
    float levelDb = -160.0f; // energy of the direct sound (relative dB)
    float snrDb = 0.0f;     // direct peak over the noise floor
    float directness = 0.0f; // direct peak / strongest peak, 1 = clean line of sight
    int sample = 0;
};

/// Finds the direct sound in an impulse response. `maxSeconds` limits the search window and
/// should not exceed the recorded tail (the end of the window serves as the noise reference).
Arrival findArrival(const std::vector<float>& ir, double sampleRate, double maxSeconds = 1.0);

/// Convenience: deconvolve + findArrival. `inputPeak` > 0.99 marks the result as clipped.
struct Measurement {
    Arrival arrival;
    bool clipped = false;
    bool tooQuiet = false;
};
Measurement analyse(const std::vector<float>& recording, const std::vector<float>& sweep, const SweepParams& p);

// ---------------------------------------------------------------------------------------------
// Geometry

struct Observation {
    int speaker = 0;
    int point = 0;
    double seconds = 0.0; // arrival time (including the unknown interface latency)
    float levelDb = 0.0f;
    float weight = 1.0f;  // 0..1, e.g. from SNR and line of sight
};

/// A distance measured by hand (e.g. with a tape measure) from a measuring point to a speaker.
struct DistanceHint {
    int speaker = 0;
    int point = 0;
    float metres = 0.0f;
};

struct SolveInput {
    std::vector<Vec3> speakerGuess;  // current layout positions (rough is fine)
    std::vector<Vec3> pointGuess;    // where the microphone was placed
    std::vector<Observation> observations;
    std::vector<DistanceHint> distances;
    float speedOfSound = 343.0f;
    float pointSigma = 0.10f;   // how exactly the microphone was placed (m)
    float speakerSigma = 4.0f;  // how much the layout guess is trusted (m), weak on purpose
    float timeSigma = 50.0e-6f; // timing precision (s), about 1.7 cm
    /// Known interface latency (from a loopback cable), otherwise solved.
    std::optional<double> knownLatency;
    double latencyGuess = 0.0;  // e.g. the latency the driver reports
    double latencySigma = 0.02;
    int maxIterations = 200;
};

struct SpeakerFit {
    Vec3 position;
    bool solved = false;     // enough measurements to place it
    int points = 0;          // measuring points that heard it
    float uncertainty = 0.0f; // estimated 1-sigma position error (m)
    float rmsError = 0.0f;   // fit residual (m)
};

struct SolveResult {
    std::vector<SpeakerFit> speakers;
    std::vector<Vec3> points;  // refined measuring points
    double latency = 0.0;
    float rmsError = 0.0f;     // over all observations (m)
    int iterations = 0;
    bool converged = false;
};

/// Robust multilateration (Levenberg-Marquardt with Huber weights). Never throws; invalid input
/// gives an empty or unsolved result.
SolveResult solvePositions(const SolveInput& in);

/// True if the points span all three dimensions (needed to resolve heights).
bool pointsSpanVolume(const std::vector<Vec3>& points, float minExtent = 0.2f);

/// Suggested measuring points for a layout: inside the listening area, one of them raised so the
/// heights can be resolved. For a "donut" they go round the ring between the speaker rings.
std::vector<Vec3> suggestPoints(const SpeakerLayout& layout, int count);

// ---------------------------------------------------------------------------------------------
// Delay and level alignment

struct AlignInput {
    int numSpeakers = 0;
    std::vector<Observation> observations;
    /// Point at which delay and level are aligned (index into the points).
    int referencePoint = 0;
    /// Optional solved geometry: if set, levels from all points are combined (1/r law) and the
    /// delays follow from the solved distances. Otherwise only the reference point is used.
    const SolveResult* geometry = nullptr;
    Vec3 referencePosition{};
    float speedOfSound = 343.0f;
    std::vector<bool> isSubwoofer;
};

struct SpeakerAlign {
    bool measured = false;
    float delayMs = 0.0f;
    float gainDb = 0.0f;
    float levelDb = -160.0f;     // level at the reference point before correction
    float distance = 0.0f;       // to the reference point (m), 0 if unknown
    bool veryQuiet = false;      // much quieter than the others: not connected? facing away?
};

std::vector<SpeakerAlign> align(const AlignInput& in);

// ---------------------------------------------------------------------------------------------
// Matching measured positions to the speakers of a layout (e.g. after an iPhone survey)

/// Optimal one-to-one assignment (Hungarian method) minimising the total distance.
/// result[i] = index into `measured` for speaker i, or -1.
std::vector<int> matchByDistance(const std::vector<Vec3>& speakers, const std::vector<Vec3>& measured);

/// Speakers whose measured position is clearly nearer another speaker's planned position:
/// pairs (i, j) "speaker i sounds where j should be", a hint for swapped cables.
std::vector<std::pair<int, int>> suspectedSwaps(const std::vector<Vec3>& planned, const std::vector<Vec3>& measured,
                                                const std::vector<bool>& valid);

} // namespace rk::calib
