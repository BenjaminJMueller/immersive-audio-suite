// Raumklang core: the bridge between the "Quelle" plugin and the Raumklang app.
//
// Each plugin instance claims a slot in a block of shared memory and writes its track
// audio and position into it. The app reads every slot in its own audio callback and
// spatialises the tracks there. No virtual audio device and no routing software is
// involved, and it works in every DAW, also those without multichannel tracks.
//
// Safety rules
//   * The shared memory is treated as hostile: every index, size and sample read from
//     it is checked, so a crashed or misbehaving peer can never crash this process.
//   * Sender::write() and Receiver::read() run on audio threads: no allocation, no
//     locks, no system calls. Everything else runs on a normal thread.
//   * Both clocks (DAW device and app device) drift apart slowly. The receiver keeps
//     its read position at a target distance behind the writer and adjusts its read
//     rate by at most 0.5 % with interpolation, so drift never causes clicks.
#pragma once

#include "rk/math.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rk {
class Engine;
}

namespace rk::bridge {

inline constexpr uint32_t kMagic = 0x31424b52u; // "RKB1"
inline constexpr uint32_t kVersion = 2; // 2: the app can move a track
inline constexpr uint32_t kControlMagic = 0x31434b52u; // "RKC1"
inline constexpr int kSlots = 32;
inline constexpr int kMaxChannels = 2;
inline constexpr int kRingFrames = 16384; // power of two, about 340 ms at 48 kHz
inline constexpr int kNameWords = 16;     // 64 bytes of UTF-8
inline constexpr int64_t kStaleMs = 3000; // a peer that has not signalled for this long is gone

/// Default name of the shared memory block. Tests use their own names.
const char* defaultName();

/// Milliseconds on a clock that is the same for all processes on this machine.
int64_t nowMs();

/// Position and mix values of one track, as set in the plugin.
struct TrackParams {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 2.0f;
    float spread = 0.0f;          // 0..1
    float gainDb = 0.0f;
    float reverbSendDb = -12.0f;
    float stereoWidthDeg = 60.0f; // angle between left and right of a stereo track
    float distanceAmount = 1.0f;  // 0..2
    /// Mute and solo as set in the plugin. They travel in the control block, so the
    /// audio block keeps its layout and older apps simply ignore them.
    bool mute = false;
    bool solo = false;
    /// Clamps every value into its range; non-finite values become the defaults.
    TrackParams sanitized() const noexcept;
};

/// Sets engine sources `first` and `first + 1` (stereo) from a track. Realtime safe.
/// `inputBase` is the engine input of the track's first channel.
void applyTrack(Engine& engine, int first, int inputBase, int channels, const TrackParams& p, bool active) noexcept;

struct Slot;
struct Shared;
struct ReturnShared;
struct ControlShared;

/// Mix values the user changed for a DAW track in the app (app -> plugin).
/// Only the values whose bit is set in `mask` were changed.
struct MixChange {
    enum : uint32_t {
        Gain = 1u, Spread = 2u, ReverbSend = 4u, StereoWidth = 8u, DistanceAmount = 16u,
        Mute = 32u, Solo = 64u, // the new state is in `mute` / `solo`
        All = 127u
    };
    uint32_t mask = 0;
    float gainDb = 0.0f;
    float spread = 0.0f;          // 0..1
    float reverbSendDb = -12.0f;
    float stereoWidthDeg = 60.0f;
    float distanceAmount = 1.0f;  // 0..2
    bool mute = false;
    bool solo = false;
};

// App -> plugin mix changes live in a second, small block named "<bridge name>-ctl". A
// separate block keeps the layout of the audio block unchanged, so older plugins keep working.

/// Read position of one drift-corrected ring reader (audio thread state).
struct DriftState {
    bool running = false;
    double readPos = 0.0;
    double smoothedError = 0.0;
    uint64_t lastWrite = 0;
    /// Write position when the reader last ran dry: it only starts again on audio
    /// written after that, never by replaying what it already played.
    uint64_t resumeFrom = 0;
    int idle = 0;      // callbacks in a row without new audio
    float fade = 0.0f;
    /// Extra frames kept in reserve. Grows after each underrun (the writer or the reader was
    /// late, e.g. while the system is busy with window animations), so it does not happen again.
    double reserve = 0.0;
};

/// A named block of memory shared between processes (POSIX shm or a Windows file mapping).
class SharedMemory {
public:
    SharedMemory() = default;
    ~SharedMemory();
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;
    /// Opens the block, creating it zero-filled if it does not exist yet.
    bool open(const std::string& name, size_t size);
    void close();
    void* data() const noexcept { return data_; }
    size_t size() const noexcept { return size_; }
    /// Removes the name (POSIX only; the memory lives on while mapped). For tests.
    static void unlink(const std::string& name);

private:
    void* data_ = nullptr;
    size_t size_ = 0;
    void* handle_ = nullptr; // Windows mapping handle
};

/// Size of the shared block in bytes.
size_t sharedSize();

/// Plugin side.
class Sender {
public:
    Sender();
    ~Sender();
    /// Any thread except audio. Returns false if shared memory is unavailable (e.g. sandbox).
    bool open(const std::string& name = defaultName());
    bool isOpen() const noexcept { return shared_ != nullptr; }
    /// Claims a free (or abandoned) slot. Returns the slot index or -1.
    int claim();
    void release();
    int slot() const noexcept { return slot_.load(std::memory_order_acquire); }
    /// Call about once per second from a normal thread: keeps the slot alive.
    void heartbeat(const std::string& trackName);
    /// True if the app has signalled recently.
    bool appRunning() const noexcept;
    double appSampleRate() const noexcept;

    /// Audio thread. Writes one block of track audio (1 or 2 channels) and the parameters.
    void write(const float* const* channels, int numChannels, int numFrames, double sampleRate,
               const TrackParams& params) noexcept;

    /// Normal thread: true once per new position the user dragged the track to in the app.
    /// The plugin applies it to its own parameters, so the DAW can record it as automation.
    bool takeMove(float& azimuthDeg, float& elevationDeg, float& distance) noexcept;
    /// Normal thread: true if the user changed mix values of this track in the app. Values
    /// are sanitised; the plugin applies those in `out.mask` to its own parameters.
    bool takeMix(MixChange& out) noexcept;

private:
    SharedMemory mem_;
    SharedMemory controlMem_;
    Shared* shared_ = nullptr;
    ControlShared* control_ = nullptr;
    std::atomic<int> slot_{-1};
    uint64_t token_ = 0;
    uint32_t moveSeen_ = 0;
};

/// What the app knows about one slot (UI thread).
struct TrackInfo {
    int slot = -1;
    std::string name;
    int channels = 1;
    TrackParams params;
    bool sending = false;            // audio arrived recently
    bool sampleRateMismatch = false; // DAW and app run at different rates
    double sampleRate = 0.0;
};

/// App side.
class Receiver {
public:
    Receiver();
    ~Receiver();
    bool open(const std::string& name = defaultName());
    bool isOpen() const noexcept { return shared_ != nullptr; }
    /// Normal thread, regularly: tells plugins that the app runs and at which rate.
    void heartbeat(double sampleRate);
    /// Normal thread: the slots that currently belong to a plugin.
    std::vector<TrackInfo> scan() const;
    /// Normal thread: asks the plugin in `slot` to move its track (the user dragged it in the app).
    bool requestMove(int slot, float azimuthDeg, float elevationDeg, float distance) noexcept;
    /// Normal thread: asks the plugin in `slot` to take over the mix values in `change.mask`.
    bool requestMix(int slot, const MixChange& change) noexcept;

    /// Audio thread, once per callback before read(): resets state if the rate changed.
    void beginBlock(double sampleRate) noexcept;
    /// Audio thread. Reads `numFrames` of slot `slot` into out[0] and out[1] (both must be
    /// valid). Returns the track's channel count (1 or 2), or 0 if the slot is silent; the
    /// outputs are always written (silence when nothing arrives). `params` receives the
    /// track parameters.
    int read(int slot, float* const* out, int numFrames, TrackParams& params) noexcept;

    /// Diagnostics (any thread): number of times a track ran dry or had to resynchronise.
    uint32_t takeDropouts() noexcept { return dropouts_.exchange(0); }

private:
    struct ReadState : DriftState {
        uint64_t owner = 0;
    };
    SharedMemory mem_;
    SharedMemory controlMem_;
    Shared* shared_ = nullptr;
    ControlShared* control_ = nullptr;
    double sampleRate_ = 48000.0;
    std::array<ReadState, kSlots> state_{};
    std::atomic<uint32_t> dropouts_{0};
};

// ---- return path: app -> DAW ("Raumklang Rückweg") ---------------------------------
// The app writes its rendered speaker signals into a second block; any number of
// return plugins read from it (each from its own first channel), for recording and
// real-time bounces in the DAW.

inline constexpr uint32_t kReturnMagic = 0x31524b52u; // "RKR1"
inline constexpr int kReturnChannels = 64;
const char* defaultReturnName();
size_t returnSize();

/// App side.
class ReturnSender {
public:
    ReturnSender();
    ~ReturnSender();
    bool open(const std::string& name = defaultReturnName());
    bool isOpen() const noexcept { return shared_ != nullptr; }
    /// Normal thread, regularly.
    void heartbeat(double sampleRate);
    /// True if a return plugin asked for audio recently (the app writes only then).
    bool listening() const noexcept;
    /// Audio thread: the rendered output, `numChannels` (clamped to kReturnChannels).
    void write(const float* const* channels, int numChannels, int numFrames) noexcept;

private:
    SharedMemory mem_;
    ReturnShared* shared_ = nullptr;
};

/// Plugin side.
class ReturnReceiver {
public:
    ReturnReceiver();
    ~ReturnReceiver();
    bool open(const std::string& name = defaultReturnName());
    bool isOpen() const noexcept { return shared_ != nullptr; }
    /// Normal thread, about once per second: tells the app that someone listens.
    void heartbeat();
    bool appRunning() const noexcept;
    double appSampleRate() const noexcept;
    /// Number of channels the app currently renders.
    int appChannels() const noexcept;
    /// Audio thread. Reads app channels [firstChannel, firstChannel + numOut) into `out`
    /// (all outputs are always written). Returns the number of channels with audio.
    int read(int firstChannel, float* const* out, int numOut, int numFrames, double sampleRate) noexcept;
    uint32_t takeDropouts() noexcept { return dropouts_.exchange(0); }

private:
    SharedMemory mem_;
    ReturnShared* shared_ = nullptr;
    DriftState state_;
    double rate_ = 0.0;
    std::atomic<uint32_t> dropouts_{0};
};

} // namespace rk::bridge
