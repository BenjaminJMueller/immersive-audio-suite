#include "Scene.h"

namespace rkui {
namespace {

constexpr int kFormatVersion = 1;

float getF(const juce::var& o, const char* key, float def, float lo, float hi) {
    const juce::var v = o.getProperty(key, juce::var());
    if (!(v.isDouble() || v.isInt() || v.isInt64())) return def;
    return rk::safeClamp(static_cast<float>(static_cast<double>(v)), lo, hi, def);
}

bool getB(const juce::var& o, const char* key, bool def) {
    const juce::var v = o.getProperty(key, juce::var());
    return v.isBool() || v.isInt() ? static_cast<bool>(v) : def;
}

int getI(const juce::var& o, const char* key, int def, int lo, int hi) {
    const juce::var v = o.getProperty(key, juce::var());
    if (!(v.isDouble() || v.isInt() || v.isInt64())) return def;
    const double d = static_cast<double>(v);
    if (!std::isfinite(d)) return def;
    return juce::jlimit(lo, hi, static_cast<int>(juce::jlimit(-1.0e9, 1.0e9, d)));
}

juce::String getS(const juce::var& o, const char* key, const juce::String& def, int maxLen = 64) {
    const juce::var v = o.getProperty(key, juce::var());
    return v.isString() ? v.toString().substring(0, maxLen) : def;
}

juce::var vec(rk::Vec3 p) {
    juce::Array<juce::var> a;
    a.add(p.x);
    a.add(p.y);
    a.add(p.z);
    return a;
}

rk::Vec3 getVec(const juce::var& o, const char* key, rk::Vec3 def) {
    const juce::var v = o.getProperty(key, juce::var());
    if (!v.isArray() || v.size() != 3) return def;
    auto f = [&](int i, float d) {
        const juce::var e = v[i];
        return (e.isDouble() || e.isInt() || e.isInt64()) ? rk::safeClamp(static_cast<float>(static_cast<double>(e)), -1000.0f, 1000.0f, d) : d;
    };
    return {f(0, def.x), f(1, def.y), f(2, def.z)};
}

} // namespace

rk::PannerKind Scene::effectivePanner() const {
    return autoPanner ? rk::analyze(layout).recommended : panner;
}

rk::EngineConfig Scene::engineConfig() const {
    rk::EngineConfig c;
    c.layout = layout;
    c.panner = effectivePanner();
    c.dbap.rolloffDb = dbapRolloffDb;
    c.dbap.blur = dbapBlur;
    c.dbap.directivity = dbapDirectivity;
    c.ambiOrder = 3;
    c.outputMode = headphones ? rk::OutputMode::Headphones : rk::OutputMode::Speakers;
    return c;
}

juce::var layoutToVar(const rk::SpeakerLayout& l) {
    auto* o = new juce::DynamicObject();
    o->setProperty("name", juce::String(l.name));
    juce::Array<juce::var> groups;
    for (const auto& g : l.groups) groups.add(juce::String(g.name));
    o->setProperty("groups", groups);
    juce::Array<juce::var> spk;
    for (const auto& s : l.speakers) {
        auto* so = new juce::DynamicObject();
        so->setProperty("name", juce::String(s.name));
        so->setProperty("position", vec(s.position));
        so->setProperty("aim", vec(s.aim));
        so->setProperty("gainDb", s.gainDb);
        so->setProperty("delayMs", s.delayMs);
        so->setProperty("group", s.group);
        so->setProperty("subwoofer", s.isSubwoofer);
        so->setProperty("enabled", s.enabled);
        spk.add(juce::var(so));
    }
    o->setProperty("speakers", spk);
    return juce::var(o);
}

rk::SpeakerLayout layoutFromVar(const juce::var& v) {
    rk::SpeakerLayout l;
    l.name = getS(v, "name", "Layout").toStdString();
    if (const auto* g = v.getProperty("groups", juce::var()).getArray())
        for (const auto& e : *g)
            if (e.isString() && l.groups.size() < 64) l.groups.push_back({e.toString().substring(0, 64).toStdString()});
    if (const auto* a = v.getProperty("speakers", juce::var()).getArray()) {
        for (const auto& e : *a) {
            if (static_cast<int>(l.speakers.size()) >= rk::kMaxSpeakers) break;
            if (!e.isObject()) continue;
            rk::Speaker s;
            s.name = getS(e, "name", juce::String(static_cast<int>(l.speakers.size()) + 1)).toStdString();
            s.position = getVec(e, "position", {0, 2, 0});
            s.aim = getVec(e, "aim", {0, 0, 0});
            s.gainDb = getF(e, "gainDb", 0.0f, -60.0f, 12.0f);
            s.delayMs = getF(e, "delayMs", 0.0f, 0.0f, 500.0f);
            s.group = getI(e, "group", 0, 0, 63);
            s.isSubwoofer = getB(e, "subwoofer", false);
            s.enabled = getB(e, "enabled", true);
            l.speakers.push_back(s);
        }
    }
    rk::sanitize(l);
    return l;
}

juce::var sceneToVar(const Scene& s) {
    auto* o = new juce::DynamicObject();
    o->setProperty("format", "raumklang");
    o->setProperty("version", kFormatVersion);
    o->setProperty("name", s.name);
    o->setProperty("layout", layoutToVar(s.layout));
    o->setProperty("preset", s.presetId);
    auto* pp = new juce::DynamicObject();
    pp->setProperty("count", s.ringCount);
    pp->setProperty("radius", s.radius);
    pp->setProperty("height", s.height);
    pp->setProperty("innerCount", s.innerCount);
    pp->setProperty("innerRadius", s.innerRadius);
    pp->setProperty("outerCount", s.outerCount);
    pp->setProperty("outerRadius", s.outerRadius);
    o->setProperty("presetParams", juce::var(pp));
    o->setProperty("autoPanner", s.autoPanner);
    o->setProperty("panner", juce::String(rk::pannerId(s.panner)));
    o->setProperty("dbapRolloffDb", s.dbapRolloffDb);
    o->setProperty("dbapBlur", s.dbapBlur);
    o->setProperty("dbapDirectivity", s.dbapDirectivity);
    auto* r = new juce::DynamicObject();
    r->setProperty("decaySec", s.reverb.decaySec);
    r->setProperty("dampingHz", s.reverb.dampingHz);
    r->setProperty("preDelayMs", s.reverb.preDelayMs);
    r->setProperty("size", s.reverb.size);
    r->setProperty("wetDb", s.reverbWetDb);
    o->setProperty("reverb", juce::var(r));
    o->setProperty("masterGainDb", s.masterGainDb);
    o->setProperty("limiter", s.limiter);
    o->setProperty("headphones", s.headphones);
    juce::Array<juce::var> src;
    for (const auto& x : s.sources) {
        auto* so = new juce::DynamicObject();
        so->setProperty("name", x.name);
        so->setProperty("input", x.inputChannel);
        so->setProperty("test", static_cast<int>(x.test));
        so->setProperty("position", vec(x.position));
        so->setProperty("gainDb", x.gainDb);
        so->setProperty("spread", x.spread);
        so->setProperty("reverbSendDb", x.reverbSendDb);
        so->setProperty("distanceAmount", x.distanceAmount);
        so->setProperty("mute", x.mute);
        so->setProperty("solo", x.solo);
        so->setProperty("orbit", x.orbit);
        so->setProperty("orbitSpeed", x.orbitSpeed);
        src.add(juce::var(so));
    }
    o->setProperty("sources", src);
    return juce::var(o);
}

Scene sceneFromVar(const juce::var& v) {
    Scene s;
    s.name = getS(v, "name", {}, 128);
    const juce::var layout = v.getProperty("layout", juce::var());
    if (layout.isObject()) s.layout = layoutFromVar(layout);
    s.presetId = getS(v, "preset", "custom");
    const juce::var pp = v.getProperty("presetParams", juce::var());
    s.ringCount = getI(pp, "count", 8, 1, rk::kMaxSpeakers);
    s.radius = getF(pp, "radius", 3.0f, 0.3f, 100.0f);
    s.height = getF(pp, "height", 0.0f, -20.0f, 50.0f);
    s.innerCount = getI(pp, "innerCount", 4, 1, rk::kMaxSpeakers / 2);
    s.innerRadius = getF(pp, "innerRadius", 1.0f, 0.2f, 100.0f);
    s.outerCount = getI(pp, "outerCount", 8, 1, rk::kMaxSpeakers / 2);
    s.outerRadius = getF(pp, "outerRadius", 5.0f, 0.5f, 200.0f);
    if (!presetIds().contains(s.presetId)) s.presetId = "custom";
    s.autoPanner = getB(v, "autoPanner", true);
    const juce::String p = getS(v, "panner", "vbap");
    s.panner = p == "dbap" ? rk::PannerKind::DBAP : (p == "ambisonics" ? rk::PannerKind::Ambisonics : rk::PannerKind::VBAP);
    s.dbapRolloffDb = getF(v, "dbapRolloffDb", 6.0f, 1.0f, 18.0f);
    s.dbapBlur = getF(v, "dbapBlur", 0.3f, 0.01f, 50.0f);
    s.dbapDirectivity = getF(v, "dbapDirectivity", 0.7f, 0.0f, 1.0f);
    const juce::var r = v.getProperty("reverb", juce::var());
    s.reverb.decaySec = getF(r, "decaySec", 1.8f, 0.2f, 12.0f);
    s.reverb.dampingHz = getF(r, "dampingHz", 6000.0f, 1000.0f, 20000.0f);
    s.reverb.preDelayMs = getF(r, "preDelayMs", 15.0f, 0.0f, 250.0f);
    s.reverb.size = getF(r, "size", 1.0f, 0.3f, 2.0f);
    s.reverbWetDb = getF(r, "wetDb", -6.0f, -80.0f, 6.0f);
    s.masterGainDb = getF(v, "masterGainDb", 0.0f, -80.0f, 12.0f);
    s.limiter = getB(v, "limiter", true);
    s.headphones = getB(v, "headphones", false);
    if (const auto* a = v.getProperty("sources", juce::var()).getArray()) {
        for (const auto& e : *a) {
            if (static_cast<int>(s.sources.size()) >= kMaxSceneSources) break;
            if (!e.isObject()) continue;
            SourceInfo x;
            x.name = getS(e, "name", juce::String("Quelle") + " " + juce::String(static_cast<int>(s.sources.size()) + 1));
            x.inputChannel = getI(e, "input", 0, 0, 63);
            x.test = static_cast<TestSignal>(getI(e, "test", 0, 0, 3));
            x.position = getVec(e, "position", {0, 2, 0});
            x.gainDb = getF(e, "gainDb", 0.0f, -80.0f, 12.0f);
            x.spread = getF(e, "spread", 0.0f, 0.0f, 1.0f);
            x.reverbSendDb = getF(e, "reverbSendDb", -12.0f, -80.0f, 6.0f);
            x.distanceAmount = getF(e, "distanceAmount", 1.0f, 0.0f, 2.0f);
            x.mute = getB(e, "mute", false);
            x.solo = getB(e, "solo", false);
            x.orbit = getB(e, "orbit", false);
            x.orbitSpeed = getF(e, "orbitSpeed", 0.1f, -2.0f, 2.0f);
            s.sources.push_back(x);
        }
    }
    return s;
}

const juce::StringArray& presetIds() {
    static const juce::StringArray ids{"stereo", "quad", "5.1", "7.1.4", "ring", "dome", "cube", "donut", "custom"};
    return ids;
}

juce::String presetName(const juce::String& id) {
    if (id == "stereo") return "Stereo";
    if (id == "quad") return "Quadrofonie";
    if (id == "5.1") return "5.1";
    if (id == "7.1.4") return "7.1.4";
    if (id == "ring") return "Ring";
    if (id == "dome") return "Kuppel";
    if (id == "cube") return juce::String::fromUTF8("W\xc3\xbcrfel");
    if (id == "donut") return "Donut (Lautsprecher zueinander)";
    return "Eigenes Layout";
}

bool regenerateLayout(Scene& s) {
    const juce::String& id = s.presetId;
    rk::SpeakerLayout l;
    if (id == "stereo") l = rk::presets::stereo(s.radius);
    else if (id == "quad") l = rk::presets::quad(s.radius);
    else if (id == "5.1") l = rk::presets::surround51(s.radius);
    else if (id == "7.1.4") l = rk::presets::surround714(s.radius);
    else if (id == "ring") l = rk::presets::ring(s.ringCount, s.radius, s.height, s.ringCount % 2 == 0);
    else if (id == "dome") l = rk::presets::dome({8, 8, 4}, s.radius, true);
    else if (id == "cube") l = rk::presets::cube(s.radius * 0.7071f);
    else if (id == "donut") l = rk::presets::donut(s.innerCount, s.innerRadius, s.outerCount, s.outerRadius, s.height);
    else return false;
    s.layout = l;
    return true;
}

void alignDistances(rk::SpeakerLayout& l) {
    float maxD = 0.0f;
    for (const auto& sp : l.speakers)
        if (sp.enabled && !sp.isSubwoofer) maxD = std::max(maxD, rk::length(sp.position));
    if (maxD <= 0.0f) return;
    for (auto& sp : l.speakers) {
        if (sp.isSubwoofer) continue;
        const float d = std::max(0.1f, rk::length(sp.position));
        sp.delayMs = rk::safeClamp((maxD - d) / rk::kSpeedOfSound * 1000.0f, 0.0f, 500.0f, 0.0f);
        sp.gainDb = rk::safeClamp(20.0f * std::log10(d / maxD), -60.0f, 12.0f, 0.0f);
    }
}

juce::String sceneToJson(const Scene& s) { return juce::JSON::toString(sceneToVar(s)); }

bool sceneFromJson(const juce::String& json, Scene& out) {
    juce::var v;
    if (json.length() > 16 * 1024 * 1024) return false;
    if (juce::JSON::parse(json, v).failed() || !v.isObject()) return false;
    if (v.getProperty("format", juce::var()).toString() != "raumklang") return false;
    out = sceneFromVar(v);
    return true;
}

juce::File appDataFolder() {
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    dir = dir.getChildFile("Application Support");
#endif
    dir = dir.getChildFile("Raumklang");
    dir.createDirectory();
    return dir;
}

juce::File sharedSetupFile() { return appDataFolder().getChildFile("Lautsprecher-Setup.json"); }

bool writeFileAtomically(const juce::File& f, const juce::String& text) {
    juce::TemporaryFile tmp(f);
    if (!tmp.getFile().replaceWithText(text, false, false, "\n")) return false;
    return tmp.overwriteTargetFileWithTemporary();
}

bool writeSharedSetup(const Scene& s) {
    Scene setup = s;
    setup.sources.clear();
    return writeFileAtomically(sharedSetupFile(), sceneToJson(setup));
}

bool readSharedSetup(Scene& s) {
    const juce::File f = sharedSetupFile();
    if (!f.existsAsFile() || f.getSize() > 4 * 1024 * 1024) return false;
    Scene loaded;
    if (!sceneFromJson(f.loadFileAsString(), loaded)) return false;
    s.layout = loaded.layout;
    s.presetId = loaded.presetId;
    s.ringCount = loaded.ringCount;
    s.radius = loaded.radius;
    s.height = loaded.height;
    s.innerCount = loaded.innerCount;
    s.innerRadius = loaded.innerRadius;
    s.outerCount = loaded.outerCount;
    s.outerRadius = loaded.outerRadius;
    s.autoPanner = loaded.autoPanner;
    s.panner = loaded.panner;
    s.dbapRolloffDb = loaded.dbapRolloffDb;
    s.dbapBlur = loaded.dbapBlur;
    s.dbapDirectivity = loaded.dbapDirectivity;
    s.reverb = loaded.reverb;
    s.reverbWetDb = loaded.reverbWetDb;
    return true;
}

} // namespace rkui
