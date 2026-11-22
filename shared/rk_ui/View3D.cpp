#include "View3D.h"
#include "Visuals.h"

#include "Strings.h"

#include <array>

namespace rkui {
namespace {

rk::Vec3 vsub(rk::Vec3 a, rk::Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float vdot(rk::Vec3 a, rk::Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
rk::Vec3 vcross(rk::Vec3 a, rk::Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
rk::Vec3 vnorm(rk::Vec3 a) {
    const float l = std::sqrt(vdot(a, a));
    return l > 1e-6f ? rk::Vec3{a.x / l, a.y / l, a.z / l} : rk::Vec3{0.0f, 1.0f, 0.0f};
}

constexpr float kStand = 1.2f; // ear height (z = 0) above the floor

} // namespace

View3D::View3D() { setOpaque(true); }

void View3D::setScene(const Scene& s) {
    const bool layoutChanged = !panningBuilt_ || !s.samePanning(scene_);
    panningBuilt_ = true;
    scene_ = s;
    if (layoutChanged) {
        rk::DbapPanner::Params d{s.dbapRolloffDb, s.dbapBlur, s.dbapDirectivity};
        panner_.prepare(s.layout, s.effectivePanner(), d, 3);
    }
    repaint();
}

void View3D::setSourceStyle(std::vector<juce::Colour> colours, int dimFrom, bool names) {
    sourceColours_ = std::move(colours);
    dimFrom_ = dimFrom;
    names_ = names;
    repaint();
}

juce::Colour View3D::sourceColour(int s) const {
    if (s >= 0 && s < (int)sourceColours_.size() && !sourceColours_[(size_t)s].isTransparent()) return sourceColours_[(size_t)s];
    return itemColour(s);
}

float View3D::extent() const {
    float r = 2.0f;
    for (const auto& sp : scene_.layout.speakers) r = std::max(r, std::hypot(sp.position.x, sp.position.y));
    return r;
}

void View3D::resetView() {
    yaw_ = -0.25f;
    pitch_ = 0.62f;
    zoom_ = 1.0f;
    repaint();
}

View3D::Projected View3D::project(rk::Vec3 w) const {
    const float dist = extent() * 2.6f / zoom_;
    const rk::Vec3 target{0.0f, 0.0f, -0.4f};
    const rk::Vec3 cam{target.x - std::sin(yaw_) * std::cos(pitch_) * dist, target.y - std::cos(yaw_) * std::cos(pitch_) * dist,
                       target.z + std::sin(pitch_) * dist};
    const rk::Vec3 f = vnorm(vsub(target, cam));
    const rk::Vec3 r = vnorm(vcross(f, {0.0f, 0.0f, 1.0f}));
    const rk::Vec3 u = vcross(r, f);
    const rk::Vec3 v = vsub(w, cam);
    const float depth = std::max(0.05f, vdot(v, f));
    const float focal = std::min((float)getWidth(), (float)getHeight() * 1.4f) * 0.78f;
    const float cx = getWidth() * 0.5f, cy = getHeight() * 0.45f;
    return {{cx + focal * vdot(v, r) / depth, cy - focal * vdot(v, u) / depth}, depth};
}

void View3D::paint(juce::Graphics& g) {
    const Palette& p = palette();
    g.fillAll(p.stage);
    const float R = extent();
    const float refDepth = R * 2.6f / zoom_;
    auto sizeAt = [refDepth](float base, float depth) { return juce::jlimit(base * 0.55f, base * 1.6f, base * refDepth / depth); };

    // Listening plane: rings every metre and the two axes.
    const int rings = (int)std::ceil(R + 0.6f);
    for (int k = 1; k <= rings; ++k) {
        juce::Path ring;
        for (int t = 0; t <= 96; ++t) {
            const float a = juce::MathConstants<float>::twoPi * (float)t / 96.0f;
            const auto q = project({(float)k * std::sin(a), (float)k * std::cos(a), -kStand}).p;
            if (t == 0) ring.startNewSubPath(q);
            else ring.lineTo(q);
        }
        g.setColour((k == rings ? p.lineStrong : p.line).withMultipliedAlpha(0.8f));
        g.strokePath(ring, juce::PathStrokeType(0.8f));
    }
    g.setColour(p.line);
    const float ax = (float)rings;
    g.drawLine(juce::Line<float>(project({-ax, 0, -kStand}).p, project({ax, 0, -kStand}).p), 0.8f);
    g.drawLine(juce::Line<float>(project({0, -ax, -kStand}).p, project({0, ax, -kStand}).p), 0.8f);
    g.setColour(p.text3);
    g.setFont(uiFont(11.0f));
    const auto front = project({0.0f, ax + 0.5f, -kStand}).p;
    g.drawText(tr("vorne"), juce::Rectangle<float>(front.x - 40, front.y - 16, 80, 14), juce::Justification::centred);

    // Everything else is sorted back to front.
    struct Item { float depth; std::function<void()> draw; };
    std::vector<Item> items;
    const int nSpk = (int)scene_.layout.speakers.size();

    for (int m = 0; m < nSpk; ++m) {
        const auto& spk = scene_.layout.speakers[(size_t)m];
        const auto top = project(spk.position);
        const auto foot = project({spk.position.x, spk.position.y, -kStand});
        const float lvl = m < (int)speakerLevels_.size() ? speakerLevels_[(size_t)m] : 0.0f;
        const juce::Colour col = spk.enabled ? itemColour(spk.group + 3) : p.text3;
        items.push_back({top.depth, [&g, &p, top, foot, lvl, col, m, sizeAt] {
                             const float s = sizeAt(18.0f, top.depth);
                             g.setColour(p.text3.withAlpha(0.5f));
                             g.drawLine(juce::Line<float>(foot.p, top.p), 1.0f);
                             g.setColour(juce::Colours::white.withAlpha(0.06f));
                             g.fillEllipse(foot.p.x - s * 0.45f, foot.p.y - s * 0.14f, s * 0.9f, s * 0.28f);
                             const auto box = juce::Rectangle<float>(s, s * 0.8f).withCentre(top.p);
                             g.setColour(col.withAlpha(0.30f));
                             g.fillRoundedRectangle(box, s * 0.2f);
                             if (lvl > 0.01f) {
                                 g.setColour(lvl > 0.97f ? p.bad : col);
                                 g.fillRoundedRectangle(box.withTrimmedTop(box.getHeight() * (1.0f - juce::jlimit(0.0f, 1.0f, lvl))), s * 0.2f);
                             }
                             g.setColour(col);
                             g.drawRoundedRectangle(box, s * 0.2f, 1.2f);
                             g.setColour(juce::Colours::white);
                             g.setFont(uiFont(juce::jlimit(8.0f, 12.0f, s * 0.55f), true));
                             g.drawText(juce::String(m + 1), box, juce::Justification::centred);
                         }});
    }

    // Listener: a ring at ear height above the centre of the floor.
    {
        const auto c = project({0.0f, 0.0f, 0.0f});
        const auto f = project({0.0f, 0.0f, -kStand});
        items.push_back({c.depth, [&g, &p, c, f] {
                             g.setColour(p.text3.withAlpha(0.5f));
                             g.drawLine(juce::Line<float>(f.p, c.p), 1.0f);
                             g.setColour(p.stage);
                             g.fillEllipse(c.p.x - 7, c.p.y - 7, 14, 14);
                             g.setColour(p.text2);
                             g.drawEllipse(c.p.x - 7, c.p.y - 7, 14, 14, 1.4f);
                         }});
    }

    std::array<float, rk::kMaxSpeakers> gains{};
    animating_ = false;
    for (int s = 0; s < (int)scene_.sources.size(); ++s) {
        const auto& src = scene_.sources[(size_t)s];
        const auto c = project(src.position);
        const auto base = project({src.position.x, src.position.y, -kStand});
        const bool dim = s >= dimFrom_;
        const juce::Colour col = src.mute ? p.text3 : sourceColour(s);
        const float lvl = s < (int)sourceLevels_.size() ? sourceLevels_[(size_t)s] : 0.0f;
        struct Path3 { juce::Point<float> to; float energy; uint32_t seed; };
        std::vector<Path3> lines;
        if (!dim && !src.mute && (s == sel_ || lvl > 0.02f)) {
            panner_.compute({src.position, src.spread}, gains.data());
            for (int m = 0; m < nSpk; ++m)
                if (gains[(size_t)m] > 0.04f) {
                    const float gm = gains[(size_t)m];
                    lines.push_back({project(scene_.layout.speakers[(size_t)m].position).p,
                                     std::max(gm * gm * std::sqrt(lvl), s == sel_ ? 0.10f * gm * gm : 0.0f), (uint32_t)(s * 131 + m + 1)});
                }
        }
        const juce::String name = src.name;
        const bool names = names_;
        animating_ = animating_ || !lines.empty();
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        items.push_back({c.depth, [&g, &p, c, base, dim, col, lines, name, names, sizeAt, lvl, now] {
                             const float r = dim ? sizeAt(6.0f, c.depth) : sizeAt(10.0f, c.depth);
                             const float a = dim ? 0.45f : 1.0f;
                             // Drop line and shadow on the floor: where the source sits and how high.
                             juce::Path drop, dashed;
                             drop.startNewSubPath(base.p);
                             drop.lineTo(c.p);
                             const float dash[] = {3.0f, 3.0f};
                             juce::PathStrokeType(1.0f).createDashedStroke(dashed, drop, dash, 2);
                             g.setColour(col.withAlpha(0.5f * a));
                             g.fillPath(dashed);
                             g.setColour(col.withAlpha(0.22f * a));
                             g.fillEllipse(base.p.x - r, base.p.y - r * 0.32f, 2 * r, r * 0.64f);
                             for (const auto& l : lines) {
                                 drawSoundParticles(g, c.p, l.to, col, l.energy, l.seed, now);
                                 drawSoundCloud(g, l.to, col, l.energy, 9.0f, 12.0f, l.seed + 5000u, now);
                             }
                             if (!dim && lvl > 0.02f) drawSoundCloud(g, c.p, col, std::sqrt(lvl), 12.0f, 20.0f, (uint32_t)name.hashCode(), now);
                             if (!dim && lvl > 0.01f) {
                                 const float rr = r + 6.0f;
                                 const float a0 = juce::MathConstants<float>::pi * 1.25f, sweep = juce::MathConstants<float>::pi * 1.5f;
                                 juce::Path arc;
                                 arc.addCentredArc(c.p.x, c.p.y, rr, rr, 0.0f, a0, a0 + sweep * juce::jlimit(0.0f, 1.0f, lvl), true);
                                 g.setColour(lvl > 0.97f ? p.bad : col);
                                 g.strokePath(arc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                             }
                             g.setColour(col.withAlpha(a));
                             g.fillEllipse(c.p.x - r, c.p.y - r, 2 * r, 2 * r);
                             if (!dim) {
                                 g.setColour(juce::Colours::white);
                                 g.drawEllipse(c.p.x - r, c.p.y - r, 2 * r, 2 * r, 2.0f);
                             }
                             if (names || !dim) {
                                 g.setColour(dim ? p.text2.withAlpha(0.7f) : p.text);
                                 g.setFont(uiFont(dim ? 11.5f : 12.5f, !dim));
                                 g.drawText(name, juce::Rectangle<float>(c.p.x + r + 8, c.p.y - 9, 180, 18), juce::Justification::centredLeft, true);
                             }
                         }});
    }

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.depth > b.depth; });
    for (auto& it : items) it.draw();
    if (animating_ != isTimerRunning()) {
        if (animating_) startTimerHz(30);
        else stopTimer();
    }
}

void View3D::mouseDown(const juce::MouseEvent& e) {
    yaw0_ = yaw_;
    pitch0_ = pitch_;
    // A click on a source selects it.
    for (int s = std::min((int)scene_.sources.size(), dimFrom_) - 1; s >= 0; --s)
        if (project(scene_.sources[(size_t)s].position).p.getDistanceFrom(e.position) < 14.0f) {
            sel_ = s;
            if (onSourceSelected) onSourceSelected(s);
            repaint();
            return;
        }
}

void View3D::mouseDrag(const juce::MouseEvent& e) {
    yaw_ = yaw0_ + (float)e.getDistanceFromDragStartX() * 0.008f;
    pitch_ = juce::jlimit(0.08f, 1.45f, pitch0_ + (float)e.getDistanceFromDragStartY() * 0.006f);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    repaint();
}

void View3D::mouseDoubleClick(const juce::MouseEvent&) { resetView(); }

void View3D::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    zoom_ = juce::jlimit(0.4f, 3.0f, zoom_ * (1.0f + w.deltaY * 0.5f));
    repaint();
}

} // namespace rkui
