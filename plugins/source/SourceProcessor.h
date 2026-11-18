// "Raumklang Quelle": put one on every track. It places the track's audio in the
// room and outputs one channel per speaker (set the track to as many channels as
// the speaker setup has). The speaker setup is shared with the Raumklang app and the
// "Raumklang Raum" plugin through a file, so every instance always uses the same room.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <rk/engine.hpp>
#include <rk_ui/Scene.h>

namespace rkplug {

class SourceProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    SourceProcessor();
    ~SourceProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Raumklang Quelle"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    juce::AudioProcessorValueTreeState params;
    rk::Engine engine;

    /// Message thread: the room this instance currently renders into.
    rkui::Scene setupSnapshot() const;
    /// Message thread: position of the (mono) source for the stage view.
    rk::Vec3 sourcePosition() const;
    void setSourcePosition(rk::Vec3 p);
    int outputChannels() const { return outputChannels_.load(); }
    float takeOutputPeak(int c) { return engine.takeOutputPeak(c); }

private:
    void timerCallback() override;
    void reloadSetup(bool force);
    void applyEngineConfig();

    juce::CriticalSection setupLock_;
    rkui::Scene setup_;
    juce::Time setupFileTime_;
    std::atomic<int> outputChannels_{2};
    std::atomic<float>* az_ = nullptr;
    std::atomic<float>* el_ = nullptr;
    std::atomic<float>* dist_ = nullptr;
    std::atomic<float>* spread_ = nullptr;
    std::atomic<float>* gain_ = nullptr;
    std::atomic<float>* send_ = nullptr;
    std::atomic<float>* width_ = nullptr;
    std::atomic<float>* distAmt_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourceProcessor)
};

} // namespace rkplug
