// "Raumklang Raum": goes on the master/speaker bus after all "Raumklang Quelle"
// instances. It holds the speaker setup (and publishes it to the sources), applies
// per-speaker delay and level, offers a headphone preview and a speaker test, and
// protects the speakers with a limiter.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <rk/speaker_processor.hpp>
#include <rk_ui/Scene.h>

namespace rkplug {

class RoomProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    RoomProcessor();
    ~RoomProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Raumklang Raum"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    juce::AudioProcessorValueTreeState params;
    rk::SpeakerProcessor speakers;

    /// Message thread only.
    rkui::Scene& scene() { return scene_; }
    /// Message thread: call after editing scene(). `publish` writes the shared file for the sources.
    void sceneChanged(bool publish);
    int channels() const { return channels_.load(); }

private:
    void timerCallback() override;
    rkui::Scene scene_;
    bool lastHeadphones_ = false;
    std::atomic<int> channels_{2};
    std::atomic<float>* master_ = nullptr;
    std::atomic<float>* limiter_ = nullptr;
    std::atomic<float>* headphones_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoomProcessor)
};

} // namespace rkplug
