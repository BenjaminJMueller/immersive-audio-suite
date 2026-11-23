#include "Snapshots.h"

#include "Strings.h"

#include <cmath>

namespace rkui {
namespace {
float lerp(float a, float b, float t) { return a + (b - a) * t; }
} // namespace

Snapshot captureSnapshot(const Scene& s, const std::vector<DawTrackState>& daw, const juce::String& name, float fadeSec) {
    Snapshot sn;
    sn.name = name;
    sn.fadeSec = juce::jlimit(0.0f, 60.0f, fadeSec);
    sn.sources = s.sources;
    sn.reverb = s.reverb;
    sn.reverbWetDb = s.reverbWetDb;
    sn.masterGainDb = s.masterGainDb;
    sn.daw = daw;
    return sn;
}

rk::Vec3 blendPosition(rk::Vec3 from, rk::Vec3 to, float t) {
    t = juce::jlimit(0.0f, 1.0f, t);
    const rk::Polar a = rk::toPolar(from), b = rk::toPolar(to);
    float d = b.azimuthDeg - a.azimuthDeg;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    rk::Polar p;
    p.azimuthDeg = a.azimuthDeg + d * t;
    if (p.azimuthDeg > 180.0f) p.azimuthDeg -= 360.0f;
    if (p.azimuthDeg < -180.0f) p.azimuthDeg += 360.0f;
    p.elevationDeg = lerp(a.elevationDeg, b.elevationDeg, t);
    p.distance = lerp(a.distance, b.distance, t);
    return rk::toCartesian(p);
}

DawTrackState blendDaw(const DawTrackState& from, const DawTrackState& to, float t) {
    const rk::Vec3 p = blendPosition(rk::toCartesian({from.azimuthDeg, from.elevationDeg, from.distance}),
                                     rk::toCartesian({to.azimuthDeg, to.elevationDeg, to.distance}), t);
    const rk::Polar q = rk::toPolar(p);
    DawTrackState r = to;
    r.azimuthDeg = q.azimuthDeg;
    r.elevationDeg = q.elevationDeg;
    r.distance = q.distance;
    return r;
}

void blendSnapshot(Scene& out, const Scene& from, const Snapshot& to, float t) {
    t = juce::jlimit(0.0f, 1.0f, t);
    out.sources = from.sources;
    const size_t n = std::min(from.sources.size(), to.sources.size());
    for (size_t i = 0; i < n; ++i) {
        const SourceInfo& a = from.sources[i];
        const SourceInfo& b = to.sources[i];
        SourceInfo& o = out.sources[i];
        o.position = blendPosition(a.position, b.position, t);
        o.gainDb = lerp(a.gainDb, b.gainDb, t);
        o.spread = lerp(a.spread, b.spread, t);
        o.reverbSendDb = lerp(a.reverbSendDb, b.reverbSendDb, t);
        o.distanceAmount = lerp(a.distanceAmount, b.distanceAmount, t);
        o.directivity = lerp(a.directivity, b.directivity, t);
        // Turn the short way round.
        float dy = std::fmod(b.yawDeg - a.yawDeg + 540.0f, 360.0f) - 180.0f;
        if (!std::isfinite(dy)) dy = 0.0f;
        o.yawDeg = std::fmod(a.yawDeg + dy * t + 540.0f, 360.0f) - 180.0f;
        if (t > 0.0f) {
            o.doppler = b.doppler;
            o.mute = b.mute;
            o.solo = b.solo;
            o.orbit = b.orbit;
            o.orbitSpeed = b.orbitSpeed;
            o.inputChannel = b.inputChannel;
            o.test = b.test;
        }
    }
    const rk::ReverbParams& ra = from.reverb;
    const rk::ReverbParams& rb = to.reverb;
    out.reverb.decaySec = lerp(ra.decaySec, rb.decaySec, t);
    out.reverb.dampingHz = std::exp(lerp(std::log(std::max(1.0f, ra.dampingHz)), std::log(std::max(1.0f, rb.dampingHz)), t));
    out.reverb.preDelayMs = lerp(ra.preDelayMs, rb.preDelayMs, t);
    out.reverb.size = lerp(ra.size, rb.size, t);
    out.reverb.lowRatio = lerp(ra.lowRatio, rb.lowRatio, t);
    out.reverb.earlyDb = lerp(ra.earlyDb, rb.earlyDb, t);
    out.reverb.clusterDb = lerp(ra.clusterDb, rb.clusterDb, t);
    out.reverb.tailDb = lerp(ra.tailDb, rb.tailDb, t);
    out.reverbWetDb = lerp(from.reverbWetDb, to.reverbWetDb, t);
    out.masterGainDb = lerp(from.masterGainDb, to.masterGainDb, t);
}

float fadeCurve(float t) {
    t = juce::jlimit(0.0f, 1.0f, t);
    return t * t * (3.0f - 2.0f * t);
}

// ---------------------------------------------------------------------------------------

SnapshotBar::SnapshotBar() {
    setTooltip(tr("Szenen speichern den ganzen Mix: Positionen, Pegel, Raum. Klick ruft eine Szene ab und blendet hinüber, die Pfeile gehen zur vorigen und nächsten. Rechtsklick zum Umbenennen, Überblendzeit oder Löschen."));
}

void SnapshotBar::setSnapshots(const std::vector<Snapshot>& list) {
    chips_.clear();
    for (const auto& sn : list) chips_.push_back({sn.name, num(sn.fadeSec, sn.fadeSec < 10.0f ? 1 : 0) + " s"});
    if (active_ >= (int)chips_.size()) active_ = -1;
    repaint();
}

void SnapshotBar::setActive(int index, float progress) {
    if (index == active_ && std::abs(progress - progress_) < 1e-3f) return;
    active_ = index;
    progress_ = progress;
    repaint();
}

SnapshotBar::Layout SnapshotBar::layout() const {
    Layout l;
    const juce::Font f = uiFont(12.5f);
    const float h = 28.0f, y = (getHeight() - h) * 0.5f;
    // Right: "Aktualisieren" and "+ Neue Szene", like the snapshot bar of a console.
    float right = getWidth() - 12.0f;
    const float addW = juce::GlyphArrangement::getStringWidth(f, "+ " + tr("Neue Szene")) + 24.0f;
    l.add = {right - addW, y, addW, h};
    right -= addW + 6.0f;
    const float updW = juce::GlyphArrangement::getStringWidth(f, tr("Aktualisieren")) + 24.0f;
    l.update = {right - updW, y, updW, h};
    right -= updW + 14.0f;
    // Left: label, previous, the snapshots, next.
    float x = 78.0f;
    l.prev = {x, y, 28.0f, h};
    x += 34.0f;
    const float nextX = right - 28.0f;
    for (const auto& c : chips_) {
        const float w = juce::jlimit(64.0f, 180.0f, juce::GlyphArrangement::getStringWidth(f, c.name) + 28.0f);
        l.chips.emplace_back(x, y, w, h);
        x += w + 6.0f;
    }
    l.next = {std::min(x, nextX), y, 28.0f, h};
    return l;
}

int SnapshotBar::hit(juce::Point<float> p) const {
    const auto l = layout();
    for (int i = 0; i < (int)l.chips.size(); ++i)
        if (l.chips[(size_t)i].contains(p) && l.chips[(size_t)i].getRight() <= l.next.getX()) return i;
    if (l.prev.contains(p)) return kPrev;
    if (l.next.contains(p)) return kNext;
    if (l.update.contains(p)) return kUpdate;
    if (l.add.contains(p)) return kNew;
    return -1;
}

void SnapshotBar::paint(juce::Graphics& g) {
    const Palette& p = palette();
    g.fillAll(p.side);
    g.setColour(p.line);
    g.drawHorizontalLine(0, 0.0f, (float)getWidth());
    g.setColour(p.text2);
    g.setFont(uiFont(12.0f, true));
    g.drawText(tr("Szenen"), juce::Rectangle<float>(16.0f, 0.0f, 60.0f, (float)getHeight()), juce::Justification::centredLeft);
    const auto l = layout();
    auto button = [&](juce::Rectangle<float> r, int id, bool enabled) {
        g.setColour(!enabled ? p.field.withMultipliedAlpha(0.5f) : (hover_ == id ? p.hover : p.field));
        g.fillRoundedRectangle(r, 7.0f);
    };
    const int n = (int)chips_.size();
    for (int i = 0; i < n; ++i) {
        const auto c = l.chips[(size_t)i];
        if (c.getRight() > l.next.getX()) break; // more than fit: reachable with the arrows
        const bool act = i == active_;
        g.setColour(act ? p.accent.withAlpha(0.22f) : (i == hover_ ? p.hover : p.field));
        g.fillRoundedRectangle(c, 7.0f);
        if (act) {
            g.setColour(p.accent);
            g.drawRoundedRectangle(c.reduced(0.5f), 7.0f, 1.0f);
            if (progress_ < 1.0f) // fading towards this one
                g.fillRoundedRectangle(c.withTrimmedTop(c.getHeight() - 3.0f).withWidth(c.getWidth() * progress_), 1.5f);
        }
        g.setColour(act ? p.text : p.text2);
        g.setFont(uiFont(12.5f, act));
        g.drawText(chips_[(size_t)i].name, c.reduced(10.0f, 0.0f), juce::Justification::centred, true);
    }
    const bool canPrev = n > 0 && active_ != 0, canNext = n > 0 && active_ < n - 1;
    auto chevron = [&](juce::Rectangle<float> r, bool left, bool enabled) {
        juce::Path c;
        const auto m = r.getCentre();
        c.startNewSubPath(m.x + (left ? 2.5f : -2.5f), m.y - 5.0f);
        c.lineTo(m.x + (left ? -2.5f : 2.5f), m.y);
        c.lineTo(m.x + (left ? 2.5f : -2.5f), m.y + 5.0f);
        g.setColour(enabled ? p.text2 : p.text3.withMultipliedAlpha(0.6f));
        g.strokePath(c, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    button(l.prev, kPrev, canPrev);
    chevron(l.prev, true, canPrev);
    button(l.next, kNext, canNext);
    chevron(l.next, false, canNext);
    const bool canUpdate = active_ >= 0;
    button(l.update, kUpdate, canUpdate);
    g.setFont(uiFont(12.5f));
    g.setColour(canUpdate ? p.text2 : p.text3);
    g.drawText(tr("Aktualisieren"), l.update, juce::Justification::centred);
    g.setColour(hover_ == kNew ? p.accent.brighter(0.1f) : p.accent);
    g.fillRoundedRectangle(l.add, 7.0f);
    g.setColour(p.onAccent);
    g.drawText("+ " + tr("Neue Szene"), l.add, juce::Justification::centred);
}

void SnapshotBar::mouseMove(const juce::MouseEvent& e) {
    const int h = hit(e.position);
    if (h != hover_) {
        hover_ = h;
        setMouseCursor(h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void SnapshotBar::mouseExit(const juce::MouseEvent&) {
    hover_ = -1;
    repaint();
}

void SnapshotBar::mouseUp(const juce::MouseEvent& e) {
    if (!e.mouseWasClicked()) return;
    const int h = hit(e.position);
    const int n = (int)chips_.size();
    if (h < 0) return;
    if (h == kNew) {
        if (onStore) onStore();
    } else if (h == kUpdate) {
        if (active_ >= 0 && onUpdate) onUpdate();
    } else if (h == kPrev || h == kNext) {
        if (n == 0 || !onRecall) return;
        const int target = active_ < 0 ? (h == kPrev ? n - 1 : 0) : active_ + (h == kPrev ? -1 : 1);
        if (target >= 0 && target < n) onRecall(target);
    } else if (e.mods.isPopupMenu()) {
        if (onMenu) onMenu(h);
    } else if (onRecall) {
        onRecall(h);
    }
}

} // namespace rkui
