#include "ReturnProcessor.h"

namespace rkplug {
namespace {
int64_t nowMs() { return (int64_t)juce::Time::getMillisecondCounter(); }

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"first", 1}, "Erster Lautsprecher", 1,
                                                          rk::bridge::kReturnChannels, 1));
    return {p.begin(), p.end()};
}
} // namespace

ReturnProcessor::ReturnProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Speakers", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "RaumklangRueckweg", makeLayout()) {
    first_ = params.getRawParameterValue("first");
    receiver_.open(); // may fail in a sandboxed host: the editor then says so
    receiver_.heartbeat();
    startTimerHz(4);
}

ReturnProcessor::~ReturnProcessor() { stopTimer(); }

bool ReturnProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const int in = layouts.getMainInputChannels();
    const int out = layouts.getMainOutputChannels();
    // Input may be absent (instrument-like use) or match the output, as effect hosts require.
    return out >= 1 && out <= rk::bridge::kReturnChannels && (in == 0 || in == out);
}

void ReturnProcessor::prepareToPlay(double sampleRate, int) {
    sampleRate_.store(sampleRate);
    channels_.store(getTotalNumOutputChannels());
}

void ReturnProcessor::timerCallback() {
    // Tells the app that someone listens; it then renders every speaker for us.
    receiver_.heartbeat();
}

void ReturnProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numCh = std::min(buffer.getNumChannels(), rk::bridge::kReturnChannels);
    const int n = buffer.getNumSamples();
    lastProcessMs_.store(nowMs(), std::memory_order_relaxed);
    if (isNonRealtime()) lastOfflineMs_.store(nowMs(), std::memory_order_relaxed);
    const int first = juce::jlimit(0, rk::bridge::kReturnChannels - 1, juce::roundToInt(first_->load()) - 1);
    const int got = receiver_.read(first, buffer.getArrayOfWritePointers(), numCh, n, getSampleRate());
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, n);
    if (got > 0) lastAudioMs_.store(nowMs(), std::memory_order_relaxed);
    dropouts_.fetch_add(receiver_.takeDropouts(), std::memory_order_relaxed);
    for (int ch = 0; ch < numCh; ++ch) {
        const float pk = buffer.getMagnitude(ch, 0, n);
        auto& a = peaks_[(size_t)ch];
        if (pk > a.load(std::memory_order_relaxed)) a.store(pk, std::memory_order_relaxed);
    }
}

float ReturnProcessor::takePeak(int ch) noexcept {
    if (ch < 0 || ch >= rk::bridge::kReturnChannels) return 0.0f;
    return peaks_[(size_t)ch].exchange(0.0f, std::memory_order_relaxed);
}

ReturnProcessor::Status ReturnProcessor::status() {
    Status s;
    s.available = receiver_.isOpen();
    s.appRunning = receiver_.appRunning();
    s.sampleRate = sampleRate_.load();
    s.appSampleRate = receiver_.appSampleRate();
    s.rateMismatch = s.appRunning && s.sampleRate > 0.0 && std::abs(s.appSampleRate - s.sampleRate) > 0.5;
    s.appChannels = receiver_.appChannels();
    s.trackChannels = channels_.load();
    s.firstChannel = juce::roundToInt(first_->load());
    const int64_t now = nowMs();
    s.receiving = now - lastAudioMs_.load() < 1000;
    s.processing = now - lastProcessMs_.load() < 600;
    s.offline = now - lastOfflineMs_.load() < 5000 && lastOfflineMs_.load() != 0;
    s.dropouts = dropouts_.exchange(0);
    return s;
}

void ReturnProcessor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = params.copyState().createXml()) copyXmlToBinary(*xml, dest);
}

void ReturnProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (xml != nullptr && xml->hasTagName(params.state.getType())) params.replaceState(juce::ValueTree::fromXml(*xml));
}

} // namespace rkplug

#ifndef RK_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new rkplug::ReturnProcessor(); }
#endif
