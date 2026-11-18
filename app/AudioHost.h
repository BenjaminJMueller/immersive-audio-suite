// Raumklang app: owns the audio device and the engine. Device inputs and the built-in
// test signals feed the sources, the engine output goes straight to the device outputs.
// No virtual audio cables or routing tools are needed.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <rk/bridge.hpp>
#include <rk/dsp.hpp>
#include <rk/engine.hpp>

#include <array>
#include <atomic>

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
    juce::String start(const juce::XmlElement* savedState);
    void stop();

    rk::Engine engine;
    juce::AudioDeviceManager devices;
    /// Test signal per source: 0 off, 1 pink noise, 2 sine, 3 clicks.
    std::array<std::atomic<int>, kMaxGenerated> testSignal{};

    int numDeviceOutputs() const noexcept { return numOutputs_.load(); }
    int numDeviceInputs() const noexcept { return numInputs_.load(); }
    double sampleRate() const noexcept { return sampleRate_.load(); }
    juce::String takeError();

    /// Plugin bridge (message thread): announce the app and list the connected DAW tracks.
    bool bridgeAvailable() const noexcept { return receiver_.isOpen(); }
    std::vector<rk::bridge::TrackInfo> scanBridge();
    uint32_t takeBridgeDropouts() noexcept { return receiver_.takeDropouts(); }
    /// Latest parameters of a DAW track, as the audio thread applied them.
    rk::bridge::TrackParams bridgeParams(int slot) const noexcept;
    /// Off: DAW tracks are ignored (they fall back to playing in the DAW).
    std::atomic<bool> bridgeEnabled{true};

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
    struct BridgeParamsAtomic {
        std::atomic<float> az{0.0f}, el{0.0f}, dist{2.0f}, spread{0.0f}, gain{0.0f}, send{-12.0f}, width{60.0f}, amount{1.0f};
    };
    std::array<BridgeParamsAtomic, rk::bridge::kSlots> bridgeParams_{};
    std::array<rk::PinkNoise, kMaxGenerated> pink_;
    std::array<double, kMaxGenerated> phase_{};
    std::array<long long, kMaxGenerated> clickCounter_{};
    std::atomic<int> numOutputs_{0}, numInputs_{0};
    std::atomic<double> sampleRate_{48000.0};
    juce::CriticalSection errorLock_;
    juce::String error_;
};

} // namespace rkapp
