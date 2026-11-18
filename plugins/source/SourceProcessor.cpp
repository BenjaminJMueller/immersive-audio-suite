#include "SourceProcessor.h"

#include "SourceEditor.h"

namespace rkplug {
namespace {

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    using P = juce::AudioParameterFloat;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<P>(juce::ParameterID{"azimuth", 1}, "Azimut", juce::NormalisableRange<float>(-180.0f, 180.0f, 0.1f), 0.0f, A().withLabel(juce::String::fromUTF8("\xc2\xb0"))));
    p.push_back(std::make_unique<P>(juce::ParameterID{"elevation", 1}, "Elevation", juce::NormalisableRange<float>(-90.0f, 90.0f, 0.1f), 0.0f, A().withLabel(juce::String::fromUTF8("\xc2\xb0"))));
    juce::NormalisableRange<float> dr(0.0f, 50.0f, 0.01f);
    dr.setSkewForCentre(5.0f);
    p.push_back(std::make_unique<P>(juce::ParameterID{"distance", 1}, "Abstand", dr, 2.0f, A().withLabel("m")));
    p.push_back(std::make_unique<P>(juce::ParameterID{"spread", 1}, "Breite", juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, A().withLabel("%")));
    p.push_back(std::make_unique<P>(juce::ParameterID{"gain", 1}, "Pegel", juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f), 0.0f, A().withLabel("dB")));
    p.push_back(std::make_unique<P>(juce::ParameterID{"reverbSend", 1}, "Hallanteil", juce::NormalisableRange<float>(-80.0f, 6.0f, 0.1f), -12.0f, A().withLabel("dB")));
    p.push_back(std::make_unique<P>(juce::ParameterID{"stereoWidth", 1}, "Stereobreite", juce::NormalisableRange<float>(0.0f, 180.0f, 0.1f), 60.0f, A().withLabel(juce::String::fromUTF8("\xc2\xb0"))));
    p.push_back(std::make_unique<P>(juce::ParameterID{"distanceAmount", 1}, "Entfernung", juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 100.0f, A().withLabel("%")));
    return {p.begin(), p.end()};
}

} // namespace

SourceProcessor::SourceProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Speakers", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "RaumklangQuelle", makeLayout()) {
    az_ = params.getRawParameterValue("azimuth");
    el_ = params.getRawParameterValue("elevation");
    dist_ = params.getRawParameterValue("distance");
    spread_ = params.getRawParameterValue("spread");
    gain_ = params.getRawParameterValue("gain");
    send_ = params.getRawParameterValue("reverbSend");
    width_ = params.getRawParameterValue("stereoWidth");
    distAmt_ = params.getRawParameterValue("distanceAmount");
    reloadSetup(true);
    startTimer(1000);
}

SourceProcessor::~SourceProcessor() { stopTimer(); }

bool SourceProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const int in = layouts.getMainInputChannels();
    const int out = layouts.getMainOutputChannels();
    return (in == 1 || in == 2) && out >= 1 && out <= 64 && !layouts.getMainOutputChannelSet().isDisabled();
}

void SourceProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    outputChannels_.store(getTotalNumOutputChannels());
    engine.prepare(sampleRate, juce::jlimit(16, 8192, samplesPerBlock));
}

void SourceProcessor::reloadSetup(bool force) {
    const juce::File f = rkui::sharedSetupFile();
    const juce::Time t = f.existsAsFile() ? f.getLastModificationTime() : juce::Time();
    if (!force && t == setupFileTime_) return;
    setupFileTime_ = t;
    rkui::Scene s;
    {
        const juce::ScopedLock sl(setupLock_);
        s = setup_;
    }
    if (!rkui::readSharedSetup(s) && !force) return;
    {
        const juce::ScopedLock sl(setupLock_);
        setup_ = s;
    }
    applyEngineConfig();
}

void SourceProcessor::applyEngineConfig() {
    rkui::Scene s = setupSnapshot();
    rk::EngineConfig c = s.engineConfig();
    // Calibration (delay, level), headphones and the final limiter belong to "Raum" on the master.
    for (auto& sp : c.layout.speakers) {
        sp.gainDb = 0.0f;
        sp.delayMs = 0.0f;
    }
    c.outputMode = rk::OutputMode::Speakers;
    engine.setConfig(c);
    engine.setReverb(s.reverb);
    engine.reverbWetDb = s.reverbWetDb;
    engine.limiterEnabled = false;
}

rkui::Scene SourceProcessor::setupSnapshot() const {
    const juce::ScopedLock sl(setupLock_);
    return setup_;
}

rk::Vec3 SourceProcessor::sourcePosition() const {
    return rk::toCartesian({az_->load(), el_->load(), dist_->load()});
}

void SourceProcessor::setSourcePosition(rk::Vec3 p) {
    const rk::Polar pol = rk::toPolar(p);
    auto set = [this](const char* id, float v) {
        if (auto* prm = params.getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1(v));
    };
    set("azimuth", pol.azimuthDeg);
    set("elevation", pol.elevationDeg);
    set("distance", pol.distance);
}

void SourceProcessor::timerCallback() {
    reloadSetup(false);
    engine.collectGarbage();
}

void SourceProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numIn = juce::jmin(2, getTotalNumInputChannels());
    const int numOut = buffer.getNumChannels();
    const int n = buffer.getNumSamples();

    // A stereo input becomes two sources, spread left and right of the centre direction.
    const float az = az_->load(), el = el_->load(), dist = dist_->load();
    const float half = numIn == 2 ? width_->load() * 0.5f : 0.0f;
    for (int ch = 0; ch < 2; ++ch) {
        auto& s = engine.source(ch);
        if (ch >= numIn) { s.active = false; continue; }
        s.active = true;
        s.inputChannel = ch;
        s.gainDb = gain_->load() + (numIn == 2 ? -3.0f : 0.0f);
        s.spread = spread_->load() * 0.01f;
        s.reverbSendDb = send_->load();
        s.distanceAmount = distAmt_->load() * 0.01f;
        const float a = az + (ch == 0 ? half : -half);
        const rk::Vec3 p = rk::toCartesian({rk::wrapDegrees(a), el, dist});
        if (p.x != s.x.load() || p.y != s.y.load() || p.z != s.z.load()) s.setPosition(p);
        else s.touch();
    }
    engine.process(buffer.getArrayOfReadPointers(), numIn, buffer.getArrayOfWritePointers(), numOut, n);
}

void SourceProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = params.copyState();
    state.setProperty("setup", rkui::sceneToJson(setupSnapshot()), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void SourceProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (xml == nullptr || !xml->hasTagName(params.state.getType())) return;
    auto tree = juce::ValueTree::fromXml(*xml);
    // The shared setup file wins; the stored copy is the fallback when it is missing.
    if (!rkui::sharedSetupFile().existsAsFile()) {
        rkui::Scene s;
        if (rkui::sceneFromJson(tree.getProperty("setup").toString(), s)) {
            const juce::ScopedLock sl(setupLock_);
            setup_ = s;
        }
        applyEngineConfig();
    }
    tree.removeProperty("setup", nullptr);
    params.replaceState(tree);
}

} // namespace rkplug

#ifndef RK_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new rkplug::SourceProcessor(); }
#endif
