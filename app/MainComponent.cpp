#include "MainComponent.h"

using rkui::tr;
using rkui::u8;

namespace rkapp {

// ---------------------------------------------------------------------------
// First-run introduction, offers the demo (Projection Studio does the same).
// ---------------------------------------------------------------------------
class IntroOverlay : public juce::Component {
public:
    std::function<void()> onDemo, onEmpty;

    IntroOverlay() {
        demo_ = rkui::makeButton(tr("Mit Demo beginnen"), true);
        empty_ = rkui::makeButton(tr("Leer beginnen"));
        demo_->onClick = [this] { if (onDemo) onDemo(); };
        empty_->onClick = [this] { if (onEmpty) onEmpty(); };
        addAndMakeVisible(*demo_);
        addAndMakeVisible(*empty_);
    }

    juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre(560, 330); }

    void paint(juce::Graphics& g) override {
        const auto& p = rkui::palette();
        g.fillAll(juce::Colours::black.withAlpha(0.45f));
        auto c = card().toFloat();
        g.setColour(p.bar);
        g.fillRoundedRectangle(c, 14.0f);
        g.setColour(p.lineStrong);
        g.drawRoundedRectangle(c, 14.0f, 1.0f);
        auto r = card().reduced(32);
        g.setColour(p.text);
        g.setFont(rkui::uiFont(24.0f, true));
        g.drawText(tr("Willkommen bei Raumklang"), r.removeFromTop(34), juce::Justification::centredLeft);
        r.removeFromTop(10);
        juce::AttributedString a;
        a.setLineSpacing(4.0f);
        a.append(rkui::language() == rkui::Language::German
                     ? u8("Raumklang verteilt Klangquellen frei im Raum auf beliebig viele Lautsprecher: Ring, Kuppel, "
                          "5.1 bis 7.1.4 oder ein Donut mit Lautsprechern, die sich gegen\xc3\xbc" "berstehen.\n\n"
                          "1  Lautsprecher-Layout w\xc3\xa4hlen\n2  Quellen platzieren und ziehen\n3  Raum und Hall einstellen\n"
                          "4  Audiogerät w\xc3\xa4hlen. Ohne Zusatzprogramme f\xc3\xbcr das Routing.")
                     : u8("Raumklang places sound sources freely in space on any number of speakers: ring, dome, 5.1 to 7.1.4 "
                          "or a donut with speakers facing each other.\n\n1  Choose a speaker layout\n2  Place and drag sources\n"
                          "3  Set up room and reverb\n4  Pick your audio device. No extra routing software needed."),
                 rkui::uiFont(14.0f), p.text2);
        juce::TextLayout t;
        t.createLayout(a, (float)r.getWidth());
        t.draw(g, r.toFloat());
    }

    void resized() override {
        auto r = card().reduced(32).removeFromBottom(36);
        demo_->setBounds(r.removeFromRight(190));
        r.removeFromRight(10);
        empty_->setBounds(r.removeFromRight(150));
    }

    void mouseDown(const juce::MouseEvent&) override {} // block clicks to the UI below

private:
    std::unique_ptr<juce::TextButton> demo_, empty_;
};

// ---------------------------------------------------------------------------

namespace {
constexpr int kTopBar = 52;
constexpr int kSidebar = 240;
constexpr int kInspector = 350;

float levelToUnit(float peak) {
    const float db = rk::gainToDb(peak);
    return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

juce::String degree() { return juce::String(juce::CharPointer_UTF8("\xc2\xb0")); }
} // namespace

MainComponent::MainComponent()
    : steps_(juce::StringArray{tr("Lautsprecher"), tr("Quellen"), tr("Raum"), tr("Ausgabe")}, true) {
    juce::PropertiesFile::Options opt;
    opt.applicationName = "Raumklang";
    opt.filenameSuffix = ".settings";
    opt.folderName = rkui::appDataFolder().getFullPathName();
    opt.osxLibrarySubFolder = "Application Support";
    settings_ = std::make_unique<juce::PropertiesFile>(rkui::appDataFolder().getChildFile("Einstellungen.settings"), opt);
    rkui::setLanguage(settings_->getValue("language", "de") == "en" ? rkui::Language::English : rkui::Language::German);
    const juce::String theme = settings_->getValue("theme", "system");
    rkui::setDarkMode(theme == "system" ? juce::Desktop::getInstance().isDarkModeActive() : theme == "dark");
    lnf_.refreshColours();
    setLookAndFeel(&lnf_);
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);

    steps_.onChange = [this](int i) { setStep(i); };
    addAndMakeVisible(steps_);
    undoBtn_ = rkui::makeButton(juce::String(juce::CharPointer_UTF8("\xe2\x86\xb6")), false, true);
    redoBtn_ = rkui::makeButton(juce::String(juce::CharPointer_UTF8("\xe2\x86\xb7")), false, true);
    openBtn_ = rkui::makeButton(tr("Öffnen …"), false, true);
    saveBtn_ = rkui::makeButton(tr("Sichern unter …"), false, true);
    helpBtn_ = rkui::makeButton("?", false, true);
    addBtn_ = rkui::makeButton("+ " + tr("Quelle hinzufügen"));
    undoBtn_->setTooltip(tr("Rückgängig"));
    redoBtn_->setTooltip(tr("Wiederholen"));
    helpBtn_->setTooltip(tr("Hilfe"));
    undoBtn_->onClick = [this] { undo(); };
    redoBtn_->onClick = [this] { redo(); };
    openBtn_->onClick = [this] { openProject(); };
    saveBtn_->onClick = [this] { saveProjectAs(); };
    helpBtn_->onClick = [this] { showHelpMenu(); };
    addBtn_->onClick = [this] { addSource(); };
    for (auto* b : {undoBtn_.get(), redoBtn_.get(), openBtn_.get(), saveBtn_.get(), helpBtn_.get(), addBtn_.get()}) addAndMakeVisible(*b);

    sidebar_.onSelect = [this](int row) {
        if (step_ == StepSpeakers || step_ == StepOutput) selectSpeaker(row);
        else selectSource(row);
    };
    sidebarView_.setViewedComponent(&sidebar_, false);
    sidebarView_.setScrollBarsShown(true, false);
    sidebarView_.setScrollBarThickness(8);
    addAndMakeVisible(sidebarView_);

    stage_.onSourceMoved = [this](int i, rk::Vec3 p) {
        if (i < (int)scene_.sources.size()) {
            scene_.sources[(size_t)i].position = p;
            applyScene(false);
            refreshSidebar();
            if (i == selSource_ && step_ == StepSources) rebuildInspector();
        }
    };
    stage_.onSpeakerMoved = [this](int i, rk::Vec3 p) {
        if (i < (int)scene_.layout.speakers.size()) {
            scene_.layout.speakers[(size_t)i].position = p;
            scene_.presetId = "custom";
            applyScene(true);
            refreshSidebar();
        }
    };
    stage_.onSourceSelected = [this](int i) { selectSource(i); };
    stage_.onSpeakerSelected = [this](int i) { selectSpeaker(i); };
    stage_.onDragEnd = [this] { commit(); rebuildInspector(); };
    addAndMakeVisible(stage_);
    addAndMakeVisible(inspector_);

    // Audio: open the device stored last time, otherwise the system default.
    std::unique_ptr<juce::XmlElement> audioState(settings_->getXmlValue("audioDevice"));
    const juce::String err = host_.start(audioState.get());
    if (err.isNotEmpty()) statusText_ = err;

    // Restore the autosave, or show the introduction.
    const juce::File autosave = rkui::appDataFolder().getChildFile("Autosave.raumklang");
    rkui::Scene restored;
    if (autosave.existsAsFile() && rkui::sceneFromJson(autosave.loadFileAsString(), restored)) {
        loadScene(restored, true);
    } else {
        rkui::Scene s;
        loadScene(s, true);
        dirty_ = false; // nothing chosen yet: keep showing the introduction next time
        intro_ = std::make_unique<IntroOverlay>();
        intro_->onDemo = [this] { startDemo(); };
        intro_->onEmpty = [this] { startEmpty(); };
        addAndMakeVisible(*intro_);
    }

    setWantsKeyboardFocus(true);
    setSize(1320, 820);
    startTimerHz(30);
}

MainComponent::~MainComponent() {
    stopTimer();
    saveNow();
    if (auto xml = host_.devices.createStateXml()) settings_->setValue("audioDevice", xml.get());
    settings_->saveIfNeeded();
    host_.stop();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

// ---------------------------------------------------------------------------

rkui::PageContext MainComponent::pageContext() {
    rkui::PageContext c;
    c.scene = &scene_;
    c.changed = [this](bool structural) {
        applyScene(structural);
        stage_.setScene(scene_);
        refreshSidebar();
    };
    c.commit = [this] { commit(); };
    c.rebuild = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) { sp->rebuildInspector(); sp->refreshSidebar(); } }); };
    return c;
}

void MainComponent::applyScene(bool structural) {
    if (structural) host_.engine.setConfig(scene_.engineConfig());
    for (int i = 0; i < rkui::kMaxSceneSources; ++i) {
        auto& c = host_.engine.source(i);
        if (i < (int)scene_.sources.size()) {
            const auto& s = scene_.sources[(size_t)i];
            const bool test = s.test != rkui::TestSignal::Off;
            host_.testSignal[(size_t)i].store(static_cast<int>(s.test));
            c.inputChannel = test ? AudioHost::kGeneratorBase + i : s.inputChannel;
            c.gainDb = s.gainDb;
            c.spread = s.spread;
            c.reverbSendDb = s.reverbSendDb;
            c.distanceAmount = s.distanceAmount;
            c.mute = s.mute;
            c.solo = s.solo;
            c.active = true;
            c.setPosition(s.position);
        } else {
            c.active = false;
            host_.testSignal[(size_t)i].store(0);
        }
    }
    host_.engine.setReverb(scene_.reverb);
    host_.engine.reverbWetDb = scene_.reverbWetDb;
    host_.engine.masterGainDb = scene_.masterGainDb;
    host_.engine.limiterEnabled = scene_.limiter;
    dirty_ = true;
    lastChange_ = juce::Time::getMillisecondCounterHiRes();
}

void MainComponent::commit() {
    const juce::String now = rkui::sceneToJson(scene_);
    if (now == committed_) return;
    undo_.push_back(committed_);
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
    committed_ = now;
    dirty_ = true;
    rkui::writeSharedSetup(scene_);
    undoBtn_->setEnabled(true);
    redoBtn_->setEnabled(false);
}

void MainComponent::undo() {
    if (undo_.empty()) return;
    redo_.push_back(committed_);
    committed_ = undo_.back();
    undo_.pop_back();
    rkui::Scene s;
    if (rkui::sceneFromJson(committed_, s)) loadScene(s, false);
}

void MainComponent::redo() {
    if (redo_.empty()) return;
    undo_.push_back(committed_);
    committed_ = redo_.back();
    redo_.pop_back();
    rkui::Scene s;
    if (rkui::sceneFromJson(committed_, s)) loadScene(s, false);
}

void MainComponent::loadScene(const rkui::Scene& s, bool resetHistory) {
    scene_ = s;
    if (resetHistory) {
        undo_.clear();
        redo_.clear();
        committed_ = rkui::sceneToJson(scene_);
    }
    selSource_ = scene_.sources.empty() ? -1 : juce::jlimit(0, (int)scene_.sources.size() - 1, juce::jmax(0, selSource_));
    selSpeaker_ = juce::jmin(selSpeaker_, (int)scene_.layout.speakers.size() - 1);
    applyScene(true);
    rkui::writeSharedSetup(scene_);
    refreshAll();
}

void MainComponent::refreshAll() {
    steps_.setSelected(step_);
    stage_.setScene(scene_);
    stage_.setSelectedSource(selSource_);
    stage_.setSelectedSpeaker(selSpeaker_);
    stage_.setEditTarget(step_ == StepSpeakers ? rkui::StageView::EditTarget::Speakers : rkui::StageView::EditTarget::Sources);
    stage_.setHint(step_ == StepSpeakers ? u8("Lautsprecher ziehen verschiebt sie. Mit \xe2\x87\xa7 rastet die Position ein.")
                                         : u8("Quellen ziehen verschiebt sie. Mit \xe2\x87\xa7 rastet die Position ein. Mausrad zoomt."));
    if (rkui::language() == rkui::Language::English)
        stage_.setHint(step_ == StepSpeakers ? u8("Drag speakers to move them. Hold \xe2\x87\xa7 to snap. Scroll to zoom.")
                                             : u8("Drag sources to move them. Hold \xe2\x87\xa7 to snap. Scroll to zoom."));
    undoBtn_->setEnabled(!undo_.empty());
    redoBtn_->setEnabled(!redo_.empty());
    addBtn_->setVisible(step_ == StepSources || step_ == StepRoom);
    refreshSidebar();
    rebuildInspector();
    resized();
    repaint();
}

juce::String MainComponent::sourceSubtitle(int i) const {
    const auto& s = scene_.sources[(size_t)i];
    juce::String in;
    switch (s.test) {
    case rkui::TestSignal::Pink: in = tr("Rosa Rauschen"); break;
    case rkui::TestSignal::Sine: in = tr("Sinuston"); break;
    case rkui::TestSignal::Clicks: in = tr("Klicks"); break;
    default: in = tr("Eingang") + " " + juce::String(s.inputChannel + 1); break;
    }
    const rk::Polar p = rk::toPolar(s.position);
    return in + u8(" \xc2\xb7 ") + rkui::num(p.azimuthDeg, 0) + degree() + u8(" \xc2\xb7 ") + rkui::num(p.distance, 1) + " m";
}

void MainComponent::refreshSidebar() {
    std::vector<rkui::SidebarList::Row> rows;
    if (step_ == StepSpeakers || step_ == StepOutput) {
        const int outs = host_.numDeviceOutputs();
        for (int m = 0; m < (int)scene_.layout.speakers.size(); ++m) {
            const auto& sp = scene_.layout.speakers[(size_t)m];
            rkui::SidebarList::Row r;
            r.group = sp.group < (int)scene_.layout.groups.size() ? juce::String(scene_.layout.groups[(size_t)sp.group].name) : juce::String();
            r.number = m + 1;
            r.colour = rkui::itemColour(sp.group + 3);
            r.title = sp.name.empty() ? tr("Lautsprecher") + " " + juce::String(m + 1) : juce::String(sp.name);
            const rk::Polar p = rk::toPolar(sp.position);
            r.subtitle = tr("Ausgang") + " " + juce::String(m + 1) + u8(" \xc2\xb7 ") + rkui::num(p.azimuthDeg, 0) + degree() + u8(" \xc2\xb7 ") + rkui::num(p.distance, 1) + " m";
            r.trailing = sp.isSubwoofer ? "Sub" : (sp.gainDb != 0.0f ? rkui::num(sp.gainDb, 1) + " dB" : juce::String());
            r.dim = !sp.enabled || (!scene_.headphones && m >= outs);
            rows.push_back(r);
        }
        sidebar_.setRows(rows);
        sidebar_.setSelected(selSpeaker_);
    } else {
        for (int i = 0; i < (int)scene_.sources.size(); ++i) {
            const auto& s = scene_.sources[(size_t)i];
            rkui::SidebarList::Row r;
            r.number = i + 1;
            r.colour = rkui::itemColour(i);
            r.title = s.name;
            r.subtitle = sourceSubtitle(i);
            r.trailing = s.solo ? "Solo" : (s.mute ? tr("Stumm") : (s.orbit ? u8("\xe2\x9f\xb3") : juce::String()));
            r.dim = s.mute;
            rows.push_back(r);
        }
        sidebar_.setRows(rows);
        sidebar_.setSelected(selSource_);
    }
    sidebar_.setSize(sidebarView_.getMaximumVisibleWidth() > 0 ? sidebarView_.getMaximumVisibleWidth() : kSidebar, sidebar_.contentHeight());
}

void MainComponent::selectSource(int i) {
    selSource_ = i;
    stage_.setSelectedSource(i);
    sidebar_.setSelected(i);
    rebuildInspector();
}

void MainComponent::selectSpeaker(int i) {
    selSpeaker_ = i;
    stage_.setSelectedSpeaker(i);
    stage_.setScene(scene_);
    refreshSidebar();
    rebuildInspector();
}

void MainComponent::setStep(int step) {
    step_ = juce::jlimit(0, 3, step);
    if (step_ != StepOutput && speakerTestRunning_) {
        speakerTestRunning_ = false;
        host_.engine.speakerTest = -1;
    }
    refreshAll();
}

void MainComponent::rebuildInspector() {
    deviceSelector_.reset();
    switch (step_) {
    case StepSpeakers:
        rkui::buildLayoutPage(inspector_, pageContext(), selSpeaker_, [this](int i) {
            juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this), i] { if (sp) sp->selectSpeaker(i); });
        });
        break;
    case StepRoom: rkui::buildRoomPage(inspector_, pageContext()); break;
    case StepOutput: buildOutputPage(); break;
    default: buildSourcesPage(); break;
    }
}

void MainComponent::buildSourcesPage() {
    inspector_.clear();
    auto ctx = pageContext();
    if (selSource_ < 0 || selSource_ >= (int)scene_.sources.size()) {
        auto card = std::make_unique<rkui::StatusCard>();
        card->set(rkui::StatusCard::Kind::Info, tr("Quellen"),
                  u8("Noch keine Quelle. Eine Quelle ist ein Eingang deines Audio-Interfaces oder ein eingebautes Testsignal."));
        inspector_.add(std::move(card), 84);
        auto* add = inspector_.add(rkui::makeButton("+ " + tr("Quelle hinzufügen"), true), 34);
        add->onClick = [this] { addSource(); };
        return;
    }
    const int i = selSource_;
    rkui::SourceInfo& s = scene_.sources[(size_t)i];
    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Quelle") + " " + juce::String(i + 1)), 26);
    auto* name = inspector_.add(std::make_unique<rkui::TextRow>(tr("Name")), 34);
    name->editor().setText(s.name, false);
    name->onCommit = [this, i](const juce::String& t) {
        if (i >= (int)scene_.sources.size() || scene_.sources[(size_t)i].name == t) return;
        scene_.sources[(size_t)i].name = t.substring(0, 64);
        commit();
        refreshSidebar();
    };

    juce::StringArray inputs;
    const int numIn = juce::jmax(2, host_.numDeviceInputs());
    for (int k = 0; k < numIn; ++k) inputs.add(tr("Eingang") + " " + juce::String(k + 1));
    inputs.add(tr("Testsignal") + ": " + tr("Rosa Rauschen"));
    inputs.add(tr("Testsignal") + ": " + tr("Sinuston"));
    inputs.add(tr("Testsignal") + ": " + tr("Klicks"));
    auto* input = inspector_.add(std::make_unique<rkui::ChoiceRow>(tr("Eingang"), inputs), 36);
    input->box().setSelectedItemIndex(s.test != rkui::TestSignal::Off ? numIn + (int)s.test - 1 : juce::jmin(s.inputChannel, numIn - 1), juce::dontSendNotification);
    input->onChange = [this, i, numIn](int idx) {
        if (i >= (int)scene_.sources.size()) return;
        auto& src = scene_.sources[(size_t)i];
        if (idx < numIn) { src.test = rkui::TestSignal::Off; src.inputChannel = idx; }
        else src.test = static_cast<rkui::TestSignal>(idx - numIn + 1);
        applyScene(false);
        commit();
        refreshSidebar();
    };

    auto setPolar = [this, i](int which, double v) {
        if (i >= (int)scene_.sources.size()) return;
        auto& src = scene_.sources[(size_t)i];
        rk::Polar p = rk::toPolar(src.position);
        if (p.distance < 0.001f) p.distance = 0.001f;
        if (which == 0) p.azimuthDeg = (float)v;
        if (which == 1) p.elevationDeg = (float)v;
        if (which == 2) p.distance = (float)v;
        src.position = rk::toCartesian(p);
        applyScene(false);
        stage_.setScene(scene_);
        refreshSidebar();
    };
    const rk::Polar pol = rk::toPolar(s.position);
    auto* az = inspector_.add(std::make_unique<rkui::ParamSlider>(tr("Azimut"), -180, 180, 0.5, degree(), 1, 0), 32);
    az->setValue(pol.azimuthDeg);
    az->onChange = [setPolar](double v) { setPolar(0, v); };
    az->onCommit = [this] { commit(); };
    auto* el = inspector_.add(std::make_unique<rkui::ParamSlider>(tr("Elevation"), -90, 90, 0.5, degree(), 1, 0), 32);
    el->setValue(pol.elevationDeg);
    el->onChange = [setPolar](double v) { setPolar(1, v); };
    el->onCommit = [this] { commit(); };
    auto* di = inspector_.add(std::make_unique<rkui::ParamSlider>(tr("Abstand"), 0, 50, 0.05, "m", 2, 0), 32);
    di->setSkewForCentre(5.0);
    di->setValue(pol.distance);
    di->onChange = [setPolar](double v) { setPolar(2, v); };
    di->onCommit = [this] { commit(); };

    auto addParam = [this, i](const juce::String& label, double lo, double hi, double step, const juce::String& unit, int dec,
                              double origin, double value, std::function<void(rkui::SourceInfo&, double)> set) {
        auto* p = inspector_.add(std::make_unique<rkui::ParamSlider>(label, lo, hi, step, unit, dec, origin), 32);
        p->setValue(value);
        p->onChange = [this, i, set](double v) {
            if (i >= (int)scene_.sources.size()) return;
            set(scene_.sources[(size_t)i], v);
            applyScene(false);
            stage_.setScene(scene_);
        };
        p->onCommit = [this] { commit(); };
        return p;
    };
    addParam(tr("Pegel"), -60, 12, 0.1, "dB", 1, 0, s.gainDb, [](rkui::SourceInfo& x, double v) { x.gainDb = (float)v; });
    addParam(tr("Breite"), 0, 100, 1, "%", 0, 0, s.spread * 100.0, [](rkui::SourceInfo& x, double v) { x.spread = (float)v / 100.0f; });
    auto* send = addParam(tr("Hallanteil"), -80, 6, 0.5, "dB", 1, -80, s.reverbSendDb, [](rkui::SourceInfo& x, double v) { x.reverbSendDb = (float)v; });
    send->formatter = [](double v) { return v <= -79.9 ? u8("\xe2\x88\x92\xe2\x88\x9e dB") : rkui::num(v, 1) + " dB"; };
    send->setValue(s.reverbSendDb);
    addParam(tr("Entfernung"), 0, 200, 1, "%", 0, 0, s.distanceAmount * 100.0, [](rkui::SourceInfo& x, double v) { x.distanceAmount = (float)v / 100.0f; });

    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Bewegung")), 26);
    auto* motion = inspector_.add(std::make_unique<rkui::Segmented>(juce::StringArray{tr("Aus"), tr("Kreisen")}, false), 32);
    motion->setSelected(s.orbit ? 1 : 0);
    motion->onChange = [this, i](int m) {
        if (i >= (int)scene_.sources.size()) return;
        scene_.sources[(size_t)i].orbit = m == 1;
        commit();
        refreshSidebar();
    };
    auto* speed = addParam(tr("Tempo"), -30, 30, 0.5, "U/min", 1, 0, s.orbitSpeed * 60.0, [](rkui::SourceInfo& x, double v) { x.orbitSpeed = (float)v / 60.0f; });
    speed->setTooltip(u8("Umdrehungen pro Minute, negativ = im Uhrzeigersinn"));

    inspector_.addSpace(4);
    auto* mute = inspector_.add(std::make_unique<juce::ToggleButton>(tr("Stumm")), 28);
    mute->setToggleState(s.mute, juce::dontSendNotification);
    mute->onClick = [this, i, mute] {
        if (i >= (int)scene_.sources.size()) return;
        scene_.sources[(size_t)i].mute = mute->getToggleState();
        applyScene(false);
        commit();
        refreshSidebar();
        stage_.setScene(scene_);
    };
    auto* solo = inspector_.add(std::make_unique<juce::ToggleButton>(tr("Solo")), 28);
    solo->setToggleState(s.solo, juce::dontSendNotification);
    solo->onClick = [this, i, solo] {
        if (i >= (int)scene_.sources.size()) return;
        scene_.sources[(size_t)i].solo = solo->getToggleState();
        applyScene(false);
        commit();
        refreshSidebar();
    };
    auto* rm = inspector_.add(rkui::makeButton(tr("Entfernen"), false, true), 30);
    rm->getProperties().set("danger", true);
    rm->onClick = [this, i] {
        if (i >= (int)scene_.sources.size()) return;
        scene_.sources.erase(scene_.sources.begin() + i);
        selSource_ = juce::jmin(i, (int)scene_.sources.size() - 1);
        applyScene(false);
        commit();
        juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) sp->refreshAll(); });
    };
}

void MainComponent::buildOutputPage() {
    inspector_.clear();
    auto ctx = pageContext();
    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Audiogerät")), 26);
    auto sel = std::make_unique<juce::AudioDeviceSelectorComponent>(host_.devices, 0, AudioHost::kMaxDeviceInputs, 1, rk::kMaxSpeakers,
                                                                    false, false, false, false);
    inspector_.add(std::move(sel), 360);

    const int outs = host_.numDeviceOutputs();
    const int need = scene_.headphones ? 2 : (int)scene_.layout.speakers.size();
    auto card = std::make_unique<rkui::StatusCard>();
    if (outs == 0) card->set(rkui::StatusCard::Kind::Warn, tr("Bitte prüfen"), tr("Kein Audiogerät geöffnet."));
    else if (outs < need) card->set(rkui::StatusCard::Kind::Warn, tr("Bitte prüfen"),
                                    juce::String(outs) + " / " + juce::String(need) + " " + tr("Ausgang") + ". " +
                                        tr("Das Audiogerät hat weniger Ausgänge als Lautsprecher. Die übrigen bleiben stumm."));
    else card->set(rkui::StatusCard::Kind::Ok, tr("Sitzt gut"),
                   juce::String(need) + " / " + juce::String(outs) + " " + tr("Ausgang") + u8(" \xc2\xb7 ") +
                       rkui::num(host_.sampleRate() / 1000.0, 1) + " kHz");
    inspector_.add(std::move(card), 72);

    inspector_.add(std::make_unique<rkui::SectionHeader>(tr("Ausgabe")), 26);
    auto* master = inspector_.add(std::make_unique<rkui::ParamSlider>(tr("Gesamtpegel"), -60, 12, 0.1, "dB", 1, 0), 32);
    master->setValue(scene_.masterGainDb);
    master->onChange = [this](double v) { scene_.masterGainDb = (float)v; applyScene(false); };
    master->onCommit = [this] { commit(); };
    auto* hp = inspector_.add(std::make_unique<juce::ToggleButton>(tr("Kopfhörer-Vorschau")), 28);
    hp->setToggleState(scene_.headphones, juce::dontSendNotification);
    hp->setTooltip(u8("Rechnet alle Lautsprecher auf zwei Kopfh\xc3\xb6rer-Kan\xc3\xa4le herunter. Zum Vorbereiten ohne Lautsprecher."));
    hp->onClick = [this, hp] {
        scene_.headphones = hp->getToggleState();
        applyScene(true);
        commit();
        juce::MessageManager::callAsync([sp = juce::Component::SafePointer<MainComponent>(this)] { if (sp) sp->refreshAll(); });
    };
    auto* lim = inspector_.add(std::make_unique<juce::ToggleButton>(tr("Schutzbegrenzer")), 28);
    lim->setToggleState(scene_.limiter, juce::dontSendNotification);
    lim->setTooltip(u8("Verhindert \xc3\x9c" "bersteuerung und sch\xc3\xbctzt die Lautsprecher. Bitte nur f\xc3\xbcr Messungen ausschalten."));
    lim->onClick = [this, lim] { scene_.limiter = lim->getToggleState(); applyScene(false); commit(); };
    auto* test = inspector_.add(rkui::makeButton(speakerTestRunning_ ? tr("Test beenden") : tr("Lautsprecher einzeln anspielen"), speakerTestRunning_), 32);
    test->onClick = [this] {
        speakerTestRunning_ = !speakerTestRunning_;
        speakerTestIndex_ = juce::jmax(0, selSpeaker_);
        speakerTestNext_ = 0.0;
        if (!speakerTestRunning_) host_.engine.speakerTest = -1;
        rebuildInspector();
    };
}

void MainComponent::addSource() {
    if ((int)scene_.sources.size() >= rkui::kMaxSceneSources) return;
    rkui::SourceInfo s;
    const int n = (int)scene_.sources.size();
    s.name = tr("Quelle") + " " + juce::String(n + 1);
    s.inputChannel = juce::jmin(n, juce::jmax(0, host_.numDeviceInputs() - 1));
    float r = 2.0f;
    for (const auto& sp : scene_.layout.speakers) r = std::max(r, rk::length(sp.position) * 0.7f);
    s.position = rk::toCartesian({std::fmod(30.0f + 47.0f * (float)n, 360.0f) - 180.0f, 0.0f, r});
    scene_.sources.push_back(s);
    selSource_ = n;
    if (step_ != StepSources) step_ = StepSources;
    applyScene(false);
    commit();
    refreshAll();
}

void MainComponent::startDemo() {
    rkui::Scene s;
    s.name = "Demo";
    s.presetId = "ring";
    s.ringCount = 8;
    s.radius = 3.0f;
    rkui::regenerateLayout(s);
    s.masterGainDb = -10.0f;
    const struct { const char* name; rkui::TestSignal t; float az; float speed; } demo[] = {
        {"Klicks", rkui::TestSignal::Clicks, 45.0f, 0.03f},
        {"Rauschen", rkui::TestSignal::Pink, -120.0f, -0.017f},
        {"Ton", rkui::TestSignal::Sine, 170.0f, 0.0f},
    };
    for (const auto& d : demo) {
        rkui::SourceInfo src;
        src.name = tr(d.name);
        src.test = d.t;
        src.position = rk::toCartesian({d.az, 0.0f, 2.4f});
        src.orbit = d.speed != 0.0f;
        src.orbitSpeed = d.speed;
        src.gainDb = d.t == rkui::TestSignal::Pink ? -8.0f : -3.0f;
        s.sources.push_back(src);
    }
    // No multichannel interface: listen on headphones right away.
    if (host_.numDeviceOutputs() < (int)s.layout.speakers.size()) s.headphones = true;
    intro_.reset();
    step_ = StepSources;
    selSource_ = 0;
    loadScene(s, true);
    dirty_ = true;
}

void MainComponent::startEmpty() {
    rkui::Scene s;
    s.presetId = "ring";
    rkui::regenerateLayout(s);
    intro_.reset();
    step_ = StepSpeakers;
    loadScene(s, true);
    dirty_ = true;
}

void MainComponent::saveNow() {
    if (!dirty_) return;
    rkui::writeFileAtomically(rkui::appDataFolder().getChildFile("Autosave.raumklang"), rkui::sceneToJson(scene_));
    if (projectFile_ != juce::File()) rkui::writeFileAtomically(projectFile_, rkui::sceneToJson(scene_));
    dirty_ = false;
}

void MainComponent::openProject() {
    chooser_ = std::make_unique<juce::FileChooser>(tr("Öffnen …"), rkui::appDataFolder(), "*.raumklang");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const juce::File f = fc.getResult();
        if (f == juce::File()) return;
        rkui::Scene s;
        if (f.getSize() < 16 * 1024 * 1024 && rkui::sceneFromJson(f.loadFileAsString(), s)) {
            if (s.name.isEmpty()) s.name = f.getFileNameWithoutExtension();
            projectFile_ = f;
            loadScene(s, true);
        } else {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Raumklang",
                                                   u8("Die Datei ist kein Raumklang-Projekt oder besch\xc3\xa4" "digt."));
        }
    });
}

void MainComponent::saveProjectAs() {
    chooser_ = std::make_unique<juce::FileChooser>(tr("Sichern unter …"), rkui::appDataFolder().getChildFile((scene_.name.isEmpty() ? "Projekt" : scene_.name) + ".raumklang"), "*.raumklang");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
        juce::File f = fc.getResult();
        if (f == juce::File()) return;
        if (!f.hasFileExtension("raumklang")) f = f.withFileExtension("raumklang");
        scene_.name = f.getFileNameWithoutExtension();
        projectFile_ = f;
        dirty_ = true;
        commit();
        saveNow();
        repaint();
    });
}

void MainComponent::applyTheme() {
    lnf_.refreshColours();
    sendLookAndFeelChange();
    refreshAll();
}

void MainComponent::showHelpMenu() {
    juce::PopupMenu m;
    m.addSectionHeader("Raumklang " + juce::String(JUCE_APPLICATION_VERSION_STRING));
    m.addItem(1, tr("Sprache: Deutsch"), true, rkui::language() == rkui::Language::German);
    m.addItem(2, tr("Sprache: Englisch"), true, rkui::language() == rkui::Language::English);
    m.addSeparator();
    m.addItem(3, rkui::language() == rkui::Language::German ? "Dunkel" : "Dark", true, rkui::isDark());
    m.addItem(4, rkui::language() == rkui::Language::German ? "Hell" : "Light", true, !rkui::isDark());
    m.addSeparator();
    m.addItem(5, rkui::language() == rkui::Language::German ? u8("Einf\xc3\xbchrung zeigen") : "Show introduction");
    m.addItem(6, rkui::language() == rkui::Language::German ? "Projektseite auf GitHub" : "Project page on GitHub");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(helpBtn_.get()), [this](int r) {
        if (r == 1 || r == 2) {
            rkui::setLanguage(r == 1 ? rkui::Language::German : rkui::Language::English);
            settings_->setValue("language", r == 1 ? "de" : "en");
            settings_->saveIfNeeded();
            steps_.setItems(juce::StringArray{tr("Lautsprecher"), tr("Quellen"), tr("Raum"), tr("Ausgabe")});
            openBtn_->setButtonText(tr("Öffnen …"));
            saveBtn_->setButtonText(tr("Sichern unter …"));
            addBtn_->setButtonText("+ " + tr("Quelle hinzufügen"));
            refreshAll();
        } else if (r == 3 || r == 4) {
            rkui::setDarkMode(r == 3);
            settings_->setValue("theme", r == 3 ? "dark" : "light");
            settings_->saveIfNeeded();
            applyTheme();
        } else if (r == 5) {
            intro_ = std::make_unique<IntroOverlay>();
            intro_->onDemo = [this] { startDemo(); };
            intro_->onEmpty = [this] { startEmpty(); };
            addAndMakeVisible(*intro_);
            resized();
        } else if (r == 6) {
            juce::URL("https://github.com/BenjaminJMueller/raumklang").launchInDefaultBrowser();
        }
    });
}

// ---------------------------------------------------------------------------

void MainComponent::timerCallback() {
    const double now = juce::Time::getMillisecondCounterHiRes();

    // Moving sources ("Kreisen").
    const double dt = lastOrbitTime_ > 0.0 ? juce::jlimit(0.0, 0.2, (now - lastOrbitTime_) * 0.001) : 0.0;
    lastOrbitTime_ = now;
    bool moved = false;
    for (int i = 0; i < (int)scene_.sources.size(); ++i) {
        auto& s = scene_.sources[(size_t)i];
        if (!s.orbit || s.orbitSpeed == 0.0f) continue;
        const float a = (float)(s.orbitSpeed * dt * juce::MathConstants<double>::twoPi);
        const float c = std::cos(a), sn = std::sin(a);
        s.position = {s.position.x * c - s.position.y * sn, s.position.x * sn + s.position.y * c, s.position.z};
        host_.engine.source(i).setPosition(s.position);
        moved = true;
    }
    if (moved) {
        stage_.setScene(scene_);
        if (step_ == StepSources || step_ == StepRoom) refreshSidebar();
    }

    // Meters.
    static std::vector<float> spk(rk::kMaxSpeakers, 0.0f), src(rk::kMaxSources, 0.0f);
    const int nOut = scene_.headphones ? 2 : (int)scene_.layout.speakers.size();
    for (int c = 0; c < rk::kMaxSpeakers; ++c) spk[(size_t)c] = std::max(levelToUnit(host_.engine.takeOutputPeak(c)), spk[(size_t)c] * 0.85f);
    for (int s = 0; s < rkui::kMaxSceneSources; ++s) src[(size_t)s] = std::max(levelToUnit(host_.engine.takeSourcePeak(s)), src[(size_t)s] * 0.85f);
    std::vector<float> spkView(spk.begin(), spk.begin() + juce::jmax(0, scene_.headphones ? 0 : nOut));
    stage_.setSpeakerLevels(spkView);
    stage_.setSourceLevels(std::vector<float>(src.begin(), src.begin() + (long)scene_.sources.size()));
    if (step_ == StepSpeakers || step_ == StepOutput) sidebar_.setLevels(spkView);
    else sidebar_.setLevels(std::vector<float>(src.begin(), src.begin() + (long)scene_.sources.size()));

    // Speaker test: 1.5 s per speaker, in order.
    if (speakerTestRunning_ && now >= speakerTestNext_) {
        const int n = (int)scene_.layout.speakers.size();
        if (n > 0) {
            speakerTestIndex_ = speakerTestNext_ == 0.0 ? speakerTestIndex_ % n : (speakerTestIndex_ + 1) % n;
            host_.engine.speakerTest = speakerTestIndex_;
            selSpeaker_ = speakerTestIndex_;
            sidebar_.setSelected(selSpeaker_);
            stage_.setSelectedSpeaker(selSpeaker_);
        }
        speakerTestNext_ = now + 1500.0;
    }

    host_.engine.collectGarbage();

    // Autosave one second after the last change.
    if (dirty_ && now - lastChange_ > 1000.0) saveNow();

    const juce::String err = host_.takeError();
    if (err.isNotEmpty()) statusText_ = err;
    static int slow = 0;
    if (++slow % 15 == 0) repaint(0, getHeight() - 40, kSidebar, 40); // CPU display
}

void MainComponent::paint(juce::Graphics& g) {
    const auto& p = rkui::palette();
    g.fillAll(p.bg);
    // Top bar.
    g.setColour(p.bar);
    g.fillRect(0, 0, getWidth(), kTopBar);
    g.setColour(p.line);
    g.drawHorizontalLine(kTopBar - 1, 0.0f, (float)getWidth());
    g.setColour(p.text);
    g.setFont(rkui::uiFont(14.0f, true));
    g.drawText(scene_.name.isEmpty() ? tr("Neues Projekt") : scene_.name, 16, 8, 220, 18, juce::Justification::left);
    g.setColour(p.text3);
    g.setFont(rkui::uiFont(11.5f));
    g.drawText(tr("Automatisch gesichert"), 16, 26, 220, 16, juce::Justification::left);
    // Sidebar.
    g.setColour(p.side);
    g.fillRect(0, kTopBar, kSidebar, getHeight() - kTopBar);
    g.setColour(p.line);
    g.drawVerticalLine(kSidebar - 1, (float)kTopBar, (float)getHeight());
    g.drawVerticalLine(getWidth() - kInspector, (float)kTopBar, (float)getHeight());
    g.setColour(p.side);
    g.fillRect(getWidth() - kInspector + 1, kTopBar, kInspector, getHeight() - kTopBar);
    // Sidebar footer: what is set up and how busy the CPU is.
    auto footer = juce::Rectangle<int>(16, getHeight() - 36, kSidebar - 32, 20);
    g.setColour(p.text2);
    g.setFont(rkui::uiFont(12.0f));
    juce::String info = statusText_.isNotEmpty() ? statusText_
                                                 : juce::String(scene_.layout.speakers.size()) + " " + tr("Lautsprecher") + u8(" \xc2\xb7 ") +
                                                       juce::String(scene_.sources.size()) + " " + tr("Quellen") + u8(" \xc2\xb7 CPU ") +
                                                       juce::String(juce::roundToInt(host_.devices.getCpuUsage() * 100.0)) + " %";
    g.drawText(info, footer, juce::Justification::centredLeft, true);
}

void MainComponent::resized() {
    auto r = getLocalBounds();
    auto top = r.removeFromTop(kTopBar);
    const int sw = juce::jmin(steps_.idealWidth(), top.getWidth() - 560);
    steps_.setBounds(top.withSizeKeepingCentre(sw, 34));
    auto right = top.reduced(10, 10);
    helpBtn_->setBounds(right.removeFromRight(32));
    right.removeFromRight(6);
    saveBtn_->setBounds(right.removeFromRight(124));
    openBtn_->setBounds(right.removeFromRight(86));
    right.removeFromRight(6);
    redoBtn_->setBounds(right.removeFromRight(32));
    undoBtn_->setBounds(right.removeFromRight(32));

    auto side = r.removeFromLeft(kSidebar);
    side.removeFromBottom(44);
    if (addBtn_->isVisible()) addBtn_->setBounds(side.removeFromBottom(44).reduced(12, 6));
    sidebarView_.setBounds(side.reduced(0, 4));
    sidebar_.setSize(sidebarView_.getMaximumVisibleWidth(), sidebar_.contentHeight());
    inspector_.setBounds(r.removeFromRight(kInspector).reduced(0, 8));
    stage_.setBounds(r);
    if (intro_ != nullptr) intro_->setBounds(getLocalBounds());
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (key.getKeyCode() == 'Z' && mods.isCommandDown()) {
        if (mods.isShiftDown()) redo();
        else undo();
        return true;
    }
    if (key.getKeyCode() == 'Y' && mods.isCommandDown()) { redo(); return true; }
    if (key.getKeyCode() == 'S' && mods.isCommandDown()) { saveProjectAs(); return true; }
    if (key.getKeyCode() == 'O' && mods.isCommandDown()) { openProject(); return true; }
    if (key.getKeyCode() >= '1' && key.getKeyCode() <= '4' && mods.isCommandDown()) { setStep(key.getKeyCode() - '1'); return true; }
    return false;
}

} // namespace rkapp
