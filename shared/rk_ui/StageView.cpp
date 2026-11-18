#include "StageView.h"

#include "Strings.h"

namespace rkui {

StageView::StageView() : views_(juce::StringArray{tr("Oben"), tr("Vorne"), tr("Seite")}, false) {
    setOpaque(true);
    views_.setTooltip(tr("Ansicht: von oben für den Azimut, von vorne oder von der Seite für die Elevation"));
    views_.onChange = [this](int i) { setProjection(static_cast<Projection>(i)); };
    addAndMakeVisible(views_);
    fit_.setTooltip(tr("Alles zeigen"));
    fit_.onClick = [this] { zoom_ = 1.0f; updateScale(); repaint(); };
    addAndMakeVisible(fit_);
}

void StageView::resized() {
    fit_.setBounds(getWidth() - 40, 10, 30, 26);
    const int vw = juce::jmin(views_.idealWidth(), getWidth() / 2);
    views_.setBounds(getWidth() - 48 - vw, 8, vw, 30);
    updateScale();
}

void StageView::setSourceLevels(const std::vector<float>& l) {
    const double now = juce::Time::getMillisecondCounterHiRes();
    prevSourceLevels_.resize(l.size(), 0.0f);
    for (size_t s = 0; s < l.size(); ++s) {
        // An onset (level jumps up) sends a ring outwards, like a drop in water.
        if (l[s] > 0.35f && l[s] - prevSourceLevels_[s] > 0.12f && ripples_.size() < 48) ripples_.push_back({(int)s, now});
        prevSourceLevels_[s] = l[s];
    }
    ripples_.erase(std::remove_if(ripples_.begin(), ripples_.end(), [now](const Ripple& r) { return now - r.start > 1400.0; }), ripples_.end());
    sourceLevels_ = l;
    repaint();
}

void StageView::setProjection(Projection p) {
    proj_ = p;
    views_.setSelected(static_cast<int>(p));
    zoom_ = 1.0f;
    updateScale();
    repaint();
}

juce::Point<float> StageView::plane(rk::Vec3 p) const {
    switch (proj_) {
    case Projection::Front: return {p.x, p.z};
    case Projection::Side: return {p.y, p.z};
    default: return {p.x, p.y};
    }
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
    for (const auto& sp : scene_.layout.speakers) {
        const auto q = plane(sp.position);
        maxR = std::max(maxR, proj_ == Projection::Top ? std::hypot(q.x, q.y) : std::max(std::abs(q.x), std::abs(q.y)));
    }
    const float extent = maxR * 1.2f + 0.6f;
    const auto b = getLocalBounds().toFloat().reduced(24.0f);
    scale_ = std::max(4.0f, std::min(b.getWidth(), b.getHeight()) * 0.5f / extent) * zoom_;
    centre_ = getLocalBounds().toFloat().getCentre();
    if (proj_ != Projection::Top) {
        // Front and side: most setups sit at or above ear height, so give the space above more room.
        float minZ = 0.0f;
        for (const auto& sp : scene_.layout.speakers) minZ = std::min(minZ, sp.position.z);
        if (minZ > -0.3f) centre_.y = (float)getHeight() * 0.68f;
    }
}

juce::Point<float> StageView::toScreen(rk::Vec3 p) const {
    const auto q = plane(p);
    return {centre_.x + q.x * scale_, centre_.y - q.y * scale_};
}

rk::Vec3 StageView::toWorld(juce::Point<float> s, rk::Vec3 keep) const {
    const float h = (s.x - centre_.x) / scale_, v = (centre_.y - s.y) / scale_;
    switch (proj_) {
    case Projection::Front: return {h, keep.y, v};
    case Projection::Side: return {keep.x, h, v};
    default: return {h, v, keep.z};
    }
}

void StageView::paintGrid(juce::Graphics& g, float step) {
    const Palette& p = palette();
    g.setFont(uiFont(11.5f));
    if (proj_ == Projection::Top) {
        // Distance rings every metre (every 5 m when zoomed out) and axes.
        const float maxDist = std::hypot((float)getWidth(), (float)getHeight()) / scale_;
        for (float r = step; r < maxDist; r += step) {
            const bool major = std::fmod(r, step * 5.0f) < 0.01f;
            g.setColour((major ? p.lineStrong : p.line).withMultipliedAlpha(0.7f));
            g.drawEllipse(centre_.x - r * scale_, centre_.y - r * scale_, 2 * r * scale_, 2 * r * scale_, 0.8f);
        }
        g.setColour(p.line);
        g.drawLine(0, centre_.y, (float)getWidth(), centre_.y, 1.0f);
        g.drawLine(centre_.x, 0, centre_.x, (float)getHeight(), 1.0f);
        g.setColour(p.text3);
        g.drawText(tr("vorne"), juce::Rectangle<float>(centre_.x - 40, 8, 80, 16), juce::Justification::centred);
        g.drawText(num(step, 0) + " m", juce::Rectangle<float>(centre_.x + step * scale_ + 4, centre_.y + 2, 60, 14), juce::Justification::left);
        return;
    }
    // Front and side: a metre grid, the floor of the listening plane and the elevation arcs.
    const float w = (float)getWidth(), h = (float)getHeight();
    g.setColour(p.line.withMultipliedAlpha(0.6f));
    for (float x = std::fmod(centre_.x, step * scale_); x < w; x += step * scale_) g.drawVerticalLine(juce::roundToInt(x), 0.0f, h);
    for (float y = std::fmod(centre_.y, step * scale_); y < h; y += step * scale_) g.drawHorizontalLine(juce::roundToInt(y), 0.0f, w);
    g.setColour(p.lineStrong);
    g.drawLine(0, centre_.y, w, centre_.y, 1.2f); // ear height
    g.drawLine(centre_.x, 0, centre_.x, h, 1.0f);
    // Elevation guides every 30 degrees on the circle through the farthest speaker.
    float r = 0.0f;
    for (const auto& sp : scene_.layout.speakers) r = std::max(r, rk::length(sp.position));
    if (r > 0.1f) {
        g.setColour(p.lineStrong.withAlpha(0.6f));
        juce::Path arc;
        arc.addCentredArc(centre_.x, centre_.y, r * scale_, r * scale_, 0.0f, -juce::MathConstants<float>::halfPi * 1.0f,
                          juce::MathConstants<float>::halfPi * 1.0f, true);
        g.strokePath(arc, juce::PathStrokeType(1.0f));
        g.setColour(p.text3);
        g.setFont(uiFont(10.5f));
        for (int deg : {30, 60}) {
            const float a = juce::degreesToRadians((float)deg);
            for (float side : {-1.0f, 1.0f}) {
                const juce::Point<float> q{centre_.x + side * std::cos(a) * r * scale_, centre_.y - std::sin(a) * r * scale_};
                g.setColour(p.line);
                g.drawLine(juce::Line<float>(centre_, q), 1.0f);
                g.setColour(p.text3);
                g.drawText(juce::String(deg) + juce::String::fromUTF8("\xc2\xb0"),
                           juce::Rectangle<float>(q.x - 20 + side * 18, q.y - 16, 40, 14), juce::Justification::centred);
            }
        }
    }
    g.setColour(p.text3);
    g.setFont(uiFont(11.5f));
    const bool side = proj_ == Projection::Side;
    g.drawText(side ? tr("hinten") : tr("rechts"), juce::Rectangle<float>(12, centre_.y + 4, 80, 16), juce::Justification::left);
    g.drawText(side ? tr("vorne") : tr("links"), juce::Rectangle<float>(w - 92, centre_.y + 4, 80, 16), juce::Justification::right);
    g.drawText(tr("oben"), juce::Rectangle<float>(centre_.x - 40, 44, 80, 16), juce::Justification::centred);
    g.drawText(num(step, 0) + " m", juce::Rectangle<float>(centre_.x + step * scale_ + 4, centre_.y + 2, 60, 14), juce::Justification::left);
}

void StageView::paint(juce::Graphics& g) {
    const Palette& p = palette();
    g.fillAll(p.stage);

    const float step = scale_ < 12.0f ? 5.0f : 1.0f;
    paintGrid(g, step);

    // Listener.
    {
        const float hr = 7.0f;
        g.setColour(p.text2);
        g.drawEllipse(centre_.x - hr, centre_.y - hr, 2 * hr, 2 * hr, 1.5f);
        juce::Path nose;
        nose.addTriangle(centre_.x - 3.5f, centre_.y - hr + 1, centre_.x + 3.5f, centre_.y - hr + 1, centre_.x, centre_.y - hr - 5);
        // Top: the nose points to the front. Side: front is to the right. Front: we look at the face.
        if (proj_ == Projection::Side) g.fillPath(nose, juce::AffineTransform::rotation(juce::MathConstants<float>::halfPi, centre_.x, centre_.y));
        else if (proj_ == Projection::Top) g.fillPath(nose);
    }

    // ---- Sound field ------------------------------------------------------------------
    // What every speaker radiates right now, as a smooth field in the colours of the sources
    // it carries, with crisp contour lines on top (like a topographic map of the sound).
    const int nSpk = (int)scene_.layout.speakers.size();
    std::vector<float> mixR((size_t)nSpk, 0.0f), mixG((size_t)nSpk, 0.0f), mixB((size_t)nSpk, 0.0f), mixE((size_t)nSpk, 0.0f);
    std::vector<std::array<float, rk::kMaxSpeakers>> srcGains(scene_.sources.size());
    for (int s = 0; s < (int)scene_.sources.size(); ++s) {
        const auto& src = scene_.sources[(size_t)s];
        const float lvl = s < (int)sourceLevels_.size() ? sourceLevels_[(size_t)s] : 0.0f;
        const bool sel = target_ != EditTarget::Speakers && s == selSource_;
        if (src.mute || (lvl < 0.02f && !sel)) { srcGains[(size_t)s][0] = -1.0f; continue; }
        panner_.compute({src.position, src.spread}, srcGains[(size_t)s].data());
        const juce::Colour col = itemColour(s);
        for (int m = 0; m < nSpk; ++m) {
            const float e = srcGains[(size_t)s][(size_t)m] * srcGains[(size_t)s][(size_t)m] * lvl;
            mixR[(size_t)m] += col.getFloatRed() * e;
            mixG[(size_t)m] += col.getFloatGreen() * e;
            mixB[(size_t)m] += col.getFloatBlue() * e;
            mixE[(size_t)m] += e;
        }
    }
    auto speakerColour = [&](int m) {
        if (mixE[(size_t)m] > 1e-5f)
            return juce::Colour::fromFloatRGBA(mixR[(size_t)m] / mixE[(size_t)m], mixG[(size_t)m] / mixE[(size_t)m], mixB[(size_t)m] / mixE[(size_t)m], 1.0f);
        return juce::Colour(0xff8fd3ff); // reverb or test signals only
    };
    paintField(g, speakerColour);

    // ---- Signal paths --------------------------------------------------------------------
    // Fine lines from each sounding source to its speakers, fading towards the speaker.
    for (int s = 0; s < (int)scene_.sources.size(); ++s) {
        if (srcGains[(size_t)s][0] < 0.0f) continue;
        const auto& src = scene_.sources[(size_t)s];
        const float lvl = s < (int)sourceLevels_.size() ? sourceLevels_[(size_t)s] : 0.0f;
        const bool sel = target_ != EditTarget::Speakers && s == selSource_;
        const auto a = toScreen(src.position);
        const juce::Colour col = itemColour(s);
        for (int m = 0; m < nSpk; ++m) {
            const float gm = srcGains[(size_t)s][(size_t)m];
            if (gm < 0.06f) continue;
            const auto b = toScreen(scene_.layout.speakers[(size_t)m].position);
            const float alpha = juce::jlimit(0.0f, 0.85f, std::max(sel ? 0.55f * gm * gm + 0.08f : 0.0f, 0.9f * gm * std::sqrt(lvl)));
            if (alpha < 0.02f) continue;
            juce::ColourGradient grad(col.withAlpha(alpha), a.x, a.y, col.withAlpha(alpha * 0.18f), b.x, b.y, false);
            g.setGradientFill(grad);
            g.drawLine(juce::Line<float>(a, b), sel ? 1.25f : 1.0f);
        }
    }

    // ---- Speakers ------------------------------------------------------------------------
    for (int m = 0; m < nSpk; ++m) {
        const auto& spk = scene_.layout.speakers[(size_t)m];
        const auto c = toScreen(spk.position);
        const juce::Colour col = spk.enabled ? itemColour(spk.group + 3) : p.text3;
        const bool sel = target_ == EditTarget::Speakers && m == selSpeaker_;
        const float lvl = m < (int)speakerLevels_.size() ? speakerLevels_[(size_t)m] : 0.0f;
        if (spk.isSubwoofer) {
            g.setColour(col.withAlpha(0.85f));
            g.fillEllipse(c.x - 11, c.y - 11, 22, 22);
            g.setColour(juce::Colours::white);
            g.setFont(uiFont(11.0f, true));
            g.drawText("S", juce::Rectangle<float>(c.x - 11, c.y - 11, 22, 22), juce::Justification::centred);
        } else {
            const auto aim = plane(spk.aimDirection());
            const bool showCone = std::hypot(aim.x, aim.y) > 0.2f; // aimed into the screen: no cone
            const float ang = std::atan2(aim.x, aim.y); // screen rotation, 0 = pointing up
            juce::Path body;
            body.addRoundedRectangle(-10.0f, -8.0f, 20.0f, 16.0f, 4.0f);
            juce::Path cone;
            cone.addTriangle(-7.0f, -8.0f, 7.0f, -8.0f, 0.0f, -15.0f);
            const auto tf = juce::AffineTransform::rotation(ang).translated(c.x, c.y);
            g.setColour(col.withAlpha(0.35f));
            if (showCone) g.fillPath(cone, tf);
            g.setColour(col);
            g.fillPath(body, tf);
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.strokePath(body, juce::PathStrokeType(1.0f), tf);
            g.setColour(juce::Colours::white);
            g.setFont(uiFont(10.5f, true));
            g.drawText(juce::String(m + 1), juce::Rectangle<float>(c.x - 10, c.y - 8, 20, 16), juce::Justification::centred);
        }
        // Segment meter under the speaker, lit in the colours of what it plays.
        {
            constexpr int kSeg = 8;
            const float segW = 2.5f, gap = 1.0f, w = kSeg * segW + (kSeg - 1) * gap;
            const float x0 = c.x - w * 0.5f, y0 = c.y + 13.0f;
            const int lit = juce::roundToInt(lvl * kSeg);
            const juce::Colour on = lvl > 0.97f ? p.bad : speakerColour(m);
            for (int k = 0; k < kSeg; ++k) {
                g.setColour(k < lit ? on.withAlpha(0.55f + 0.45f * (float)k / kSeg) : juce::Colours::white.withAlpha(0.07f));
                g.fillRoundedRectangle(x0 + k * (segW + gap), y0, segW, 3.0f, 0.8f);
            }
        }
        if (sel) {
            g.setColour(p.text);
            g.drawEllipse(c.x - 16, c.y - 16, 32, 32, 1.5f);
        }
    }

    // ---- Sources -------------------------------------------------------------------------
    for (int s = 0; s < (int)scene_.sources.size(); ++s) {
        const auto& src = scene_.sources[(size_t)s];
        const auto c = toScreen(src.position);
        const float lvl = s < (int)sourceLevels_.size() ? sourceLevels_[(size_t)s] : 0.0f;
        const juce::Colour col = src.mute ? p.text3 : itemColour(s);
        // Width: an arc segment around the listener at the source's distance (top view).
        if (src.spread > 0.01f && proj_ == Projection::Top) {
            const float r = std::hypot(c.x - centre_.x, c.y - centre_.y);
            if (r > 20.0f) {
                const float az = std::atan2(c.x - centre_.x, centre_.y - c.y);
                const float half = src.spread * juce::MathConstants<float>::halfPi;
                juce::Path band;
                band.addCentredArc(centre_.x, centre_.y, r + 3.5f, r + 3.5f, 0.0f, az - half, az + half, true);
                band.addCentredArc(centre_.x, centre_.y, r - 3.5f, r - 3.5f, 0.0f, az + half, az - half, false);
                band.closeSubPath();
                g.setColour(col.withAlpha(0.10f));
                g.fillPath(band);
                g.setColour(col.withAlpha(0.45f));
                g.strokePath(band, juce::PathStrokeType(0.9f));
            }
        }
        // Level: a fine ring meter around the source (270 degrees, open at the bottom).
        {
            const float rr = 16.5f;
            const float a0 = juce::MathConstants<float>::pi * 1.25f, sweep = juce::MathConstants<float>::pi * 1.5f;
            juce::Path track;
            track.addCentredArc(c.x, c.y, rr, rr, 0.0f, a0, a0 + sweep, true);
            g.setColour(juce::Colours::white.withAlpha(0.09f));
            g.strokePath(track, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (lvl > 0.01f && !src.mute) {
                juce::Path arc;
                arc.addCentredArc(c.x, c.y, rr, rr, 0.0f, a0, a0 + sweep * juce::jlimit(0.0f, 1.0f, lvl), true);
                g.setColour(lvl > 0.97f ? p.bad : col);
                g.strokePath(arc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        const bool sel = target_ != EditTarget::Speakers && s == selSource_;
        drawNumberBadge(g, {c.x - 11, c.y - 11, 22, 22}, s + 1, col, false);
        if (sel) {
            g.setColour(p.text);
            g.drawEllipse(c.x - 21.5f, c.y - 21.5f, 43.0f, 43.0f, 1.2f);
        }
        if (s >= firstLocked_) { // from the DAW: small marker
            g.setColour(p.text3);
            g.setFont(uiFont(9.5f, true));
            g.drawText("DAW", juce::Rectangle<float>(c.x - 20, c.y - 34, 40, 12), juce::Justification::centred);
        }
        if (proj_ == Projection::Top && std::abs(src.position.z) > 0.05f) {
            g.setColour(p.text2);
            g.setFont(uiFont(10.5f));
            g.drawText((src.position.z > 0 ? "+" : "") + num(src.position.z, 1) + " m",
                       juce::Rectangle<float>(c.x - 30, c.y + 20, 60, 14), juce::Justification::centred);
        }
    }

    paintLegend(g);
    paintCoach(g);
}

void StageView::paintField(juce::Graphics& g, const std::function<juce::Colour(int)>& speakerColour) {
    const int nSpk = (int)scene_.layout.speakers.size();
    float total = 0.0f;
    for (int m = 0; m < nSpk && m < (int)speakerLevels_.size(); ++m) total += speakerLevels_[(size_t)m];
    if (total < 0.01f || nSpk == 0) return;

    float maxR = 1.0f;
    for (const auto& sp : scene_.layout.speakers) maxR = std::max(maxR, rk::length(sp.position));
    const float sigma = std::max(0.7f, 0.32f * maxR) * scale_; // spread of each speaker's sound, pixels
    const float inv2s2 = 1.0f / (2.0f * sigma * sigma);

    constexpr int kCell = 6;
    const int gw = getWidth() / kCell + 2, gh = getHeight() / kCell + 2;
    std::vector<float> v((size_t)(gw * gh), 0.0f), cr(v.size(), 0.0f), cg(v.size(), 0.0f), cb(v.size(), 0.0f);
    struct Emitter { float x, y, l; juce::Colour c; };
    std::vector<Emitter> em;
    for (int m = 0; m < nSpk && m < (int)speakerLevels_.size(); ++m) {
        const float l = speakerLevels_[(size_t)m];
        if (l < 0.01f) continue;
        const auto c = toScreen(scene_.layout.speakers[(size_t)m].position);
        em.push_back({c.x, c.y, l, speakerColour(m)});
    }
    float peak = 1e-6f;
    for (int y = 0; y < gh; ++y)
        for (int x = 0; x < gw; ++x) {
            const float px = (float)(x * kCell), py = (float)(y * kCell);
            float sum = 0.0f, r = 0.0f, gg = 0.0f, b = 0.0f;
            for (const auto& e : em) {
                const float dx = px - e.x, dy = py - e.y;
                const float w = e.l * std::exp(-(dx * dx + dy * dy) * inv2s2);
                sum += w;
                r += w * e.c.getFloatRed();
                gg += w * e.c.getFloatGreen();
                b += w * e.c.getFloatBlue();
            }
            const size_t i = (size_t)(y * gw + x);
            v[i] = sum;
            if (sum > 1e-6f) { cr[i] = r / sum; cg[i] = gg / sum; cb[i] = b / sum; }
            peak = std::max(peak, sum);
        }
    const float norm = 1.0f / std::max(1.0f, peak);

    // Colour layer, upscaled smoothly (no blur filter: the field itself is smooth).
    juce::Image img(juce::Image::ARGB, gw, gh, true);
    {
        juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < gh; ++y)
            for (int x = 0; x < gw; ++x) {
                const size_t i = (size_t)(y * gw + x);
                const float t = juce::jlimit(0.0f, 1.0f, v[i] * norm);
                const float a = 0.30f * t * t * (3.0f - 2.0f * t);
                bd.setPixelColour(x, y, juce::Colour::fromFloatRGBA(cr[i], cg[i], cb[i], a));
            }
    }
    juce::Graphics::ScopedSaveState clipState(g);
    if (proj_ != Projection::Top) {
        // Front and side: sound lives above the floor (ear height and up, unless speakers sit lower).
        float minZ = 0.0f;
        for (const auto& sp : scene_.layout.speakers) minZ = std::min(minZ, sp.position.z);
        g.reduceClipRegion(0, 0, getWidth(), juce::roundToInt(centre_.y - minZ * scale_));
    }
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImageTransformed(img, juce::AffineTransform::scale((float)kCell), false);

    // Contour lines (marching squares) at even steps: crisp, thin and quiet.
    for (int k = 2; k <= 6; ++k) {
        const float iso = (float)k / 7.0f / norm;
        juce::Path path;
        for (int y = 0; y + 1 < gh; ++y)
            for (int x = 0; x + 1 < gw; ++x) {
                const float a = v[(size_t)(y * gw + x)], b = v[(size_t)(y * gw + x + 1)];
                const float c = v[(size_t)((y + 1) * gw + x + 1)], d = v[(size_t)((y + 1) * gw + x)];
                const int code = (a > iso ? 1 : 0) | (b > iso ? 2 : 0) | (c > iso ? 4 : 0) | (d > iso ? 8 : 0);
                if (code == 0 || code == 15) continue;
                const float X = (float)(x * kCell), Y = (float)(y * kCell), S = (float)kCell;
                auto lerp = [iso](float p0, float p1) { return juce::jlimit(0.0f, 1.0f, (iso - p0) / (p1 - p0 + 1e-9f)); };
                const juce::Point<float> top{X + S * lerp(a, b), Y}, right{X + S, Y + S * lerp(b, c)},
                    bottom{X + S * lerp(d, c), Y + S}, left{X, Y + S * lerp(a, d)};
                auto seg = [&path](juce::Point<float> p0, juce::Point<float> p1) {
                    path.startNewSubPath(p0);
                    path.lineTo(p1);
                };
                switch (code) {
                case 1: case 14: seg(left, top); break;
                case 2: case 13: seg(top, right); break;
                case 3: case 12: seg(left, right); break;
                case 4: case 11: seg(right, bottom); break;
                case 6: case 9: seg(top, bottom); break;
                case 7: case 8: seg(left, bottom); break;
                case 5: seg(left, top); seg(right, bottom); break;
                case 10: seg(top, right); seg(left, bottom); break;
                default: break;
                }
            }
        g.setColour(juce::Colours::white.withAlpha(0.04f + 0.04f * (float)k));
        g.strokePath(path, juce::PathStrokeType(k == 6 ? 1.0f : 0.7f));
    }
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
    auto snap = [this](juce::Point<float> q) {
        const float h = std::round((q.x - centre_.x) / scale_ * 2.0f) * 0.5f, v = std::round((centre_.y - q.y) / scale_ * 2.0f) * 0.5f;
        return juce::Point<float>{centre_.x + h * scale_, centre_.y - v * scale_};
    };
    juce::Point<float> pt = e.position;
    pt.x = juce::jlimit(0.0f, (float)getWidth(), pt.x);
    pt.y = juce::jlimit(0.0f, (float)getHeight(), pt.y);
    if (dragSource_ >= 0 && dragSource_ < (int)scene_.sources.size()) {
        auto& src = scene_.sources[(size_t)dragSource_];
        rk::Vec3 w = toWorld(pt, src.position);
        if (e.mods.isShiftDown()) w = toWorld(snap(pt), src.position); // 0.5 m grid
        src.position = w;
        if (onSourceMoved) onSourceMoved(dragSource_, w);
    } else if (dragSpeaker_ >= 0 && dragSpeaker_ < (int)scene_.layout.speakers.size()) {
        auto& spk = scene_.layout.speakers[(size_t)dragSpeaker_];
        rk::Vec3 w = toWorld(pt, spk.position);
        if (e.mods.isShiftDown()) w = toWorld(snap(pt), spk.position);
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
