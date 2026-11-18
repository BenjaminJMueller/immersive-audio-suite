#include "SourceEditor.h"

using rkui::tr;
using rkui::u8;

namespace rkplug {
namespace {

/// German or English text, both UTF-8.
juce::String L(const char* de, const char* en) {
    return juce::String::fromUTF8(rkui::language() == rkui::Language::German ? de : en);
}

juce::String rateText(double hz) {
    return juce::String(hz / 1000.0, std::abs(hz - 44100.0) < 1.0 || std::abs(hz - 88200.0) < 1.0 ? 1 : 0) + " kHz";
}

const juce::String kDegree = juce::String::fromUTF8("\xc2\xb0");

} // namespace

juce::AudioProcessorEditor* SourceProcessor::createEditor() { return new SourceEditor(*this); }

SourceEditor::SourceEditor(SourceProcessor& p) : AudioProcessorEditor(p), proc_(p) {
    rkui::setDarkMode(true);
    lnf_.refreshColours();
    setLookAndFeel(&lnf_);
    stage_.setEditTarget(rkui::StageView::EditTarget::Sources);
    stage_.setSelectedSource(0);
    stage_.onSourceMoved = [this](int, rk::Vec3 pos) {
        if (!dragGesture_) {
            for (auto* id : {"azimuth", "elevation", "distance"})
                if (auto* prm = proc_.params.getParameter(id)) prm->beginChangeGesture();
            dragGesture_ = true;
        }
        proc_.setSourcePosition({pos.x, pos.y, proc_.sourcePosition().z});
    };
    stage_.onDragEnd = [this] {
        if (!dragGesture_) return;
        for (auto* id : {"azimuth", "elevation", "distance"})
            if (auto* prm = proc_.params.getParameter(id)) prm->endChangeGesture();
        dragGesture_ = false;
    };
    stage_.setCoach("1", L("Quelle platzieren", "Place the source"),
                    L("Den Punkt in der Ansicht ziehen oder rechts die Regler nutzen.", "Drag the dot or use the controls on the right."));
    addAndMakeVisible(stage_);
    addAndMakeVisible(page_);
    buildPage();

    setResizable(true, true);
    setResizeLimits(720, 480, 2000, 1400);
    setSize(980, 620);
    refreshStage();
    refreshStatus();
    startTimerHz(30);
}

SourceEditor::~SourceEditor() {
    stopTimer();
    bindings_.clear();
    setLookAndFeel(nullptr);
}

void SourceEditor::buildPage() {
    bindings_.clear();
    page_.clear();
    card_ = page_.add(std::make_unique<rkui::StatusCard>(), 72);

    page_.add(std::make_unique<rkui::SectionHeader>(
                  tr("Ausgabe"),
                  L("Automatisch: L\xc3\xa4uft die Raumklang-App, spielt sie diese Spur, ganz ohne Routing. Sonst klingt die Spur in der DAW, "
                    "mit einem Kanal pro Lautsprecher.",
                    "Automatic: when the Raumklang app runs, it plays this track without any routing. Otherwise the track plays in the DAW, "
                    "with one channel per speaker.")),
              26);
    routeRow_ = page_.add(std::make_unique<rkui::ChoiceRow>(L("Ziel", "Destination"),
                                                            juce::StringArray{L("Automatisch", "Automatic"), L("An die App", "To the app"),
                                                                              L("In der DAW", "In the DAW")}),
                          26);
    auto* routePrm = proc_.params.getParameter("route");
    routeRow_->box().setSelectedItemIndex(juce::roundToInt(routePrm->convertFrom0to1(routePrm->getValue())), juce::dontSendNotification);
    routeRow_->onChange = [this, routePrm](int i) {
        routePrm->beginChangeGesture();
        routePrm->setValueNotifyingHost(routePrm->convertTo0to1((float)i));
        routePrm->endChangeGesture();
        refreshStatus();
    };

    page_.add(std::make_unique<rkui::SectionHeader>(
                  tr("Position"), L("Azimut 0\xc2\xb0 ist vorne, 90\xc2\xb0 links. Alle Werte lassen sich in der DAW automatisieren.",
                                    "Azimuth 0\xc2\xb0 is front, 90\xc2\xb0 left. Every value can be automated in the DAW.")),
              26);
    addSlider("azimuth", tr("Azimut"), kDegree, 1, 0, L("Richtung um den H\xc3\xb6rplatz", "Direction around the listening spot"));
    addSlider("elevation", tr("Elevation"), kDegree, 1, 0, L("H\xc3\xb6he \xc3\xbc" "ber oder unter der Ebene", "Height above or below the plane"));
    addSlider("distance", tr("Abstand"), "m", 2, 0, L("Abstand zur Mitte", "Distance from the centre"));

    page_.add(std::make_unique<rkui::SectionHeader>(tr("Klang")), 26);
    addSlider("gain", tr("Pegel"), "dB", 1, 0, {});
    addSlider("spread", tr("Breite"), "%", 0, 0, L("Verteilt die Quelle auf mehr Lautsprecher", "Spreads the source over more speakers"));
    addSlider("stereoWidth", L("Stereobreite", "Stereo width"), kDegree, 0, 0,
              L("Winkel zwischen links und rechts einer Stereospur", "Angle between left and right of a stereo track"));
    addSlider("reverbSend", tr("Hallanteil"), "dB", 1, -80, {});
    addSlider("distanceAmount", tr("Entfernung"), "%", 0, 0,
              L("Wie stark Abstand leiser und dumpfer macht", "How much distance makes the source quieter and duller"));

    page_.addDetails(L("Ein Plugin pro Spur. L\xc3\xa4uft die Raumklang-App, erscheint die Spur dort in der Liste \xe2\x80\x9e" "Aus der DAW\xe2\x80\x9c. "
                       "Die App spielt sie \xc3\xbc" "ber ihr eigenes Audioger\xc3\xa4t, die Spur in der DAW bleibt still. Dazu muss nichts geroutet werden.\n\n"
                       "Ohne App gibt die Spur einen Kanal pro Lautsprecher aus. Stelle die Spur dann auf so viele Kan\xc3\xa4le, wie der Aufbau Lautsprecher hat, "
                       "und lege \xe2\x80\x9eRaumklang Raum\xe2\x80\x9c auf den Master.\n\n"
                       "DAW und App m\xc3\xbcssen mit derselben Abtastrate laufen.",
                       "One plugin per track. When the Raumklang app runs, the track appears there under \"From the DAW\". "
                       "The app plays it through its own audio device and the DAW track stays silent. Nothing needs to be routed.\n\n"
                       "Without the app the track outputs one channel per speaker. Set the track to as many channels as the setup has speakers "
                       "and put \"Raumklang Room\" on the master.\n\n"
                       "The DAW and the app must run at the same sample rate."));
}

rkui::ParamSlider& SourceEditor::addSlider(const char* id, const juce::String& label, const juce::String& unit, int decimals,
                                           double origin, const juce::String& tip) {
    auto* prm = proc_.params.getParameter(id);
    const auto range = prm->getNormalisableRange();
    auto* s = page_.add(std::make_unique<rkui::ParamSlider>(label, range.start, range.end, range.interval, unit, decimals, origin), 32);
    if (std::string(id) == "distance") s->setSkewForCentre(5.0);
    if (tip.isNotEmpty()) s->setTooltip(tip);
    bindings_.push_back(std::make_unique<ParamBinding>(*s, *prm));
    return *s;
}

void SourceEditor::refreshStatus() {
    if (card_ == nullptr) return;
    const auto st = proc_.linkStatus();
    const int need = (int)shown_.layout.speakers.size();
    const int have = proc_.outputChannels();
    using K = rkui::StatusCard::Kind;
    K kind = K::Info;
    juce::String title, detail;
    int filled = 0, total = 0;

    auto dawText = [&](const juce::String& lead) {
        if (have < need) {
            kind = K::Warn;
            detail = lead + L("Stelle die Spur auf ", "Set the track to ") + juce::String(need) +
                     L(" Kan\xc3\xa4le, sonst bleiben Lautsprecher stumm.", " channels, otherwise speakers stay silent.");
        } else {
            detail = lead + juce::String(need) + " " + tr("Lautsprecher") + u8(" \xc2\xb7 ") +
                     juce::String(rk::pannerId(shown_.effectivePanner())).toUpperCase() + ".";
        }
        filled = juce::jmin(have, need);
        total = need;
    };

    if (st.route == SourceProcessor::Route::Daw) {
        kind = K::Ok;
        title = L("Klingt in der DAW", "Plays in the DAW");
        dawText({});
    } else if (st.sending) {
        kind = K::Ok;
        title = L("Verbunden mit Raumklang", "Connected to Raumklang");
        detail = L("Die App spielt diese Spur. Routing ist nicht n\xc3\xb6tig.", "The app plays this track. No routing needed.");
    } else if (!st.bridgeAvailable) {
        kind = K::Warn;
        title = L("Keine Verbindung m\xc3\xb6glich", "Cannot connect");
        detail = st.route == SourceProcessor::Route::App
                     ? L("Dieser Host erlaubt keine Verbindung zur App. W\xc3\xa4hle als Ziel \xe2\x80\x9eIn der DAW\xe2\x80\x9c.",
                         "This host does not allow a connection to the app. Choose \"In the DAW\" as destination.")
                     : L("Dieser Host erlaubt keine Verbindung zur App. Die Spur klingt in der DAW.",
                         "This host does not allow a connection to the app. The track plays in the DAW.");
    } else if (st.appRunning && st.rateMismatch) {
        kind = K::Warn;
        title = L("Abtastrate passt nicht", "Sample rate differs");
        detail = L("Die App l\xc3\xa4uft mit ", "The app runs at ") + rateText(st.appSampleRate) + L(", die DAW mit ", ", the DAW at ") +
                 rateText(st.sampleRate) + L(". Stelle beide gleich ein.", ". Set both to the same rate.");
        if (st.route == SourceProcessor::Route::Automatic)
            detail << L(" Bis dahin klingt die Spur in der DAW.", " Until then the track plays in the DAW.");
    } else if (st.appRunning && st.slot < 0) {
        kind = K::Warn;
        title = L("Alle Pl\xc3\xa4tze belegt", "All places taken");
        detail = L("Die App nimmt bis zu 32 Spuren an. Stelle andere Spuren auf \xe2\x80\x9eIn der DAW\xe2\x80\x9c.",
                   "The app accepts up to 32 tracks. Set other tracks to \"In the DAW\".");
    } else if (st.appRunning) {
        kind = K::Info;
        title = L("Verbinde \xe2\x80\xa6", "Connecting \xe2\x80\xa6");
        detail = L("Sobald die DAW abspielt, kommt die Spur in der App an.", "As soon as the DAW plays, the track arrives in the app.");
    } else if (st.route == SourceProcessor::Route::App) {
        kind = K::Warn;
        title = L("App nicht gefunden", "App not found");
        detail = L("Die Spur bleibt still, bis die Raumklang-App l\xc3\xa4uft.", "The track stays silent until the Raumklang app runs.");
    } else {
        kind = K::Info;
        title = L("Klingt in der DAW", "Plays in the DAW");
        dawText(L("Starte die Raumklang-App, dann verbindet sich die Spur selbst. ", "Start the Raumklang app and the track connects by itself. "));
    }

    const juce::String key = juce::String((int)kind) + title + detail + juce::String(filled) + "/" + juce::String(total);
    if (key == cardKey_) return;
    cardKey_ = key;
    card_->set(kind, title, detail);
    card_->setProgress(filled, total);
    page_.setRowHeight(card_, card_->idealHeight(juce::jmax(200, page_.getWidth() - 24)));
}

void SourceEditor::refreshStage() {
    const rkui::Scene setup = proc_.setupSnapshot();
    const juce::String json = rkui::sceneToJson(setup);
    if (json != shownSetupJson_) {
        shownSetupJson_ = json;
        shown_ = setup;
        cardKey_.clear();
    }
    shown_.sources.resize(1);
    shown_.sources[0].name = "Quelle";
    shown_.sources[0].position = proc_.sourcePosition();
    shown_.sources[0].spread = proc_.params.getRawParameterValue("spread")->load() * 0.01f;
    stage_.setScene(shown_);
}

void SourceEditor::timerCallback() {
    for (auto& b : bindings_) b->refresh();
    if (routeRow_ != nullptr && !routeRow_->box().isPopupActive()) {
        auto* prm = proc_.params.getParameter("route");
        const int i = juce::roundToInt(prm->convertFrom0to1(prm->getValue()));
        if (routeRow_->box().getSelectedItemIndex() != i) routeRow_->box().setSelectedItemIndex(i, juce::dontSendNotification);
    }
    refreshStage();
    refreshStatus();
    const bool inDaw = !proc_.linkStatus().sending;
    levels_.resize(shown_.layout.speakers.size());
    for (size_t c = 0; c < levels_.size(); ++c) {
        const float db = inDaw ? rk::gainToDb(proc_.takeOutputPeak((int)c)) : -100.0f;
        levels_[c] = std::max(juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f), levels_[c] * 0.85f);
    }
    stage_.setSpeakerLevels(levels_);
    const float in = juce::jlimit(0.0f, 1.0f, (rk::gainToDb(proc_.takeInputPeak()) + 60.0f) / 60.0f);
    const float next = std::max(in, inputLevel_ * 0.85f);
    if (std::abs(next - inputLevel_) > 0.005f) {
        inputLevel_ = next;
        repaint(0, 0, getWidth(), 52);
    }
}

void SourceEditor::paint(juce::Graphics& g) {
    const auto& p = rkui::palette();
    g.fillAll(p.side);
    g.setColour(p.bar);
    g.fillRect(0, 0, getWidth(), 52);
    g.setColour(p.line);
    g.fillRect(0, 51, getWidth(), 1);
    g.setColour(p.text);
    g.setFont(rkui::uiFont(15.0f, true));
    g.drawText("Raumklang Quelle", 16, 0, 220, 52, juce::Justification::centredLeft);

    // Input meter at the right of the bar: shows that audio arrives from the track.
    auto meter = juce::Rectangle<float>((float)getWidth() - 16.0f - 120.0f, 23.0f, 120.0f, 6.0f);
    g.setColour(p.text3);
    g.setFont(rkui::uiFont(12.0f));
    g.drawText(L("Eingang", "Input"), meter.withX(meter.getX() - 70.0f).withWidth(62.0f).withY(14.0f).withHeight(24.0f).toNearestInt(),
               juce::Justification::centredRight);
    g.setColour(p.group);
    g.fillRoundedRectangle(meter, 3.0f);
    g.setColour(inputLevel_ > 0.97f ? p.warn : p.ok);
    g.fillRoundedRectangle(meter.withWidth(meter.getWidth() * inputLevel_), 3.0f);
}

void SourceEditor::resized() {
    auto r = getLocalBounds();
    r.removeFromTop(52);
    page_.setBounds(r.removeFromRight(juce::jmax(330, getWidth() / 3)).reduced(0, 8));
    stage_.setBounds(r);
    cardKey_.clear();
}

} // namespace rkplug
