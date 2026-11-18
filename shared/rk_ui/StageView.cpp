#include "StageView.h"

#include "Strings.h"

namespace rkui {

StageView::StageView() {
    setOpaque(true);
    fit_.setTooltip(tr("Alles zeigen"));
    fit_.onClick = [this] { zoom_ = 1.0f; updateScale(); repaint(); };
    addAndMakeVisible(fit_);
}

void StageView::resized() {
    fit_.setBounds(getWidth() - 40, 10, 30, 26);
    updateScale();
}

void StageView::setCoach(juce::String badge, juce::String title, juce::String text, bool ok) {
    coachBadge_ = std::move(badge);
    coachTitle_ = std::move(title);
    coachText_ = std::move(text);
    coachOk_ = ok;
    repaint();
}

void StageView::paintLegend(juce::Graphics& g) {
    const Palette& p = palette();
    const juce::String a = tr("Lautsprecher"), b = tr("Quelle"), c = tr("Anteil");
    const juce::Font f = uiFont(11.5f);
    auto tw = [&f](const juce::String& t) { return juce::GlyphArrangement::getStringWidth(f, t); };
    const float w = 14 + 16 + 6 + tw(a) + 14 + 14 + 6 + tw(b) + 14 + 18 + 6 + tw(c) + 14;
    auto r = juce::Rectangle<float>(10.0f, 10.0f, w, 26.0f);
    g.setColour(p.bar.withAlpha(0.85f));
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(p.line);
    g.drawRoundedRectangle(r.reduced(0.25f), 8.0f, 0.75f);
    float x = r.getX() + 14.0f;
    const float cy = r.getCentreY();
    g.setColour(itemColour(3));
    g.fillRoundedRectangle(x, cy - 5.0f, 14.0f, 10.0f, 3.0f);
    x += 20.0f;
    g.setColour(p.text2);
    g.setFont(f);
    g.drawText(a, juce::Rectangle<float>(x, r.getY(), tw(a) + 2, r.getHeight()), juce::Justification::centredLeft);
    x += tw(a) + 14.0f;
    g.setColour(itemColour(0));
    g.fillEllipse(x, cy - 6.0f, 12.0f, 12.0f);
    x += 18.0f;
    g.setColour(p.text2);
    g.drawText(b, juce::Rectangle<float>(x, r.getY(), tw(b) + 2, r.getHeight()), juce::Justification::centredLeft);
    x += tw(b) + 14.0f;
    g.setColour(itemColour(0).withAlpha(0.6f));
    g.drawLine(x, cy, x + 16.0f, cy, 1.6f);
    x += 22.0f;
    g.setColour(p.text2);
    g.drawText(c, juce::Rectangle<float>(x, r.getY(), tw(c) + 2, r.getHeight()), juce::Justification::centredLeft);
}

void StageView::paintCoach(juce::Graphics& g) {
    if (coachTitle_.isEmpty()) return;
    const Palette& p = palette();
    const juce::Font tf = uiFont(13.5f, true), sf = uiFont(12.0f);
    const float textW = std::max(juce::GlyphArrangement::getStringWidth(tf, coachTitle_), juce::GlyphArrangement::getStringWidth(sf, coachText_));
    const float w = std::min((float)getWidth() - 32.0f, 10.0f + 28.0f + 12.0f + textW + 18.0f);
    auto r = juce::Rectangle<float>(w, 50.0f).withCentre({getWidth() * 0.5f, (float)getHeight() - 20.0f - 25.0f});
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.fillRoundedRectangle(r.translated(0, 4).expanded(2), 15.0f);
    g.setColour(juce::Colour(0xff1c1c1e).withAlpha(0.88f));
    g.fillRoundedRectangle(r, 14.0f);
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawRoundedRectangle(r.reduced(0.25f), 14.0f, 0.75f);
    auto badge = juce::Rectangle<float>(r.getX() + 10.0f, r.getCentreY() - 14.0f, 28.0f, 28.0f);
    g.setColour(coachOk_ ? p.ok : juce::Colour(0xff0a84ff));
    g.fillEllipse(badge);
    g.setColour(juce::Colours::white);
    if (coachOk_ && coachBadge_.isEmpty()) {
        juce::Path t;
        t.startNewSubPath(badge.getX() + 8.5f, badge.getCentreY());
        t.lineTo(badge.getX() + 12.5f, badge.getY() + 18.0f);
        t.lineTo(badge.getX() + 20.0f, badge.getY() + 10.0f);
        g.strokePath(t, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    } else {
        g.setFont(uiFont(13.5f, true));
        g.drawText(coachBadge_, badge, juce::Justification::centred, false);
    }
    auto text = r.withTrimmedLeft(50.0f).withTrimmedRight(16.0f);
    g.setColour(juce::Colour(0xfff2f2f4));
    g.setFont(tf);
    g.drawText(coachTitle_, text.withTrimmedTop(8.0f).withHeight(18.0f), juce::Justification::centredLeft, true);
    g.setColour(juce::Colour(0xffebebf5).withAlpha(0.68f));
    g.setFont(sf);
    g.drawText(coachText_, text.withTrimmedTop(26.0f).withHeight(16.0f), juce::Justification::centredLeft, true);
}

void StageView::mouseMove(const juce::MouseEvent& e) {
    bool over = false;
    if (target_ == EditTarget::Sources) { const int s = hitSource(e.position); over = s >= 0 && s < firstLocked_; }
    else if (target_ == EditTarget::Speakers) over = hitSpeaker(e.position) >= 0;
    setMouseCursor(over ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void StageView::setScene(const Scene& s) {
    const bool layoutChanged = s.layout.speakers.size() != scene_.layout.speakers.size() || s.effectivePanner() != scene_.effectivePanner() ||
                               s.layout.name != scene_.layout.name || !dragging_;
    scene_ = s;
    if (layoutChanged) {
        rk::DbapPanner::Params d{s.dbapRolloffDb, s.dbapBlur, s.dbapDirectivity};
        panner_.prepare(s.layout, s.effectivePanner(), d, 3);
    }
    if (!dragging_) updateScale();
    repaint();
}

void StageView::updateScale() {
    float maxR = 2.0f;
    for (const auto& sp : scene_.layout.speakers) maxR = std::max(maxR, std::hypot(sp.position.x, sp.position.y));
    const float extent = maxR * 1.2f + 0.6f;
    const auto b = getLocalBounds().toFloat().reduced(24.0f);
    scale_ = std::max(4.0f, std::min(b.getWidth(), b.getHeight()) * 0.5f / extent) * zoom_;
    centre_ = getLocalBounds().toFloat().getCentre();
}

juce::Point<float> StageView::toScreen(rk::Vec3 p) const { return {centre_.x + p.x * scale_, centre_.y - p.y * scale_}; }

rk::Vec3 StageView::toWorld(juce::Point<float> s, float z) const {
    return {(s.x - centre_.x) / scale_, (centre_.y - s.y) / scale_, z};
}

void StageView::paint(juce::Graphics& g) {
    const Palette& p = palette();
    g.fillAll(p.stage);

    // Distance rings every metre (every 5 m when zoomed out) and axes.
    const float step = scale_ < 12.0f ? 5.0f : 1.0f;
    const float maxDist = std::hypot((float)getWidth(), (float)getHeight()) / scale_;
    for (float r = step; r < maxDist; r += step) {
        const bool major = std::fmod(r, step * 5.0f) < 0.01f;
        g.setColour(major ? p.lineStrong : p.line);
        g.drawEllipse(centre_.x - r * scale_, centre_.y - r * scale_, 2 * r * scale_, 2 * r * scale_, 1.0f);
    }
    g.setColour(p.line);
    g.drawLine(0, centre_.y, (float)getWidth(), centre_.y, 1.0f);
    g.drawLine(centre_.x, 0, centre_.x, (float)getHeight(), 1.0f);
    g.setColour(p.text3);
    g.setFont(uiFont(11.5f));
    g.drawText(language() == Language::German ? "vorne" : "front", juce::Rectangle<float>(centre_.x - 40, 8, 80, 16), juce::Justification::centred);
    g.drawText(num(step, 0) + " m", juce::Rectangle<float>(centre_.x + step * scale_ + 4, centre_.y + 2, 60, 14), juce::Justification::left);

    // Listener.
    {
        const float hr = 7.0f;
        g.setColour(p.text2);
        g.drawEllipse(centre_.x - hr, centre_.y - hr, 2 * hr, 2 * hr, 1.5f);
        juce::Path nose;
        nose.addTriangle(centre_.x - 3.5f, centre_.y - hr + 1, centre_.x + 3.5f, centre_.y - hr + 1, centre_.x, centre_.y - hr - 5);
        g.fillPath(nose);
    }

    // Lines from the selected (or each) source to the speakers it plays on.
    const int nSpk = (int)scene_.layout.speakers.size();
    float gains[rk::kMaxSpeakers];
    for (int s = 0; s < (int)scene_.sources.size(); ++s) {
        const auto& src = scene_.sources[(size_t)s];
        if (src.mute) continue;
        const bool sel = s == selSource_;
        if (!sel && selSource_ >= 0) continue;
        panner_.compute({src.position, src.spread}, gains);
        const auto sp = toScreen(src.position);
        for (int m = 0; m < nSpk; ++m) {
            if (gains[m] < 0.08f) continue;
            g.setColour(itemColour(s).withAlpha(juce::jlimit(0.08f, 0.7f, gains[m] * gains[m] * (sel ? 0.9f : 0.4f))));
            g.drawLine(juce::Line<float>(sp, toScreen(scene_.layout.speakers[(size_t)m].position)), sel ? 1.6f : 1.0f);
        }
    }

    // Speakers.
    for (int m = 0; m < nSpk; ++m) {
        const auto& spk = scene_.layout.speakers[(size_t)m];
        const auto c = toScreen(spk.position);
        const juce::Colour col = spk.enabled ? itemColour(spk.group + 3) : p.text3;
        const bool sel = target_ == EditTarget::Speakers && m == selSpeaker_;
        const float lvl = m < (int)speakerLevels_.size() ? speakerLevels_[(size_t)m] : 0.0f;
        if (lvl > 0.01f) {
            const float gr = 13.0f + 10.0f * lvl;
            g.setColour((lvl > 0.95f ? p.bad : p.ok).withAlpha(0.18f + 0.3f * lvl));
            g.fillEllipse(c.x - gr, c.y - gr, 2 * gr, 2 * gr);
        }
        if (spk.isSubwoofer) {
            g.setColour(col.withAlpha(0.85f));
            g.fillEllipse(c.x - 11, c.y - 11, 22, 22);
            g.setColour(juce::Colours::white);
            g.setFont(uiFont(11.0f, true));
            g.drawText("S", juce::Rectangle<float>(c.x - 11, c.y - 11, 22, 22), juce::Justification::centred);
        } else {
            const rk::Vec3 aim = spk.aimDirection();
            const float ang = std::atan2(aim.x, aim.y); // screen rotation, 0 = pointing up
            juce::Path body;
            body.addRoundedRectangle(-10.0f, -8.0f, 20.0f, 16.0f, 4.0f);
            juce::Path cone;
            cone.addTriangle(-7.0f, -8.0f, 7.0f, -8.0f, 0.0f, -15.0f);
            const auto tf = juce::AffineTransform::rotation(ang).translated(c.x, c.y);
            g.setColour(col.withAlpha(0.35f));
            g.fillPath(cone, tf);
            g.setColour(col);
            g.fillPath(body, tf);
            g.setColour(juce::Colours::white);
            g.setFont(uiFont(10.5f, true));
            g.drawText(juce::String(m + 1), juce::Rectangle<float>(c.x - 10, c.y - 8, 20, 16), juce::Justification::centred);
        }
        if (sel) {
            g.setColour(p.text);
            g.drawEllipse(c.x - 16, c.y - 16, 32, 32, 1.5f);
        }
    }

    // Sources.
    for (int s = 0; s < (int)scene_.sources.size(); ++s) {
        const auto& src = scene_.sources[(size_t)s];
        const auto c = toScreen(src.position);
        const float lvl = s < (int)sourceLevels_.size() ? sourceLevels_[(size_t)s] : 0.0f;
        const float r = 12.0f + 18.0f * src.spread;
        if (src.spread > 0.01f) {
            g.setColour(itemColour(s).withAlpha(0.15f));
            g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
        }
        if (lvl > 0.01f) {
            const float gr = 12.0f + 6.0f * lvl;
            g.setColour(itemColour(s).withAlpha(0.35f * lvl));
            g.fillEllipse(c.x - gr, c.y - gr, 2 * gr, 2 * gr);
        }
        const bool sel = target_ != EditTarget::Speakers && s == selSource_;
        drawNumberBadge(g, {c.x - 11, c.y - 11, 22, 22}, s + 1, src.mute ? p.text3 : itemColour(s), sel);
        if (std::abs(src.position.z) > 0.05f) {
            g.setColour(p.text2);
            g.setFont(uiFont(10.5f));
            g.drawText((src.position.z > 0 ? "+" : "") + num(src.position.z, 1) + " m",
                       juce::Rectangle<float>(c.x - 30, c.y + 12, 60, 14), juce::Justification::centred);
        }
    }

    paintLegend(g);
    paintCoach(g);
}

int StageView::hitSource(juce::Point<float> pt) const {
    for (int s = (int)scene_.sources.size() - 1; s >= 0; --s)
        if (toScreen(scene_.sources[(size_t)s].position).getDistanceFrom(pt) < 14.0f) return s;
    return -1;
}

int StageView::hitSpeaker(juce::Point<float> pt) const {
    for (int m = (int)scene_.layout.speakers.size() - 1; m >= 0; --m)
        if (toScreen(scene_.layout.speakers[(size_t)m].position).getDistanceFrom(pt) < 14.0f) return m;
    return -1;
}

void StageView::mouseDown(const juce::MouseEvent& e) {
    dragSource_ = dragSpeaker_ = -1;
    const auto pt = e.position;
    if (target_ == EditTarget::Sources) {
        dragSource_ = hitSource(pt);
        if (dragSource_ >= 0) {
            selSource_ = dragSource_;
            if (onSourceSelected) onSourceSelected(dragSource_);
            if (dragSource_ >= firstLocked_) dragSource_ = -1; // shown, but positioned elsewhere
        }
    } else if (target_ == EditTarget::Speakers) {
        dragSpeaker_ = hitSpeaker(pt);
        if (dragSpeaker_ >= 0) {
            selSpeaker_ = dragSpeaker_;
            if (onSpeakerSelected) onSpeakerSelected(dragSpeaker_);
        }
    }
    dragging_ = dragSource_ >= 0 || dragSpeaker_ >= 0;
    repaint();
}

void StageView::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_) return;
    juce::Point<float> pt = e.position;
    pt.x = juce::jlimit(0.0f, (float)getWidth(), pt.x);
    pt.y = juce::jlimit(0.0f, (float)getHeight(), pt.y);
    if (dragSource_ >= 0 && dragSource_ < (int)scene_.sources.size()) {
        auto& src = scene_.sources[(size_t)dragSource_];
        rk::Vec3 w = toWorld(pt, src.position.z);
        if (e.mods.isShiftDown()) { // snap to 0.5 m grid
            w.x = std::round(w.x * 2.0f) * 0.5f;
            w.y = std::round(w.y * 2.0f) * 0.5f;
        }
        src.position = w;
        if (onSourceMoved) onSourceMoved(dragSource_, w);
    } else if (dragSpeaker_ >= 0 && dragSpeaker_ < (int)scene_.layout.speakers.size()) {
        auto& spk = scene_.layout.speakers[(size_t)dragSpeaker_];
        rk::Vec3 w = toWorld(pt, spk.position.z);
        if (e.mods.isShiftDown()) {
            w.x = std::round(w.x * 2.0f) * 0.5f;
            w.y = std::round(w.y * 2.0f) * 0.5f;
        }
        spk.position = w;
        if (onSpeakerMoved) onSpeakerMoved(dragSpeaker_, w);
    }
    repaint();
}

void StageView::mouseUp(const juce::MouseEvent&) {
    const bool was = dragging_;
    dragging_ = false;
    dragSource_ = dragSpeaker_ = -1;
    if (was && onDragEnd) onDragEnd();
    updateScale();
    repaint();
}

void StageView::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    zoom_ = juce::jlimit(0.25f, 4.0f, zoom_ * (1.0f + w.deltaY * 0.5f));
    updateScale();
    repaint();
}

} // namespace rkui
