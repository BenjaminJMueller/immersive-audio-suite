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
constexpr int kBarHeight = 46;

/// "› Erweitert": opens and closes the rarely needed settings.
class ExpandRow : public juce::Component {
public:
    ExpandRow(juce::String title, bool open, std::function<void()> toggle) : title_(std::move(title)), open_(open), toggle_(std::move(toggle)) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    void paint(juce::Graphics& g) override {
        const auto& p = rkui::palette();
        const auto c = isMouseOver() ? p.text : p.text2;
        juce::Path chev;
        const float cx = 14.0f, cy = getHeight() * 0.5f;
        if (open_) {
            chev.startNewSubPath(cx - 4, cy - 2);
            chev.lineTo(cx, cy + 2);
            chev.lineTo(cx + 4, cy - 2);
        } else {
            chev.startNewSubPath(cx - 2, cy - 4);
            chev.lineTo(cx + 2, cy);
            chev.lineTo(cx - 2, cy + 4);
        }
        g.setColour(c);
        g.strokePath(chev, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont(rkui::uiFont(13.0f));
        g.drawText(title_, getLocalBounds().withTrimmedLeft(26), juce::Justification::centredLeft);
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!e.mouseWasClicked()) return;
        // The page is rebuilt (and this row deleted) after the click has been handled.
        juce::MessageManager::callAsync([t = toggle_] { t(); });
    }

private:
    juce::String title_;
    bool open_;
    std::function<void()> toggle_;
};

} // namespace

juce::AudioProcessorEditor* SourceProcessor::createEditor() { return new SourceEditor(*this); }

SourceEditor::SourceEditor(SourceProcessor& p) : AudioProcessorEditor(p), proc_(p) {
    rkui::setDarkMode(true);
    lnf_.refreshColours();
    setLookAndFeel(&lnf_);

    room_.setSelectedSource(0);
    room_.setFirstLockedSource(1);
    room_.setMode(static_cast<rkui::RoomView::Mode>(
        juce::jlimit(0, 3, (int)proc_.params.state.getProperty("view", (int)rkui::RoomView::Mode::Split))));
    room_.onModeChanged = [this](rkui::RoomView::Mode m) { proc_.params.state.setProperty("view", (int)m, nullptr); };
    room_.onSourceMoved = [this](int i, rk::Vec3 pos) {
        if (i != 0) return;
        if (!dragGesture_) {
            for (auto* id : {"azimuth", "elevation", "distance"})
                if (auto* prm = proc_.params.getParameter(id)) prm->beginChangeGesture();
            dragGesture_ = true;
        }
        proc_.setSourcePosition(pos);
    };
    room_.onDragEnd = [this] {
        if (!dragGesture_) return;
        for (auto* id : {"azimuth", "elevation", "distance"})
            if (auto* prm = proc_.params.getParameter(id)) prm->endChangeGesture();
        dragGesture_ = false;
    };
    addAndMakeVisible(room_);
    addAndMakeVisible(page_);

    // The name: follows the DAW track; a click renames it, an empty name follows the DAW again.
    name_.setFont(rkui::uiFont(15.0f, true));
    name_.setColour(juce::Label::textColourId, rkui::palette().text);
    name_.setEditable(true, false, false);
    name_.setTooltip(L("Klicken zum Umbenennen. Leer lassen, um den Namen der DAW-Spur zu verwenden.",
                       "Click to rename. Leave empty to use the name of the DAW track."));
    name_.onEditorShow = [this] {
        if (auto* ed = name_.getCurrentTextEditor()) {
            ed->setText(proc_.customName().isNotEmpty() ? proc_.customName() : proc_.dawTrackName(), false);
            ed->selectAll();
        }
    };
    name_.onTextChange = [this] {
        const auto t = name_.getText().trim();
        proc_.setCustomName(t == proc_.dawTrackName() ? juce::String() : t);
        name_.setText(proc_.displayName(), juce::dontSendNotification);
    };
    addAndMakeVisible(name_);

    help_.setTooltip(L("Ziehe die Quelle in der Ansicht oder nutze die Regler. Alles l\xc3\xa4sst sich in der DAW automatisieren. "
                       "L\xc3\xa4uft die Raumklang-App, spielt sie diese Spur, ganz ohne Routing.",
                       "Drag the source in the view or use the controls. Everything can be automated in the DAW. "
                       "When the Raumklang app runs, it plays this track without any routing."));
    addAndMakeVisible(help_);

    buildPage();
    setResizable(true, true);
    setResizeLimits(820, 520, 2400, 1600);
    setSize(1100, 680);
    others_.open();
    refreshStage();
    refreshNotice();
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
    routeRow_ = nullptr;

    page_.add(std::make_unique<rkui::SectionHeader>(
                  tr("Position"), L("Azimut 0\xc2\xb0 ist vorne, 90\xc2\xb0 links. Elevation hebt die Quelle \xc3\xbc" "ber die Ohrh\xc3\xb6he.",
                                    "Azimuth 0\xc2\xb0 is front, 90\xc2\xb0 left. Elevation lifts the source above ear height.")),
              26);
    addSlider("azimuth", tr("Azimut"), kDegree, 1, 0, {});
    addSlider("elevation", tr("Elevation"), kDegree, 1, 0, {});
    addSlider("distance", tr("Abstand"), "m", 2, 0, {});

    page_.add(std::make_unique<rkui::SectionHeader>(tr("Klang")), 26);
    addSlider("gain", tr("Pegel"), "dB", 1, 0, {});
    addSlider("spread", tr("Breite"), "%", 0, 0, L("Verteilt die Quelle auf mehr Lautsprecher", "Spreads the source over more speakers"));

    page_.add(std::make_unique<rkui::SectionHeader>(
                  tr("Raum"), L("Wie viel von dieser Spur in den Hall geht. Den Hall selbst stellst du in der Raumklang-App unter \xe2\x80\x9e" "3 Raum\xe2\x80\x9c ein.",
                                "How much of this track goes into the reverb. The reverb itself is set in the Raumklang app under \"3 Room\".")),
              26);
    addSlider("reverbSend", L("Raumanteil", "Room amount"), "dB", 1, -80, {});
    addSlider("distanceAmount", L("Entfernungseffekt", "Distance effect"), "%", 0, 0,
              L("Wie stark Abstand leiser und dumpfer macht", "How much distance makes the source quieter and duller"));

    page_.addSpace(6);
    page_.add(std::make_unique<ExpandRow>(L("Erweitert", "Advanced"), advancedOpen_, [safe = juce::Component::SafePointer<SourceEditor>(this)] {
                  if (safe == nullptr) return;
                  safe->advancedOpen_ = !safe->advancedOpen_;
                  safe->buildPage();
              }),
              30);
    if (advancedOpen_) {
        routeRow_ = page_.add(std::make_unique<rkui::ChoiceRow>(L("Ziel", "Destination"),
                                                                juce::StringArray{L("Automatisch", "Automatic"), L("An die App", "To the app"),
                                                                                  L("In der DAW", "In the DAW")}),
                              32);
        routeRow_->setTooltip(L("Automatisch: L\xc3\xa4uft die Raumklang-App, spielt sie die Spur. Sonst klingt die Spur in der DAW, "
                                "mit einem Kanal pro Lautsprecher.",
                                "Automatic: when the Raumklang app runs, it plays the track. Otherwise the track plays in the DAW, "
                                "with one channel per speaker."));
        auto* routePrm = proc_.params.getParameter("route");
        routeRow_->box().setSelectedItemIndex(juce::roundToInt(routePrm->convertFrom0to1(routePrm->getValue())), juce::dontSendNotification);
        routeRow_->onChange = [this, routePrm](int i) {
            routePrm->beginChangeGesture();
            routePrm->setValueNotifyingHost(routePrm->convertTo0to1((float)i));
            routePrm->endChangeGesture();
            refreshNotice();
        };
        addSlider("stereoWidth", L("Stereobreite", "Stereo width"), kDegree, 0, 0,
                  L("Winkel zwischen links und rechts einer Stereospur", "Angle between left and right of a stereo track"));
    }
    page_.relayout();
}

rkui::ParamSlider& SourceEditor::addSlider(const char* id, const juce::String& label, const juce::String& unit, int decimals,
                                           double origin, const juce::String& tip) {
    auto* prm = proc_.params.getParameter(id);
    const auto range = prm->getNormalisableRange();
    auto* s = page_.add(std::make_unique<rkui::ParamSlider>(label, range.start, range.end, range.interval, unit, decimals, origin), 34);
    if (std::string(id) == "distance") s->setSkewForCentre(5.0);
    if (tip.isNotEmpty()) s->setTooltip(tip);
    bindings_.push_back(std::make_unique<ParamBinding>(*s, *prm));
    return *s;
}

void SourceEditor::refreshNotice() {
    // Only what needs attention. When the app plays the track, nothing is shown.
    const auto st = proc_.linkStatus();
    const int need = (int)shown_.layout.speakers.size();
    const int have = proc_.outputChannels();
    NoticeKind kind = NoticeKind::None;
    juce::String text;
    const juce::String channels = L("Spur auf ", "Set the track to ") + juce::String(need) +
                                  L(" Kan\xc3\xa4le stellen, sonst bleiben Lautsprecher stumm.", " channels, otherwise speakers stay silent.");
    auto inDaw = [&](const juce::String& lead) {
        if (have < need) {
            kind = NoticeKind::Warn;
            text = lead + channels;
        } else {
            kind = lead.isEmpty() ? NoticeKind::None : NoticeKind::Info;
            text = lead + L("Die Spur klingt in der DAW.", "The track plays in the DAW.");
        }
    };

    if (st.route == SourceProcessor::Route::Daw) {
        inDaw({});
    } else if (st.sending) {
        kind = NoticeKind::None;
    } else if (!st.bridgeAvailable) {
        kind = NoticeKind::Warn;
        text = st.route == SourceProcessor::Route::App
                   ? L("Dieser Host erlaubt keine Verbindung zur App. Unter Erweitert \xe2\x80\x9eIn der DAW\xe2\x80\x9c w\xc3\xa4hlen.",
                       "This host does not allow a connection to the app. Choose \"In the DAW\" under Advanced.")
                   : L("Dieser Host erlaubt keine Verbindung zur App. Die Spur klingt in der DAW.",
                       "This host does not allow a connection to the app. The track plays in the DAW.");
    } else if (st.appRunning && st.rateMismatch) {
        kind = NoticeKind::Warn;
        text = L("Abtastrate passt nicht: App ", "Sample rate differs: app ") + rateText(st.appSampleRate) + L(", DAW ", ", DAW ") +
               rateText(st.sampleRate) + L(". Beide gleich einstellen.", ". Set both to the same rate.");
    } else if (st.appRunning && st.slot < 0) {
        kind = NoticeKind::Warn;
        text = L("Alle 32 Pl\xc3\xa4tze in der App belegt. Andere Spuren unter Erweitert auf \xe2\x80\x9eIn der DAW\xe2\x80\x9c stellen.",
                 "All 32 places in the app are taken. Set other tracks to \"In the DAW\" under Advanced.");
    } else if (st.appRunning) {
        kind = NoticeKind::None; // connected, waiting for the DAW to play
    } else if (st.route == SourceProcessor::Route::App) {
        kind = NoticeKind::Warn;
        text = L("Raumklang-App nicht ge\xc3\xb6" "ffnet. Die Spur bleibt still.", "Raumklang app not open. The track stays silent.");
    } else {
        inDaw(L("Raumklang-App nicht ge\xc3\xb6" "ffnet. ", "Raumklang app not open. "));
    }
    if (kind != noticeKind_ || text != notice_) {
        noticeKind_ = kind;
        notice_ = text;
        repaint(0, 0, getWidth(), kBarHeight);
    }
}

juce::Colour SourceEditor::ownColour() const {
    const auto c = proc_.dawTrackColour();
    return c.isTransparent() ? rkui::itemColour(0) : c.withAlpha(1.0f);
}

void SourceEditor::scanOtherTracks() {
    otherTracks_.clear();
    const int own = proc_.linkStatus().slot;
    for (auto& t : others_.scan())
        if (t.slot != own) otherTracks_.push_back(std::move(t));
}

void SourceEditor::refreshStage() {
    const rkui::Scene setup = proc_.setupSnapshot();
    const juce::String json = rkui::sceneToJson(setup);
    if (json != shownSetupJson_) {
        shownSetupJson_ = json;
        shown_ = setup;
        rk::DbapPanner::Params d{shown_.dbapRolloffDb, shown_.dbapBlur, shown_.dbapDirectivity};
        panner_.prepare(shown_.layout, shown_.effectivePanner(), d, 3);
    }
    shown_.sources.resize(1 + otherTracks_.size());
    auto& own = shown_.sources[0];
    own = {};
    own.name = proc_.displayName();
    own.position = proc_.sourcePosition();
    own.spread = proc_.params.getRawParameterValue("spread")->load() * 0.01f;
    std::vector<juce::Colour> colours{ownColour()};
    for (size_t i = 0; i < otherTracks_.size(); ++i) {
        auto& s = shown_.sources[i + 1];
        const auto& t = otherTracks_[i].params;
        s = {};
        s.name = juce::String::fromUTF8(otherTracks_[i].name.c_str());
        s.position = rk::toCartesian({t.azimuthDeg, t.elevationDeg, t.distance});
        colours.push_back(juce::Colours::transparentBlack);
    }
    room_.setSourceStyle(colours, 1, true);
    room_.setScene(shown_);
}

void SourceEditor::timerCallback() {
    for (auto& b : bindings_) b->refresh();
    if (routeRow_ != nullptr && !routeRow_->box().isPopupActive()) {
        auto* prm = proc_.params.getParameter("route");
        const int i = juce::roundToInt(prm->convertFrom0to1(prm->getValue()));
        if (routeRow_->box().getSelectedItemIndex() != i) routeRow_->box().setSelectedItemIndex(i, juce::dontSendNotification);
    }
    if (--scanCountdown_ <= 0) {
        scanCountdown_ = 15; // twice a second is plenty for where the other tracks are
        scanOtherTracks();
        refreshNotice();
    }
    if (!name_.isBeingEdited() && name_.getText() != proc_.displayName()) name_.setText(proc_.displayName(), juce::dontSendNotification);
    refreshStage();

    // What this track plays where: its input level spread over the speakers it pans to.
    const float in = juce::jlimit(0.0f, 1.0f, (rk::gainToDb(proc_.takeInputPeak()) + 60.0f) / 60.0f);
    inputLevel_ = std::max(in, inputLevel_ * 0.85f);
    const size_t n = shown_.layout.speakers.size();
    levels_.assign(n, 0.0f);
    if (n > 0 && inputLevel_ > 0.001f) {
        std::array<float, rk::kMaxSpeakers> gains{};
        panner_.compute({shown_.sources[0].position, shown_.sources[0].spread}, gains.data());
        for (size_t m = 0; m < n; ++m) levels_[m] = inputLevel_ * juce::jlimit(0.0f, 1.0f, gains[m]);
    }
    room_.setSpeakerLevels(levels_);
    room_.setSourceLevels({inputLevel_});
}

void SourceEditor::paint(juce::Graphics& g) {
    const auto& p = rkui::palette();
    g.fillAll(p.side);
    g.setColour(p.bar);
    g.fillRect(0, 0, getWidth(), kBarHeight);
    g.setColour(p.line);
    g.fillRect(0, kBarHeight - 1, getWidth(), 1);

    // Track colour dot before the name, the plugin's name after it.
    g.setColour(ownColour());
    g.fillEllipse(16.0f, kBarHeight * 0.5f - 6.0f, 12.0f, 12.0f);
    const float nameW = juce::GlyphArrangement::getStringWidth(name_.getFont(), name_.getText()) + 12.0f;
    juce::Path pen; // pencil: the name can be edited
    const float px = (float)name_.getX() + std::min(nameW, (float)name_.getWidth()) + 2.0f, py = kBarHeight * 0.5f;
    pen.startNewSubPath(px, py + 5);
    pen.lineTo(px + 1, py + 1.5f);
    pen.lineTo(px + 7, py - 4.5f);
    pen.lineTo(px + 9.5f, py - 2);
    pen.lineTo(px + 3.5f, py + 4);
    pen.closeSubPath();
    g.setColour(p.text3);
    g.strokePath(pen, juce::PathStrokeType(1.2f));
    g.setFont(rkui::uiFont(12.5f));
    g.drawText("Raumklang Quelle", juce::Rectangle<float>(px + 20.0f, 0.0f, 160.0f, (float)kBarHeight), juce::Justification::centredLeft);

    if (noticeKind_ != NoticeKind::None)
        rkui::drawNoticePill(g, px + 200.0f, (float)help_.getX() - 10.0f, kBarHeight * 0.5f, noticeKind_ == NoticeKind::Warn, notice_);
}

void SourceEditor::resized() {
    auto r = getLocalBounds();
    auto bar = r.removeFromTop(kBarHeight);
    help_.setBounds(bar.removeFromRight(46).withSizeKeepingCentre(30, 28));
    name_.setBounds(36, 0, 260, kBarHeight);
    name_.setBorderSize(juce::BorderSize<int>(0, 0, 0, 0));
    page_.setBounds(r.removeFromRight(juce::jlimit(300, 380, getWidth() / 3)).reduced(0, 8));
    room_.setBounds(r);
}

} // namespace rkplug
