#include "rk/layout.hpp"

#include <algorithm>
#include <cmath>

namespace rk {

Vec3 Speaker::aimDirection() const {
    if (length(aim) > 1.0e-6f && isFinite(aim)) return normalized(aim);
    // Default: point at the listener origin.
    return normalized(-position, {0.0f, -1.0f, 0.0f});
}

const char* pannerId(PannerKind k) {
    switch (k) {
    case PannerKind::VBAP: return "vbap";
    case PannerKind::DBAP: return "dbap";
    case PannerKind::Ambisonics: return "ambisonics";
    default: return "dbap";
    }
}

bool sanitize(SpeakerLayout& layout) {
    bool changed = false;
    if (static_cast<int>(layout.speakers.size()) > kMaxSpeakers) {
        layout.speakers.resize(kMaxSpeakers);
        changed = true;
    }
    auto fix = [&changed](float& v, float lo, float hi, float fallback) {
        const float n = safeClamp(v, lo, hi, fallback);
        if (n != v || std::isnan(v)) { v = n; changed = true; }
    };
    for (auto& s : layout.speakers) {
        fix(s.position.x, -500.0f, 500.0f, 0.0f);
        fix(s.position.y, -500.0f, 500.0f, 0.0f);
        fix(s.position.z, -500.0f, 500.0f, 0.0f);
        fix(s.aim.x, -1.0e3f, 1.0e3f, 0.0f);
        fix(s.aim.y, -1.0e3f, 1.0e3f, 0.0f);
        fix(s.aim.z, -1.0e3f, 1.0e3f, 0.0f);
        fix(s.gainDb, -60.0f, 12.0f, 0.0f);
        fix(s.delayMs, 0.0f, 500.0f, 0.0f);
        if (s.group < 0 || s.group > 63) { s.group = std::clamp(s.group, 0, 63); changed = true; }
        if (s.name.size() > 64) { s.name.resize(64); changed = true; }
    }
    if (layout.groups.size() > 64) { layout.groups.resize(64); changed = true; }
    return changed;
}

LayoutInfo analyze(const SpeakerLayout& layout) {
    LayoutInfo info;
    std::vector<float> azimuths;
    float minZ = 1.0e9f, maxZ = -1.0e9f;
    float minR = 1.0e9f;
    int inward = 0, outward = 0;

    for (const auto& s : layout.speakers) {
        if (!s.enabled) continue;
        if (s.isSubwoofer) { ++info.numSubs; continue; }
        ++info.numActive;
        const float r = length(s.position);
        info.maxRadius = std::max(info.maxRadius, r);
        minR = std::min(minR, r);
        minZ = std::min(minZ, s.position.z);
        maxZ = std::max(maxZ, s.position.z);
        const Vec3 horiz{s.position.x, s.position.y, 0.0f};
        if (length(horiz) > 0.05f) {
            azimuths.push_back(toPolar(horiz).azimuthDeg);
            const Vec3 toCentre = normalized(-horiz);
            const Vec3 aim = s.aimDirection();
            const float d = dot(Vec3{aim.x, aim.y, 0.0f}, toCentre);
            if (d > 0.5f) ++inward;
            else if (d < -0.5f) ++outward;
        }
    }

    if (info.numActive == 0) {
        info.hints.emplace_back("no_speakers");
        return info;
    }
    if (info.numActive == 1) info.hints.emplace_back("single_speaker");

    // "Flat" if the main speakers are within ±1 m (or 15 % of radius) in height.
    info.isFlat = (maxZ - minZ) <= std::max(1.0f, 0.15f * info.maxRadius);

    // Surrounds the origin if no horizontal gap of 180° or more exists.
    if (azimuths.size() >= 3) {
        std::sort(azimuths.begin(), azimuths.end());
        float maxGap = 360.0f - (azimuths.back() - azimuths.front());
        for (size_t i = 1; i < azimuths.size(); ++i) maxGap = std::max(maxGap, azimuths[i] - azimuths[i - 1]);
        info.surroundsOrigin = maxGap < 179.0f;
    }

    info.facesInwardAndOutward = inward >= 2 && outward >= 2;

    if (info.facesInwardAndOutward) {
        info.recommended = PannerKind::DBAP;
        info.hints.emplace_back("donut_uses_dbap");
    } else if (info.numActive <= 2 || info.surroundsOrigin) {
        info.recommended = PannerKind::VBAP;
    } else {
        info.recommended = PannerKind::DBAP;
        info.hints.emplace_back("listener_not_enclosed");
    }

    if (info.numActive >= 2 && minR > 0.0f && info.maxRadius / minR > 1.5f && !info.facesInwardAndOutward)
        info.hints.emplace_back("unequal_distances");

    return info;
}

namespace presets {
namespace {

Speaker at(const char* name, float azDeg, float elDeg, float radius, int group = 0) {
    Speaker s;
    s.name = name;
    s.position = toCartesian({azDeg, elDeg, radius});
    s.group = group;
    return s;
}

} // namespace

SpeakerLayout stereo(float radius) {
    SpeakerLayout l;
    l.name = "Stereo";
    l.speakers = {at("L", 30, 0, radius), at("R", -30, 0, radius)};
    return l;
}

SpeakerLayout quad(float radius) {
    SpeakerLayout l;
    l.name = "Quad";
    l.speakers = {at("L", 45, 0, radius), at("R", -45, 0, radius), at("Ls", 135, 0, radius),
                  at("Rs", -135, 0, radius)};
    return l;
}

SpeakerLayout surround51(float radius) {
    SpeakerLayout l;
    l.name = "5.1";
    Speaker lfe = at("LFE", 0, -20, radius * 0.6f);
    lfe.isSubwoofer = true;
    l.speakers = {at("L", 30, 0, radius),   at("R", -30, 0, radius), at("C", 0, 0, radius), lfe,
                  at("Ls", 110, 0, radius), at("Rs", -110, 0, radius)};
    return l;
}

SpeakerLayout surround714(float radius) {
    SpeakerLayout l;
    l.name = "7.1.4";
    Speaker lfe = at("LFE", 0, -20, radius * 0.6f);
    lfe.isSubwoofer = true;
    l.speakers = {at("L", 30, 0, radius),       at("R", -30, 0, radius),      at("C", 0, 0, radius),
                  lfe,                           at("Lss", 90, 0, radius),      at("Rss", -90, 0, radius),
                  at("Lrs", 150, 0, radius),     at("Rrs", -150, 0, radius),    at("Ltf", 45, 45, radius, 1),
                  at("Rtf", -45, 45, radius, 1), at("Ltr", 135, 45, radius, 1), at("Rtr", -135, 45, radius, 1)};
    l.groups = {{"Ohrhöhe"}, {"Decke"}};
    return l;
}

SpeakerLayout ring(int count, float radius, float height, bool halfStepOffset) {
    count = std::clamp(count, 1, kMaxSpeakers);
    SpeakerLayout l;
    l.name = "Ring " + std::to_string(count);
    const float step = 360.0f / static_cast<float>(count);
    for (int i = 0; i < count; ++i) {
        const float az = (halfStepOffset ? step * 0.5f : 0.0f) + step * static_cast<float>(i);
        Speaker s;
        s.name = std::to_string(i + 1);
        s.position = toCartesian({wrapDegrees(az), 0.0f, radius});
        s.position.z = height;
        l.speakers.push_back(s);
    }
    return l;
}

SpeakerLayout dome(const std::vector<int>& ringCounts, float radius, bool zenith) {
    SpeakerLayout l;
    l.name = "Kuppel";
    const int rings = static_cast<int>(ringCounts.size());
    int n = 0;
    for (int r = 0; r < rings; ++r) {
        const float el = rings <= 1 ? 0.0f : 60.0f * static_cast<float>(r) / static_cast<float>(rings - 1);
        const int count = std::clamp(ringCounts[static_cast<size_t>(r)], 1, kMaxSpeakers);
        const float step = 360.0f / static_cast<float>(count);
        const float offset = (r % 2 == 1) ? step * 0.5f : 0.0f;
        l.groups.push_back({"Ring " + std::to_string(r + 1)});
        for (int i = 0; i < count && n < kMaxSpeakers; ++i, ++n)
            l.speakers.push_back(at(std::to_string(n + 1).c_str(), wrapDegrees(offset + step * static_cast<float>(i)), el, radius, r));
    }
    if (zenith && n < kMaxSpeakers) {
        l.groups.push_back({"Zenit"});
        l.speakers.push_back(at("Z", 0.0f, 90.0f, radius, rings));
    }
    return l;
}

SpeakerLayout cube(float h) {
    SpeakerLayout l;
    l.name = "Würfel";
    int n = 1;
    for (float z : {-h, h})
        for (float y : {h, -h})
            for (float x : {-h, h}) {
                Speaker s;
                s.name = std::to_string(n++);
                s.position = {x, y, z};
                s.group = z > 0 ? 1 : 0;
                l.speakers.push_back(s);
            }
    l.groups = {{"Unten"}, {"Oben"}};
    return l;
}

SpeakerLayout donut(int innerCount, float innerRadius, int outerCount, float outerRadius, float height) {
    innerCount = std::clamp(innerCount, 1, kMaxSpeakers / 2);
    outerCount = std::clamp(outerCount, 1, kMaxSpeakers / 2);
    innerRadius = safeClamp(innerRadius, 0.2f, 200.0f, 1.0f);
    outerRadius = safeClamp(outerRadius, innerRadius + 0.5f, 400.0f, innerRadius + 3.0f);
    SpeakerLayout l;
    l.name = "Donut";
    l.groups = {{"Innenring"}, {"Außenring"}};
    int n = 1;
    for (int i = 0; i < innerCount; ++i) {
        Speaker s;
        s.name = "I" + std::to_string(i + 1);
        s.position = toCartesian({wrapDegrees(360.0f * static_cast<float>(i) / static_cast<float>(innerCount)), 0.0f, innerRadius});
        s.position.z = height;
        s.aim = normalized(Vec3{s.position.x, s.position.y, 0.0f}); // outwards
        s.group = 0;
        l.speakers.push_back(s);
        ++n;
    }
    const float half = 180.0f / static_cast<float>(outerCount);
    for (int i = 0; i < outerCount; ++i) {
        Speaker s;
        s.name = "A" + std::to_string(i + 1);
        s.position = toCartesian({wrapDegrees(half + 360.0f * static_cast<float>(i) / static_cast<float>(outerCount)), 0.0f, outerRadius});
        s.position.z = height;
        s.aim = normalized(Vec3{-s.position.x, -s.position.y, 0.0f}); // inwards
        s.group = 1;
        l.speakers.push_back(s);
        ++n;
    }
    return l;
}

const std::vector<PresetInfo>& all() {
    static const std::vector<PresetInfo> list = {
        {"stereo", [] { return stereo(); }},
        {"quad", [] { return quad(); }},
        {"5.1", [] { return surround51(); }},
        {"7.1.4", [] { return surround714(); }},
        {"ring8", [] { auto l = ring(8, 3.0f, 0.0f, true); l.name = "Ring 8"; return l; }},
        {"ring16", [] { auto l = ring(16, 4.0f); l.name = "Ring 16"; return l; }},
        {"dome", [] { return dome({8, 8, 4}, 3.0f, true); }},
        {"cube", [] { return cube(); }},
        {"donut", [] { return donut(4, 1.0f, 8, 5.0f); }},
    };
    return list;
}

} // namespace presets
} // namespace rk
