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
    p.push_back(std::make_unique<P>(juce::ParameterID{"reverbSend", 1}, "Hall-Send", juce::NormalisableRange<float>(-80.0f, 6.0f, 0.1f), -12.0f, A().withLabel("dB")));
    p.push_back(std::make_unique<P>(juce::ParameterID{"stereoWidth", 1}, "Stereobreite", juce::NormalisableRange<float>(0.0f, 180.0f, 0.1f), 60.0f, A().withLabel(juce::String::fromUTF8("\xc2\xb0"))));
    p.push_back(std::make_unique<P>(juce::ParameterID{"distanceAmount", 1}, "Distanzeffekt", juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 100.0f, A().withLabel("%")));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"route", 1}, "Ausgabe",
                                                             juce::StringArray{"Automatisch", "An die App", "In der DAW"}, 0));
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
    route_ = params.getRawParameterValue("route");
    reloadSetup(true);
    housekeeping(); // connect to the app right away
    startTimer(40);
}

SourceProcessor::~SourceProcessor() {
    stopTimer();
    if (appGesture_) // a drag from the app was still open: close it for the host
        for (const char* id : {"azimuth", "elevation", "distance"})
            if (auto* prm = params.getParameter(id)) prm->endChangeGesture();
    sender_.release();
}

void SourceProcessor::updateTrackProperties(const TrackProperties& properties) {
    const juce::ScopedLock sl(nameLock_);
    trackName_ = properties.name.value_or(juce::String()).trim();
    if (rkui::isPlaceholderTrackName(trackName_)) trackName_.clear(); // e.g. "void" from some hosts
    trackColour_ = properties.colourARGB.has_value() ? juce::Colour((juce::uint32)*properties.colourARGB) : juce::Colours::transparentBlack;
}

juce::Colour SourceProcessor::dawTrackColour() const {
    const juce::ScopedLock sl(nameLock_);
    return trackColour_;
}

juce::String SourceProcessor::customName() const {
    const juce::ScopedLock sl(nameLock_);
    return customName_;
}

juce::String SourceProcessor::dawTrackName() const {
    const juce::ScopedLock sl(nameLock_);
    return trackName_;
}

juce::String SourceProcessor::displayName() const {
    const juce::ScopedLock sl(nameLock_);
    if (customName_.isNotEmpty()) return customName_;
    if (trackName_.isNotEmpty()) return trackName_;
    return "Raumklang Quelle";
}

void SourceProcessor::setCustomName(const juce::String& name) {
    {
        const juce::ScopedLock sl(nameLock_);
        customName_ = name.trim().substring(0, 60);
    }
    updateHostDisplay(); // the host marks the project as changed
}

SourceProcessor::LinkStatus SourceProcessor::linkStatus() const {
    LinkStatus s;
    s.sending = sending_.load();
    s.appRunning = appRunning_.load();
    s.bridgeAvailable = sender_.isOpen();
    s.slot = sender_.slot();
    s.appSampleRate = appSampleRate_.load();
    s.sampleRate = getSampleRate();
    s.rateMismatch = s.appRunning && s.appSampleRate > 0.0 && s.sampleRate > 0.0 && std::abs(s.appSampleRate - s.sampleRate) > 1.0;
    s.route = static_cast<Route>(juce::jlimit(0, 2, (int)std::lround(route_->load())));
    return s;
}

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
    // Without the app every track renders itself, including delay and level per speaker
    // (both linear, so per track is the same as on the bus). The limiter needs the sum,
    // so it stays off here; the app has one for the speakers.
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
    // Fast part: the user drags this track in the app. Applied as a normal parameter change
    // inside a gesture, so the DAW records it as automation (touch/latch/write).
    float az = 0.0f, el = 0.0f, dist = 0.0f;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (sender_.takeMove(az, el, dist)) {
        const char* ids[3] = {"azimuth", "elevation", "distance"};
        const float vals[3] = {az, el, dist};
        for (int k = 0; k < 3; ++k)
            if (auto* prm = params.getParameter(ids[k])) {
                if (!appGesture_) prm->beginChangeGesture();
                prm->setValueNotifyingHost(prm->convertTo0to1(vals[k]));
            }
        appGesture_ = true;
        lastAppMoveMs_ = now;
    } else if (appGesture_ && now - lastAppMoveMs_ > 300.0) {
        for (const char* id : {"azimuth", "elevation", "distance"})
            if (auto* prm = params.getParameter(id)) prm->endChangeGesture();
        appGesture_ = false;
    }
    if (++tick_ % 12 == 0) housekeeping(); // about twice a second
}

void SourceProcessor::housekeeping() {
    reloadSetup(false);
    engine.collectGarbage();
    // Bridge housekeeping (message thread): open, claim a slot while the app runs, stay alive.
    if (!sender_.isOpen()) sender_.open();
    const bool running = sender_.appRunning();
    appRunning_ = running;
    const double appRate = sender_.appSampleRate();
    appSampleRate_ = appRate;
    const double ownRate = getSampleRate();
    const bool rateOk = appRate <= 0.0 || ownRate <= 0.0 || std::abs(appRate - ownRate) <= 1.0;
    // "Automatic" only hands the track to the app when the app can actually play it.
    appUsable_ = running && rateOk;
    const auto route = static_cast<Route>(juce::jlimit(0, 2, (int)std::lround(route_->load())));
    if ((route == Route::App && running) || (route == Route::Automatic && running && rateOk)) {
        if (sender_.slot() < 0) sender_.claim();
        sender_.heartbeat(displayName().toStdString());
    } else if (sender_.slot() >= 0) {
        sender_.release();
    }
}

void SourceProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numIn = juce::jmin(2, getTotalNumInputChannels());
    const int numOut = buffer.getNumChannels();
    const int n = buffer.getNumSamples();

    float peak = 0.0f;
    for (int ch = 0; ch < numIn; ++ch) peak = std::max(peak, buffer.getMagnitude(ch, 0, n));
    if (std::isfinite(peak) && peak > inputPeak_.load(std::memory_order_relaxed)) inputPeak_.store(peak, std::memory_order_relaxed);

    // Send to the app: the app spatialises and plays the track, the DAW track stays silent.
    const auto route = static_cast<Route>(juce::jlimit(0, 2, (int)std::lround(route_->load())));
    const bool toApp = route == Route::App || (route == Route::Automatic && appUsable_.load(std::memory_order_relaxed));
    if (toApp && sender_.slot() >= 0) {
        rk::bridge::TrackParams tp;
        tp.azimuthDeg = az_->load();
        tp.elevationDeg = el_->load();
        tp.distance = dist_->load();
        tp.spread = spread_->load() * 0.01f;
        tp.gainDb = gain_->load();
        tp.reverbSendDb = send_->load();
        tp.stereoWidthDeg = width_->load();
        tp.distanceAmount = distAmt_->load() * 0.01f;
        sender_.write(buffer.getArrayOfReadPointers(), juce::jmax(1, numIn), n, getSampleRate(), tp);
        buffer.clear();
        sending_.store(true, std::memory_order_relaxed);
        return;
    }
    sending_.store(false, std::memory_order_relaxed);
    if (route == Route::App) { // explicitly "app", but the app is not there: stay silent
        buffer.clear();
        return;
    }

    // Render in the DAW: one output channel per speaker.
    rk::bridge::TrackParams tp;
    tp.azimuthDeg = az_->load();
    tp.elevationDeg = el_->load();
    tp.distance = dist_->load();
    tp.spread = spread_->load() * 0.01f;
    tp.gainDb = gain_->load();
    tp.reverbSendDb = send_->load();
    tp.stereoWidthDeg = width_->load();
    tp.distanceAmount = distAmt_->load() * 0.01f;
    rk::bridge::applyTrack(engine, 0, 0, juce::jmax(1, numIn), tp, numIn > 0);
    engine.process(buffer.getArrayOfReadPointers(), numIn, buffer.getArrayOfWritePointers(), numOut, n);
}

void SourceProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = params.copyState();
    state.setProperty("setup", rkui::sceneToJson(setupSnapshot()), nullptr);
    state.setProperty("name", customName(), nullptr);
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
    {
        const juce::ScopedLock sl(nameLock_);
        customName_ = tree.getProperty("name").toString().substring(0, 60);
    }
    tree.removeProperty("name", nullptr);
    params.replaceState(tree);
}

} // namespace rkplug

#ifndef RK_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new rkplug::SourceProcessor(); }
#endif
