// Tests for the document model: round trips and robustness against broken files.
#include <juce_gui_extra/juce_gui_extra.h>
#include <rk_ui/Scene.h>
#include <rk_ui/Strings.h>

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

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    roundTrip();
    rejectsGarbage();
    clampsHostileValues();
    fuzzJson();
    presetsAndAlign();
    atomicWrite();
    translations();
    std::printf("%s (%d failures)\n", failures == 0 ? "ALL SHARED TESTS PASSED" : "SHARED TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
