// Tests for the document model: round trips and robustness against broken files.
#include <juce_gui_extra/juce_gui_extra.h>
#include <rk_ui/Scene.h>
#include <rk_ui/Pages.h>
#include <rk_ui/StageView.h>
#include <rk_ui/Strings.h>
#include <rk_ui/Survey.h>
#include <rk_ui/Visuals.h>

#include <random>

namespace {

int failures = 0;
#define EXPECT(cond)                                                                 \
    do {                                                                             \
        if (!(cond)) {                                                               \
            ++failures;                                                              \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                            \
    } while (0)

void roundTrip() {
    rkui::Scene s;
    s.name = juce::String::fromUTF8("Gew\xc3\xb6lbe Probe");
    s.presetId = "donut";
    s.innerCount = 6;
    s.outerRadius = 7.5f;
    rkui::regenerateLayout(s);
    s.layout.speakers[2].gainDb = -3.5f;
    s.layout.speakers[3].delayMs = 4.25f;
    s.layout.speakers[1].isSubwoofer = true;
    s.reverb.decaySec = 3.3f;
    s.headphones = true;
    for (int i = 0; i < 5; ++i) {
        rkui::SourceInfo src;
        src.name = "Q" + juce::String(i);
        src.position = {float(i), 2.0f, 0.5f};
        src.test = rkui::TestSignal::Clicks;
        src.orbit = i % 2 == 0;
        s.sources.push_back(src);
    }
    rkui::Scene back;
    EXPECT(rkui::sceneFromJson(rkui::sceneToJson(s), back));
    EXPECT(back.name == s.name);
    EXPECT(back.presetId == "donut");
    EXPECT(back.innerCount == 6);
    EXPECT(back.layout.speakers.size() == s.layout.speakers.size());
    EXPECT(std::abs(back.layout.speakers[2].gainDb + 3.5f) < 1e-5f);
    EXPECT(std::abs(back.layout.speakers[3].delayMs - 4.25f) < 1e-5f);
    EXPECT(back.layout.speakers[1].isSubwoofer);
    EXPECT(std::abs(back.reverb.decaySec - 3.3f) < 1e-5f);
    EXPECT(back.headphones);
    EXPECT(back.sources.size() == 5);
    EXPECT(back.sources[3].position.x == 3.0f);
    EXPECT(back.sources[4].test == rkui::TestSignal::Clicks);
    EXPECT(rkui::sceneToJson(back) == rkui::sceneToJson(s));
    EXPECT(back.effectivePanner() == rk::PannerKind::DBAP);
}

void rejectsGarbage() {
    rkui::Scene s;
    const char* bad[] = {"", "{", "[]", "null", "42", "{\"format\":\"other\"}", "\xff\xfe\x00", "{\"format\":\"raumklang\",\"layout\":7}"};
    int accepted = 0;
    for (const char* b : bad) accepted += rkui::sceneFromJson(juce::String::fromUTF8(b), s) ? 1 : 0;
    EXPECT(accepted == 1); // only the last one is a (useless but valid) raumklang file
}

void clampsHostileValues() {
    const juce::String json = R"({"format":"raumklang","version":1,
        "layout":{"speakers":[{"position":[1e30,"x",null],"gainDb":1e9,"delayMs":-5,"group":99999},
                              {"position":[1,2]}, 17, {"aim":[NaN]}]},
        "reverb":{"decaySec":-1,"size":1e20,"wetDb":"loud"},
        "presetParams":{"count":-3,"outerCount":1e12},
        "sources":[{"input":-7,"test":99,"position":[0,0,0],"spread":5,"orbitSpeed":1e30}]})";
    rkui::Scene s;
    EXPECT(rkui::sceneFromJson(json, s) || true); // NaN is not valid JSON: may fail, must not crash
    juce::var v = juce::JSON::parse(json.replace("NaN", "0"));
    s = rkui::sceneFromVar(v);
    EXPECT(s.layout.speakers.size() == 3);
    for (const auto& sp : s.layout.speakers) {
        EXPECT(rk::isFinite(sp.position));
        EXPECT(sp.gainDb <= 12.0f);
        EXPECT(sp.delayMs >= 0.0f);
        EXPECT(sp.group <= 63);
    }
    EXPECT(s.reverb.decaySec >= 0.2f);
    EXPECT(s.reverb.size <= 2.0f);
    EXPECT(s.ringCount >= 1);
    EXPECT(s.outerCount <= rk::kMaxSpeakers);
    EXPECT(s.sources.size() == 1);
    EXPECT(s.sources[0].inputChannel == 0);
    EXPECT(static_cast<int>(s.sources[0].test) <= 3);
    EXPECT(s.sources[0].spread <= 1.0f);
}

void fuzzJson() {
    std::mt19937 rng(11);
    const juce::String base = rkui::sceneToJson(rkui::Scene{});
    for (int i = 0; i < 3000; ++i) {
        juce::String t = base;
        const int edits = 1 + int(rng() % 8);
        for (int e = 0; e < edits; ++e) {
            const int pos = int(rng() % (unsigned)juce::jmax(1, t.length()));
            const char repl[] = {'{', '}', '[', ']', '"', ',', ':', '-', '9', 'e', 'x', ' ', '0'};
            t = t.substring(0, pos) + juce::String::charToString((juce::juce_wchar)repl[rng() % sizeof(repl)]) + t.substring(pos + int(rng() % 3));
        }
        rkui::Scene s;
        if (rkui::sceneFromJson(t, s)) {
            rk::EngineConfig c = s.engineConfig();
            EXPECT(c.layout.speakers.size() <= (size_t)rk::kMaxSpeakers);
            for (const auto& sp : c.layout.speakers) EXPECT(rk::isFinite(sp.position));
        }
    }
}

void presetsAndAlign() {
    for (const auto& id : rkui::presetIds()) {
        rkui::Scene s;
        s.presetId = id;
        const bool generated = rkui::regenerateLayout(s);
        EXPECT(generated == (id != "custom"));
        EXPECT(!s.layout.speakers.empty());
        EXPECT(rkui::presetName(id).isNotEmpty());
    }
    rk::SpeakerLayout l = rk::presets::stereo(2.0f);
    l.speakers[1].position = l.speakers[1].position * 0.5f; // R at 1 m, L at 2 m
    rkui::alignDistances(l);
    EXPECT(l.speakers[0].delayMs == 0.0f);
    EXPECT(std::abs(l.speakers[1].delayMs - 1000.0f / rk::kSpeedOfSound) < 0.01f);
    EXPECT(std::abs(l.speakers[1].gainDb + 6.02f) < 0.05f);
}

void atomicWrite() {
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("raumklang-test.json");
    EXPECT(rkui::writeFileAtomically(f, "hello"));
    EXPECT(f.loadFileAsString() == "hello");
    EXPECT(rkui::writeFileAtomically(f, "world"));
    EXPECT(f.loadFileAsString() == "world");
    f.deleteFile();
}

void translations() {
    rkui::setLanguage(rkui::Language::English);
    EXPECT(rkui::tr("Lautsprecher") == "Speakers");
    EXPECT(rkui::tr("Unbekannt") == "Unbekannt");
    EXPECT(rkui::num(3.5, 1) == "3.5");
    rkui::setLanguage(rkui::Language::German);
    EXPECT(rkui::tr("Höhe") == juce::String::fromUTF8("H\xc3\xb6he"));
    EXPECT(rkui::num(3.5, 1) == "3,5");
}

} // namespace

// Spectrum analyser: a 1 kHz sine lands in the right band near 0 dB; garbage is harmless.
// With RK_PREVIEW_DIR set, also renders the stage and the reverb view to PNG for a visual check.
// Free speaker angles: the donut's inner ring faces outwards (180 deg), round trips hold.
void speakerAngles() {
    const auto d = rk::presets::donut(4, 1.0f, 8, 5.0f);
    for (const auto& sp : d.speakers) {
        const auto a = rkui::aimAngles(sp);
        const bool inner = rk::length(sp.position) < 2.0f;
        EXPECT(std::abs(std::abs(a.first) - (inner ? 180.0f : 0.0f)) < 0.5f);
    }
    rk::Speaker sp;
    sp.position = rk::toCartesian({30.0f, 0.0f, 3.0f});
    for (float rot : {-170.0f, -45.0f, 0.0f, 20.0f, 90.0f, 179.0f})
        for (float tilt : {-60.0f, 0.0f, 35.0f}) {
            rkui::setAimAngles(sp, rot, tilt);
            const auto a = rkui::aimAngles(sp);
            EXPECT(std::abs(a.first - rot) < 0.01f);
            EXPECT(std::abs(a.second - tilt) < 0.01f);
        }
}

void visuals() {
    rkui::SpectrumAnalyzer an;
    std::vector<float> x(rkui::SpectrumAnalyzer::kSize);
    for (size_t i = 0; i < x.size(); ++i) x[i] = (float)std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * (double)i / 48000.0);
    an.process(x.data(), 48000.0);
    int best = 0;
    for (int b = 1; b < rkui::SpectrumAnalyzer::kBands; ++b)
        if (an.bandsDb()[(size_t)b] > an.bandsDb()[(size_t)best]) best = b;
    const float f = rkui::SpectrumAnalyzer::bandFrequency(best);
    EXPECT(f > 850.0f && f < 1180.0f);
    EXPECT(an.bandsDb()[(size_t)best] > -9.0f && an.bandsDb()[(size_t)best] < 3.0f);
    std::fill(x.begin(), x.end(), std::numeric_limits<float>::quiet_NaN());
    an.process(x.data(), 48000.0);
    for (float v : an.bandsDb()) EXPECT(std::isfinite(v));

    const juce::String dir = juce::SystemStats::getEnvironmentVariable("RK_PREVIEW_DIR", {});
    if (dir.isEmpty()) return;
    rkui::setDarkMode(true);
    rkui::Scene sc;
    sc.presetId = "dome";
    rkui::regenerateLayout(sc);
    for (int i = 0; i < 4; ++i) {
        rkui::SourceInfo si;
        si.name = "Q" + juce::String(i + 1);
        si.position = rk::toCartesian({-140.0f + 85.0f * (float)i, i == 2 ? 35.0f : 0.0f, 2.2f});
        si.spread = i == 1 ? 0.3f : 0.0f;
        sc.sources.push_back(si);
    }
    for (int proj = 0; proj < 3; ++proj) {
        rkui::StageView st;
        st.setBounds(0, 0, 850, 780);
        st.setScene(sc);
        st.setProjection(static_cast<rkui::StageView::Projection>(proj));
        st.setSelectedSource(-1);
        st.setAmbience(0.7f);
        // Speaker levels as the engine would produce them from the four sources.
        const float srcLvl[4] = {0.9f, 0.6f, 0.8f, 0.5f};
        rk::Panner pan;
        pan.prepare(sc.layout, sc.effectivePanner(), {sc.dbapRolloffDb, sc.dbapBlur, sc.dbapDirectivity}, 3);
        std::vector<float> spk(sc.layout.speakers.size(), 0.0f);
        float gains[rk::kMaxSpeakers];
        for (int i = 0; i < 4; ++i) {
            pan.compute({sc.sources[(size_t)i].position, sc.sources[(size_t)i].spread}, gains);
            for (size_t m = 0; m < spk.size(); ++m) spk[m] += gains[m] * gains[m] * srcLvl[i];
        }
        for (auto& v : spk) v = std::min(1.0f, std::sqrt(v) * 0.95f + 0.04f); // plus a little reverb everywhere
        st.setSpeakerLevels(spk);
        st.setSourceLevels({0.0f, 0.0f, 0.0f, 0.0f});
        st.setSourceLevels({0.9f, 0.6f, 0.8f, 0.5f});
        st.setCoach("2", "Quellen im Raum platzieren", "Ziehen verschiebt sie.");
        juce::Image img(juce::Image::RGB, 850 * 2, 780 * 2, true);
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::scale(2.0f));
        st.paintEntireComponent(g, true);
        juce::File out = juce::File(dir).getChildFile("stage-" + juce::String(proj) + ".png");
        out.deleteFile();
        juce::FileOutputStream os(out);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }
    rkui::ReverbView rv;
    rv.setBounds(0, 0, 326, 170);
    rv.setParams({2.2f, 6000.0f, 20.0f, 1.2f}, -6.0f);
    rkui::SpectrumAnalyzer an2;
    for (int frame = 0; frame < 40; ++frame) { // a little melody so the waterfall moves
        const double f1 = 110.0 * std::pow(2.0, (double)((frame / 6) % 4) * 3.0 / 12.0);
        for (size_t i = 0; i < x.size(); ++i) {
            const double t = (double)(i + (size_t)frame * 1600) / 48000.0;
            const double env = 0.6 + 0.4 * std::sin(frame * 0.5);
            x[i] = (float)(env * (0.3 * std::sin(2.0 * juce::MathConstants<double>::pi * f1 * t) + 0.12 * std::sin(2.0 * juce::MathConstants<double>::pi * f1 * 3.0 * t) +
                                  0.05 * std::sin(2.0 * juce::MathConstants<double>::pi * f1 * 7.0 * t)) +
                           0.02 * ((double)(((i + (size_t)frame * 977) * 2654435761u) % 1000) / 500.0 - 1.0));
        }
        an2.process(x.data(), 48000.0);
        rv.setBands(an2.bandsDb());
    }
    juce::Image img(juce::Image::ARGB, 326 * 2, 170 * 2, true);
    juce::Graphics g(img);
    g.fillAll(rkui::palette().side);
    g.addTransform(juce::AffineTransform::scale(2.0f));
    rv.paintEntireComponent(g, true);
    juce::File out = juce::File(dir).getChildFile("reverb.png");
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
}

void surveys() {
    // Round trip.
    rkui::Survey sv;
    sv.source = "iPhone 15 Pro (LiDAR)";
    sv.listenerHeight = 1.15f;
    for (int i = 0; i < 4; ++i) {
        rkui::SurveySpeaker sp;
        sp.name = juce::String(i + 1);
        sp.position = rk::toCartesian({90.0f * i, 10.0f, 3.0f});
        sp.aim = rk::normalized(-sp.position);
        sp.confidence = 0.8f;
        sv.speakers.push_back(sp);
    }
    rkui::Survey back;
    juce::String err;
    EXPECT(rkui::surveyFromJson(rkui::surveyToJson(sv), back, err));
    EXPECT(back.speakers.size() == 4);
    EXPECT(std::abs(back.listenerHeight - 1.15f) < 1e-5f);
    EXPECT(rk::length(back.speakers[2].position - sv.speakers[2].position) < 1e-4f);

    // Broken and hostile files are rejected or cleaned, never crash.
    for (const char* bad : {"", "null", "[]", "{\"format\":\"raumklang\"}", "{\"format\":\"raumklang-survey\",\"version\":1}",
                            "{\"format\":\"raumklang-survey\",\"version\":9,\"speakers\":[]}",
                            "{\"format\":\"raumklang-survey\",\"version\":1,\"frame\":\"arkit\",\"speakers\":[{\"position\":[1,2,3]}]}",
                            "{\"format\":\"raumklang-survey\",\"version\":1,\"speakers\":[{\"position\":[1,2]}, {\"position\":\"x\"}, 5]}"}) {
        rkui::Survey s2;
        juce::String e2;
        EXPECT(!rkui::surveyFromJson(bad, s2, e2));
        EXPECT(e2.isNotEmpty());
    }
    {
        rkui::Survey s2;
        juce::String e2;
        const char* mixed = "{\"format\":\"raumklang-survey\",\"version\":1,\"listenerHeight\":1e400,\"speakers\":["
                            "{\"position\":[1,2,3],\"aim\":[0,0,0],\"confidence\":7},"
                            "{\"position\":[1e9,2,3]},{\"position\":[0,3,1],\"aim\":[\"a\",1,2]}]}";
        EXPECT(rkui::surveyFromJson(mixed, s2, e2));
        EXPECT(s2.speakers.size() == 2);
        EXPECT(s2.speakers[0].confidence == 1.0f);
        EXPECT(rk::length(s2.speakers[0].aim) == 0.0f);
        EXPECT(std::isfinite(s2.listenerHeight));
    }
    // Fuzz: random mutations of a valid file.
    {
        const juce::String good = rkui::surveyToJson(sv);
        std::mt19937 rng(99);
        for (int k = 0; k < 3000; ++k) {
            juce::String t = good;
            const int edits = 1 + (int)(rng() % 6);
            for (int e = 0; e < edits; ++e) {
                const int pos = (int)(rng() % (unsigned)juce::jmax(1, t.length()));
                const juce::juce_wchar c = (juce::juce_wchar)(" -9.e[]{},:\"0Ea"[rng() % 15]);
                t = t.substring(0, pos) + juce::String::charToString(c) + t.substring(pos + 1);
            }
            rkui::Survey s3;
            juce::String e3;
            if (rkui::surveyFromJson(t, s3, e3)) {
                rk::SpeakerLayout l = rk::presets::ring(4, 2.0f);
                rk::SpeakerLayout copy = rkui::applySurvey(l, s3, (rkui::SurveyMatch)(k % 3)).layout;
                EXPECT(!rk::sanitize(copy));
            }
        }
    }

    // Applying: in order, by distance, as a new layout. Gains and delays of the layout stay.
    rk::SpeakerLayout ring = rk::presets::ring(4, 3.0f);
    ring.speakers[1].gainDb = -4.0f;
    ring.speakers[1].delayMs = 2.5f;
    rkui::Survey shifted = sv;
    std::rotate(shifted.speakers.begin(), shifted.speakers.begin() + 1, shifted.speakers.end());
    const auto inOrder = rkui::applySurvey(ring, shifted, rkui::SurveyMatch::InOrder);
    EXPECT(rk::length(inOrder.layout.speakers[0].position - sv.speakers[1].position) < 1e-4f);
    EXPECT(inOrder.layout.speakers[1].gainDb == -4.0f);
    EXPECT(inOrder.layout.speakers[1].delayMs == 2.5f);
    rk::SpeakerLayout planned;
    for (int i = 0; i < 4; ++i) {
        rk::Speaker sp;
        sp.position = sv.speakers[(size_t)i].position * 1.1f;
        planned.speakers.push_back(sp);
    }
    const auto nearest = rkui::applySurvey(planned, shifted, rkui::SurveyMatch::Nearest);
    for (int i = 0; i < 4; ++i) EXPECT(rk::length(nearest.layout.speakers[(size_t)i].position - sv.speakers[(size_t)i].position) < 1e-4f);
    EXPECT(nearest.unmatchedMeasured == 0);
    // Facing the listener becomes "towards the listener" (aim zero).
    EXPECT(rk::length(nearest.layout.speakers[0].aim) < 1e-6f);
    const auto repl = rkui::applySurvey(rk::presets::ring(8, 2.0f), sv, rkui::SurveyMatch::Replace);
    EXPECT(repl.layout.speakers.size() == 4);
    // A donut speaker facing outwards keeps its measured direction.
    rkui::Survey out = sv;
    out.speakers[0].aim = rk::normalized(out.speakers[0].position);
    const auto o2 = rkui::applySurvey(ring, out, rkui::SurveyMatch::InOrder);
    EXPECT(rk::dot(o2.layout.speakers[0].aimDirection(), rk::normalized(out.speakers[0].position)) > 0.99f);
    // Fewer measured than planned: the rest stays where it was.
    rkui::Survey two = sv;
    two.speakers.resize(2);
    const auto partial = rkui::applySurvey(ring, two, rkui::SurveyMatch::InOrder);
    EXPECT(partial.updated[2] == 0 && partial.updated[1] == 1);
    EXPECT(partial.layout.speakers[3].position == ring.speakers[3].position);

    // Proposals: options choose what is taken over; donut aims follow the new position.
    rk::SpeakerLayout donut = rk::presets::donut(4, 1.0f, 8, 5.0f);
    rkui::Proposal pr;
    pr.speakers.resize(donut.speakers.size());
    pr.speakers[0].hasPosition = true;
    pr.speakers[0].position = rk::toCartesian({90.0f, 20.0f, 1.3f});
    pr.speakers[0].hasDelay = true;
    pr.speakers[0].delayMs = 3.0f;
    pr.speakers[0].hasGain = true;
    pr.speakers[0].gainDb = -2.0f;
    pr.speakers[5].hasPosition = true;
    pr.speakers[5].position = {std::nanf(""), 0, 0};
    const auto all = rkui::applyProposal(donut, pr, {});
    EXPECT(rk::length(all.speakers[0].position - pr.speakers[0].position) < 1e-5f);
    EXPECT(rk::dot(all.speakers[0].aimDirection(), rk::normalized(rk::Vec3{all.speakers[0].position.x, all.speakers[0].position.y, 0})) > 0.99f);
    EXPECT(all.speakers[0].delayMs == 3.0f && all.speakers[0].gainDb == -2.0f);
    EXPECT(all.speakers[5].position == donut.speakers[5].position); // NaN ignored
    rkui::ProposalOptions only;
    only.positions = false;
    only.levels = false;
    const auto delaysOnly = rkui::applyProposal(donut, pr, only);
    EXPECT(delaysOnly.speakers[0].position == donut.speakers[0].position);
    EXPECT(delaysOnly.speakers[0].delayMs == 3.0f && delaysOnly.speakers[0].gainDb == 0.0f);
    const auto fromPhone = rkui::proposalFromSurvey(ring, two, rkui::SurveyMatch::InOrder);
    EXPECT(fromPhone.speakers.size() == 4 && fromPhone.speakers[0].hasPosition && !fromPhone.speakers[3].hasPosition);
    const auto replaced = rkui::applyProposal(ring, rkui::proposalFromSurvey(ring, two, rkui::SurveyMatch::Replace), {});
    EXPECT(replaced.speakers.size() == 2);
}

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    roundTrip();
    rejectsGarbage();
    clampsHostileValues();
    fuzzJson();
    presetsAndAlign();
    atomicWrite();
    translations();
    speakerAngles();
    visuals();
    surveys();
    std::printf("%s (%d failures)\n", failures == 0 ? "ALL SHARED TESTS PASSED" : "SHARED TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
