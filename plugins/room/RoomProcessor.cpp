#include "RoomProcessor.h"

namespace rkplug {
namespace {
juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"master", 1}, "Gesamtpegel",
                                                            juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f), 0.0f,
                                                            juce::AudioParameterFloatAttributes().withLabel("dB")));
    p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"limiter", 1}, "Schutzbegrenzer", true));
    p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"headphones", 1}, juce::String::fromUTF8("Kopfh\xc3\xb6rer-Vorschau"), false));
    return {p.begin(), p.end()};
}
} // namespace

RoomProcessor::RoomProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Speakers", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Speakers", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "RaumklangRaum", makeLayout()) {
    master_ = params.getRawParameterValue("master");
    limiter_ = params.getRawParameterValue("limiter");
    headphones_ = params.getRawParameterValue("headphones");
    // Start from the setup the app or another session published, if any.
    rkui::readSharedSetup(scene_);
    lastHeadphones_ = headphones_->load() > 0.5f;
    speakers.setLayout(scene_.layout, lastHeadphones_);
    startTimerHz(10);
}

RoomProcessor::~RoomProcessor() { stopTimer(); }

bool RoomProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const int in = layouts.getMainInputChannels();
    const int out = layouts.getMainOutputChannels();
    return in == out && out >= 1 && out <= 64;
}

void RoomProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    channels_.store(getTotalNumOutputChannels());
    speakers.prepare(sampleRate, juce::jlimit(16, 8192, samplesPerBlock));
}

void RoomProcessor::sceneChanged(bool publish) {
    lastHeadphones_ = headphones_->load() > 0.5f;
    speakers.setLayout(scene_.layout, lastHeadphones_);
    if (publish) rkui::writeSharedSetup(scene_);
}

void RoomProcessor::timerCallback() {
    const bool hp = headphones_->load() > 0.5f;
    if (hp != lastHeadphones_) sceneChanged(false);
    speakers.collectGarbage();
}

void RoomProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    speakers.masterGainDb = master_->load();
    speakers.limiterEnabled = limiter_->load() > 0.5f;
    speakers.process(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void RoomProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = params.copyState();
    state.setProperty("scene", rkui::sceneToJson(scene_), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void RoomProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (xml == nullptr || !xml->hasTagName(params.state.getType())) return;
    auto tree = juce::ValueTree::fromXml(*xml);
    rkui::Scene s;
    if (rkui::sceneFromJson(tree.getProperty("scene").toString(), s)) scene_ = s;
    tree.removeProperty("scene", nullptr);
    params.replaceState(tree);
    // The project's room becomes the shared room, so every source follows it.
    sceneChanged(true);
}

} // namespace rkplug

#ifndef RK_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new rkplug::RoomProcessor(); }
#endif
