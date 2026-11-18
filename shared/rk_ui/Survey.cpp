#include "Survey.h"

#include <rk/calibration.hpp>

namespace rkui {
namespace {

bool readVec(const juce::var& v, rk::Vec3& out) {
    if (!v.isArray() || v.size() != 3) return false;
    double c[3];
    for (int i = 0; i < 3; ++i) {
        const juce::var& e = v[i];
        if (!(e.isDouble() || e.isInt() || e.isInt64())) return false;
        c[i] = static_cast<double>(e);
        if (!std::isfinite(c[i]) || std::abs(c[i]) > 500.0) return false;
    }
    out = {static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])};
    return true;
}

juce::var vecVar(rk::Vec3 v) {
    juce::Array<juce::var> a;
    a.add(static_cast<double>(v.x));
    a.add(static_cast<double>(v.y));
    a.add(static_cast<double>(v.z));
    return a;
}

rk::Vec3 radial(rk::Vec3 p) { return rk::normalized({p.x, p.y, 0.0f}); }

} // namespace

bool surveyFromJson(const juce::String& json, Survey& out, juce::String& error) {
    if (json.length() > 8 * 1024 * 1024) { error = "Die Datei ist zu groß."; return false; }
    juce::var v;
    if (juce::JSON::parse(json, v).failed() || !v.isObject()) { error = "Das ist keine Messung von Raumklang Messen."; return false; }
    if (v.getProperty("format", {}).toString() != "raumklang-survey") { error = "Das ist keine Messung von Raumklang Messen."; return false; }
    const int version = static_cast<int>(v.getProperty("version", 0));
    if (version < 1) { error = "Unbekannte Version der Messdatei."; return false; }
    if (version > 1) { error = "Die Messdatei ist neuer als dieses Raumklang. Bitte aktualisieren."; return false; }
    const juce::String frame = v.getProperty("frame", "raumklang").toString();
    if (frame != "raumklang") { error = "Unbekanntes Koordinatensystem in der Messdatei."; return false; }
    Survey s;
    s.source = v.getProperty("source", {}).toString().substring(0, 128);
    s.created = v.getProperty("created", {}).toString().substring(0, 64);
    const double lh = static_cast<double>(v.getProperty("listenerHeight", 1.2));
    s.listenerHeight = std::isfinite(lh) ? static_cast<float>(juce::jlimit(0.0, 5.0, lh)) : 1.2f;
    const juce::var list = v.getProperty("speakers", {});
    if (!list.isArray()) { error = "Die Messdatei enthält keine Lautsprecher."; return false; }
    for (int i = 0; i < list.size(); ++i) {
        const juce::var& e = list[i];
        if (!e.isObject()) continue;
        SurveySpeaker sp;
        if (!readVec(e.getProperty("position", {}), sp.position)) continue; // skip broken entries
        if (!readVec(e.getProperty("aim", {}), sp.aim)) sp.aim = {};
        if (rk::length(sp.aim) < 1.0e-4f) sp.aim = {};
        else sp.aim = rk::normalized(sp.aim);
        sp.name = e.getProperty("name", juce::String(i + 1)).toString().substring(0, 64);
        const double c = static_cast<double>(e.getProperty("confidence", 1.0));
        sp.confidence = std::isfinite(c) ? static_cast<float>(juce::jlimit(0.0, 1.0, c)) : 1.0f;
        s.speakers.push_back(sp);
        if (static_cast<int>(s.speakers.size()) >= rk::kMaxSpeakers) break;
    }
    if (s.speakers.empty()) { error = "Die Messdatei enthält keine Lautsprecher."; return false; }
    out = std::move(s);
    return true;
}

juce::String surveyToJson(const Survey& s) {
    auto* o = new juce::DynamicObject();
    o->setProperty("format", "raumklang-survey");
    o->setProperty("version", 1);
    o->setProperty("source", s.source);
    o->setProperty("created", s.created);
    o->setProperty("frame", "raumklang");
    o->setProperty("listenerHeight", static_cast<double>(s.listenerHeight));
    juce::Array<juce::var> list;
    for (const auto& sp : s.speakers) {
        auto* e = new juce::DynamicObject();
        e->setProperty("name", sp.name);
        e->setProperty("position", vecVar(sp.position));
        e->setProperty("aim", vecVar(sp.aim));
        e->setProperty("confidence", static_cast<double>(sp.confidence));
        list.add(juce::var(e));
    }
    o->setProperty("speakers", list);
    return juce::JSON::toString(juce::var(o));
}

SurveyApplied applySurvey(const rk::SpeakerLayout& current, const Survey& survey, SurveyMatch match) {
    SurveyApplied r;
    r.layout = current;
    const int nm = static_cast<int>(survey.speakers.size());
    auto place = [](rk::Speaker& sp, const SurveySpeaker& m) {
        // A speaker that faces the listener is stored as "towards the listener" (follows it when
        // moved); any other measured direction is kept as it is.
        sp.position = m.position;
        if (rk::length(m.aim) > 0.5f) {
            const rk::Vec3 toListener = rk::normalized(-m.position);
            sp.aim = rk::dot(m.aim, toListener) > 0.985f ? rk::Vec3{} : m.aim;
        }
    };
    if (match == SurveyMatch::Replace || current.speakers.empty()) {
        r.layout.speakers.clear();
        for (int i = 0; i < nm; ++i) {
            rk::Speaker sp;
            const auto& m = survey.speakers[static_cast<size_t>(i)];
            if (i < static_cast<int>(current.speakers.size())) {
                const auto& old = current.speakers[static_cast<size_t>(i)];
                sp.gainDb = old.gainDb;
                sp.delayMs = old.delayMs;
                sp.group = old.group;
                sp.isSubwoofer = old.isSubwoofer;
                sp.enabled = old.enabled;
            }
            sp.name = m.name.isNotEmpty() ? m.name.toStdString() : std::to_string(i + 1);
            place(sp, m);
            r.layout.speakers.push_back(sp);
        }
        r.updated.assign(r.layout.speakers.size(), 1);
        rk::sanitize(r.layout);
        return r;
    }
    const int ns = static_cast<int>(current.speakers.size());
    std::vector<int> assign(static_cast<size_t>(ns), -1);
    if (match == SurveyMatch::InOrder) {
        for (int i = 0; i < ns && i < nm; ++i) assign[static_cast<size_t>(i)] = i;
    } else {
        std::vector<rk::Vec3> planned, measured;
        for (const auto& sp : current.speakers) planned.push_back(sp.position);
        for (const auto& m : survey.speakers) measured.push_back(m.position);
        assign = rk::calib::matchByDistance(planned, measured);
    }
    r.updated.assign(static_cast<size_t>(ns), 0);
    std::vector<char> used(static_cast<size_t>(nm), 0);
    for (int i = 0; i < ns; ++i) {
        const int k = assign[static_cast<size_t>(i)];
        if (k < 0 || k >= nm) continue;
        auto& sp = r.layout.speakers[static_cast<size_t>(i)];
        const rk::Vec3 oldPos = sp.position;
        const auto& m = survey.speakers[static_cast<size_t>(k)];
        if (rk::length(m.aim) <= 0.5f && rk::length(sp.aim) > 1.0e-6f) {
            const float d = rk::dot(rk::normalized(sp.aim), radial(oldPos));
            sp.position = m.position;
            if (std::abs(d) > 0.9f) sp.aim = radial(m.position) * (d > 0 ? 1.0f : -1.0f);
        } else {
            place(sp, m);
        }
        r.updated[static_cast<size_t>(i)] = 1;
        used[static_cast<size_t>(k)] = 1;
    }
    for (char u : used) r.unmatchedMeasured += u ? 0 : 1;
    rk::sanitize(r.layout);
    return r;
}

rk::SpeakerLayout applyProposal(const rk::SpeakerLayout& current, const Proposal& p, ProposalOptions o) {
    if (p.replaces) {
        rk::SpeakerLayout l = p.replacement;
        rk::sanitize(l);
        return l;
    }
    rk::SpeakerLayout l = current;
    const size_t n = std::min(l.speakers.size(), p.speakers.size());
    for (size_t i = 0; i < n; ++i) {
        auto& sp = l.speakers[i];
        const auto& q = p.speakers[i];
        if (o.positions && q.hasPosition && rk::isFinite(q.position)) {
            if (q.hasAim) {
                sp.aim = q.aim;
            } else if (rk::length(sp.aim) > 1.0e-6f) {
                const float d = rk::dot(rk::normalized(sp.aim), radial(sp.position));
                if (std::abs(d) > 0.9f) sp.aim = radial(q.position) * (d > 0 ? 1.0f : -1.0f);
            }
            sp.position = q.position;
        }
        if (o.delays && q.hasDelay) sp.delayMs = q.delayMs;
        if (o.levels && q.hasGain) sp.gainDb = q.gainDb;
    }
    rk::sanitize(l);
    return l;
}

Proposal proposalFromSurvey(const rk::SpeakerLayout& current, const Survey& s, SurveyMatch match) {
    Proposal p;
    p.canPositions = true;
    const SurveyApplied a = applySurvey(current, s, match);
    if (match == SurveyMatch::Replace || current.speakers.empty()) {
        p.replaces = true;
        p.replacement = a.layout;
        for (size_t i = 0; i < a.layout.speakers.size(); ++i) {
            ProposedSpeaker q;
            q.measured = q.hasPosition = q.hasAim = true;
            q.position = a.layout.speakers[i].position;
            q.aim = a.layout.speakers[i].aim;
            q.note = "Neu";
            p.speakers.push_back(q);
        }
        return p;
    }
    for (size_t i = 0; i < current.speakers.size(); ++i) {
        ProposedSpeaker q;
        if (i < a.updated.size() && a.updated[i]) {
            q.measured = q.hasPosition = q.hasAim = true;
            q.position = a.layout.speakers[i].position;
            q.aim = a.layout.speakers[i].aim;
        } else {
            q.note = "Nicht gemessen";
            q.warn = true;
        }
        p.speakers.push_back(q);
    }
    return p;
}

} // namespace rkui
