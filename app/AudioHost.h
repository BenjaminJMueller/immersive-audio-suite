// Raumklang app: owns the audio device and the engine. Device inputs and the built-in
// test signals feed the sources, the engine output goes straight to the device outputs.
// No virtual audio cables or routing tools are needed.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <rk/bridge.hpp>
#include <rk/calibration.hpp>
#include <rk/dsp.hpp>
#include <rk/engine.hpp>
#include <rk/room_sim.hpp>

#include <array>
#include <atomic>
#include <thread>

namespace rkapp {

class AudioHost : public juce::AudioIODeviceCallback {
public:
    static constexpr int kMaxDeviceInputs = 64;
    static constexpr int kMaxGenerated = 64; // one generator per source
    static constexpr int kGeneratorBase = kMaxDeviceInputs;
    /// Tracks from the DAW plugin "Raumklang Quelle": slot k uses inputs kBridgeInputBase + 2k (+1)
    /// and engine sources kBridgeSourceBase + 2k (+1). The scene keeps sources below kBridgeSourceBase.
    static constexpr int kBridgeInputBase = kGeneratorBase + kMaxGenerated;
    static constexpr int kBridgeSourceBase = 64;
    static constexpr int kTotalInputs = kBridgeInputBase + rk::bridge::kSlots * 2;
    static_assert(kBridgeSourceBase + rk::bridge::kSlots * 2 <= rk::kMaxSources, "bridge sources must fit the engine");

    AudioHost();
    ~AudioHost() override;

    /// Opens the default device (or the one stored in `savedState`). Returns an error text or empty.
    /// Opens the device. Without `withInputs` no input is opened, so macOS does not
    /// ask for microphone access (test signals and DAW tracks need none).
    juce::String start(const juce::XmlElement* savedState, bool withInputs);
    /// Message thread: opens (or closes) the inputs of the current device.
    juce::String setInputsEnabled(bool on);
    bool inputsEnabled() const noexcept { return inputsEnabled_; }
    void stop();

    rk::Engine engine;
    juce::AudioDeviceManager devices;
    /// Test signal per source: 0 off, 1 pink noise, 2 sine, 3 clicks.
    std::array<std::atomic<int>, kMaxGenerated> testSignal{};

    int numDeviceOutputs() const noexcept { return numOutputs_.load(); }
    int numDeviceInputs() const noexcept { return numInputs_.load(); }
    double sampleRate() const noexcept { return sampleRate_.load(); }
    /// Message thread: sample rate and output latency of the open device, like "48 kHz, 10,7 ms latency".
    /// Empty when no device is open.
    juce::String deviceSummary();
    juce::String takeError();

    /// Plugin bridge (message thread): announce the app and list the connected DAW tracks.
    bool bridgeAvailable() const noexcept { return receiver_.isOpen(); }
    std::vector<rk::bridge::TrackInfo> scanBridge();
    uint32_t takeBridgeDropouts() noexcept { return receiver_.takeDropouts(); }
    /// Latest parameters of a DAW track, as the audio thread applied them.
    rk::bridge::TrackParams bridgeParams(int slot) const noexcept;
    /// The user dragged a DAW track in the app: the track's plugin takes the new position.
    bool requestBridgeMove(int slot, float azimuthDeg, float elevationDeg, float distance) noexcept {
        return receiver_.requestMove(slot, azimuthDeg, elevationDeg, distance);
    }
    /// The user changed level, spread or reverb send of a DAW track in the app.
    bool requestBridgeMix(int slot, const rk::bridge::MixChange& change) noexcept { return receiver_.requestMix(slot, change); }
    /// Newest `n` (<= kScopeSize) samples of the summed output, for the visualisation (any thread).
    static constexpr int kScopeSize = 4096;
    void readScope(float* dst, int n) const noexcept;
    /// Off: DAW tracks are ignored (they fall back to playing in the DAW).
    std::atomic<bool> bridgeEnabled{true};
    /// Speaker calibration: while it runs, every output is silent except the measuring sweep.
    rk::calib::SweepRunner calibration;
    /// Names of the active device inputs, in the order the audio callback sees them.
    juce::StringArray inputNames();

    /// Test room (environment variable RAUMKLANG_TESTRAUM): instead of an audio device, a
    /// simulated room with the given speakers and one measuring microphone. For trying the
    /// calibration without hardware and for automated checks.
    void startSimulation(const rk::SpeakerLayout& realRoom);
    bool simulating() const noexcept { return simThread_.joinable(); }
    void moveSimulatedMicrophone(rk::Vec3 p) noexcept { room_.setMicrophone(p); }
    /// Return path (app -> DAW): true while a "Raumklang Rückweg" plugin is listening.
    bool returnListening() const noexcept { return returnSender_.listening(); }

    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs,
                                          int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& message) override;

private:
    static constexpr int kGenBlock = 2048;
    void generate(int source, int type, float* out, int n) noexcept;

    std::vector<float> genBuffers_; // kMaxGenerated x kGenBlock
    std::vector<float> bridgeBuffers_; // kSlots x 2 x kGenBlock
    rk::bridge::Receiver receiver_;
    rk::bridge::ReturnSender returnSender_;
    std::vector<float> returnBuffers_; // kMaxSpeakers x kGenBlock, used while a return plugin listens
    struct BridgeParamsAtomic {
        std::atomic<float> az{0.0f}, el{0.0f}, dist{2.0f}, spread{0.0f}, gain{0.0f}, send{-12.0f}, width{60.0f}, amount{1.0f};
    };
    std::array<BridgeParamsAtomic, rk::bridge::kSlots> bridgeParams_{};
    std::array<std::atomic<float>, kScopeSize> scope_{};
    std::atomic<uint32_t> scopeWrite_{0};
    std::array<rk::PinkNoise, kMaxGenerated> pink_;
    std::array<double, kMaxGenerated> phase_{};
    std::array<long long, kMaxGenerated> clickCounter_{};
    std::atomic<int> numOutputs_{0}, numInputs_{0};
    bool inputsEnabled_ = false;
    std::atomic<double> sampleRate_{48000.0};
    rk::RoomSimulator room_;
    std::thread simThread_;
    std::atomic<bool> simStop_{false};
    juce::CriticalSection errorLock_;
    juce::String error_;
};

} // namespace rkapp
