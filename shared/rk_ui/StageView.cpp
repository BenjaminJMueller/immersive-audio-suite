#include "StageView.h"
#include <tuple>
#include "Visuals.h"

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
    if (!levelsDiffer(sourceLevels_, l)) return;
    sourceLevels_ = l;
    repaint();
}

void StageView::setReverbLevel(float unit) {
    unit = std::isfinite(unit) ? juce::jlimit(0.0f, 1.0f, unit) : 0.0f;
    if (std::abs(unit - reverbLevel_) < 0.005f) return;
    reverbLevel_ = unit;
    repaint();
}

void StageView::setSourceStyle(std::vector<juce::Colour> colours, int dimFrom, bool names) {
    sourceColours_ = std::move(colours);
    dimFrom_ = dimFrom;
    sourceNames_ = names;
    repaint();
}

void StageView::setShowViewSwitch(bool on) {
    views_.setVisible(on);
}

juce::Colour StageView::sourceColour(int s) const {
    if (s >= 0 && s < (int)sourceColours_.size() && !sourceColours_[(size_t)s].isTransparent()) return sourceColours_[(size_t)s];
    return itemColour(s);
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

void StageView::setCalibration(CalibrationOverlay o) {
    calib_ = std::move(o);
    repaint();
}

void StageView::clearCalibration() {
    calib_.reset();
    repaint();
}

void StageView::paintCalibrationBelow(juce::Graphics& g) {
    if (!calib_) return;
    const Palette& p = palette();
    const int n = (int)std::min(calib_->before.size(), scene_.layout.speakers.size());
    for (int m = 0; m < n; ++m) {
        if (m >= (int)calib_->moved.size() || !calib_->moved[(size_t)m]) continue;
        const auto a = toScreen(calib_->before[(size_t)m]);
        const auto b = toScreen(scene_.layout.speakers[(size_t)m].position);
        juce::Path line;
        line.startNewSubPath(a);
        line.lineTo(b);
        juce::Path dashed;
        const float dash[] = {3.0f, 3.0f};
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, line, dash, 2);
        g.setColour(p.text2.withAlpha(0.7f));
        g.fillPath(dashed);
        juce::Path box;
        box.addRoundedRectangle(a.x - 10.0f, a.y - 8.0f, 20.0f, 16.0f, 4.0f);
        juce::Path dashedBox;
        juce::PathStrokeType(1.0f).createDashedStroke(dashedBox, box, dash, 2);
        g.fillPath(dashedBox);
    }
}

void StageView::paintCalibrationAbove(juce::Graphics& g) {
    if (!calib_) return;
    const Palette& p = palette();
    // Speaker that plays the sweep: expanding rings.
    if (calib_->playingSpeaker >= 0 && calib_->playingSpeaker < (int)scene_.layout.speakers.size()) {
        const auto c = toScreen(scene_.layout.speakers[(size_t)calib_->playingSpeaker].position);
        const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
        for (int k = 0; k < 3; ++k) {
            const float ph = (float)std::fmod(t * 0.9 + k / 3.0, 1.0);
            const float r = 14.0f + ph * 34.0f;
            g.setColour(p.accent.withAlpha(0.55f * (1.0f - ph)));
            g.drawEllipse(c.x - r, c.y - r, 2 * r, 2 * r, 1.4f);
        }
    }
    for (int j = 0; j < (int)calib_->points.size(); ++j) {
        const auto c = toScreen(calib_->points[(size_t)j]);
        const int st = j < (int)calib_->pointState.size() ? calib_->pointState[(size_t)j] : 0;
        const bool active = j == calib_->activePoint;
        const juce::Colour col = st == 1 ? p.ok : (st == 2 ? p.warn : (active ? p.accent : p.text2));
        // Crosshair in a ring: where the microphone goes.
        const float r = 9.0f;
        g.setColour(p.stage.withAlpha(0.85f));
        g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
        g.setColour(col);
        g.drawEllipse(c.x - r, c.y - r, 2 * r, 2 * r, active ? 2.0f : 1.4f);
        g.drawLine(c.x - r - 4, c.y, c.x - 3, c.y, 1.2f);
        g.drawLine(c.x + 3, c.y, c.x + r + 4, c.y, 1.2f);
        g.drawLine(c.x, c.y - r - 4, c.x, c.y - 3, 1.2f);
        g.drawLine(c.x, c.y + 3, c.x, c.y + r + 4, 1.2f);
        if (active) {
            const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
            const float ph = (float)std::fmod(t * 0.6, 1.0);
            const float rr = r + 4.0f + ph * 10.0f;
            g.setColour(p.accent.withAlpha(0.6f * (1.0f - ph)));
            g.drawEllipse(c.x - rr, c.y - rr, 2 * rr, 2 * rr, 1.2f);
        }
        g.setFont(uiFont(10.5f, true));
        g.setColour(col);
        juce::String label = "M" + juce::String(j + 1);
        if (proj_ == Projection::Top && std::abs(calib_->points[(size_t)j].z) > 0.05f)
            label << "  " << (calib_->points[(size_t)j].z > 0 ? "+" : "") << num(calib_->points[(size_t)j].z, 1) << " m";
        g.drawText(label, juce::Rectangle<float>(c.x + r + 3, c.y - r - 12, 80, 12), juce::Justification::centredLeft);
    }
}

void StageView::paintLegend(juce::Graphics& g) {
    const Palette& p = palette();
    if (calib_) {
        // While calibrating: what the markers mean.
        const juce::String a = tr("Messpunkt"), b = tr("Vorher");
        const juce::Font f = uiFont(11.5f);
        auto tw = [&f](const juce::String& t) { return juce::GlyphArrangement::getStringWidth(f, t); };
        const float w = 14 + 16 + 6 + tw(a) + 14 + 20 + 6 + tw(b) + 14;
        auto r = juce::Rectangle<float>(10.0f, 10.0f, w, 26.0f);
        g.setColour(p.bar.withAlpha(0.85f));
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(p.line);
        g.drawRoundedRectangle(r.reduced(0.25f), 8.0f, 0.75f);
        float x = r.getX() + 14.0f;
        const float cy = r.getCentreY();
        g.setColour(p.accent);
        g.drawEllipse(x + 2, cy - 6.0f, 12.0f, 12.0f, 1.4f);
        x += 22.0f;
        g.setColour(p.text2);
        g.setFont(f);
        g.drawText(a, juce::Rectangle<float>(x, r.getY(), tw(a) + 2, r.getHeight()), juce::Justification::centredLeft);
        x += tw(a) + 14.0f;
        juce::Path box;
        box.addRoundedRectangle(x, cy - 5.0f, 16.0f, 10.0f, 3.0f);
        juce::Path dashed;
        const float dash[] = {3.0f, 3.0f};
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, box, dash, 2);
        g.fillPath(dashed);
        x += 22.0f;
        g.drawText(b, juce::Rectangle<float>(x, r.getY(), tw(b) + 2, r.getHeight()), juce::Justification::centredLeft);
        return;
    }
    // What the markers mean: speakers, the source(s), other tracks, the sound particles.
    struct Item { int kind; juce::Colour c; juce::String t; }; // 0 box, 1 dot, 2 particles
    std::vector<Item> items;
    items.push_back({0, itemColour(3), tr("Lautsprecher")});
    const bool named = sourceNames_ && !scene_.sources.empty();
    if (!sourceNames_ || named) {
        const bool tracks = named && dimFrom_ >= (int)scene_.sources.size() && scene_.sources.size() > 1;
        items.push_back({1, sourceColour(0), tracks ? tr("Spuren") : (named ? scene_.sources[0].name : tr("Quelle"))});
        if (named && (int)scene_.sources.size() > dimFrom_) items.push_back({1, p.text3, tr("andere Spuren")});
        items.push_back({2, sourceColour(0), tr("Signalverteilung")});
        if (scene_.reverbWetDb > -79.0f) items.push_back({2, reverbColour(), tr("Hall")});
    }
    const juce::Font f = uiFont(11.5f);
    auto tw = [&f](const juce::String& t) { return juce::GlyphArrangement::getStringWidth(f, t); };
    float w = 14.0f;
    for (const auto& it : items) w += (it.kind == 2 ? 22.0f : 20.0f) + tw(it.t) + 14.0f;
    auto r = juce::Rectangle<float>(10.0f, legendBottom_ ? (float)getHeight() - 36.0f : 10.0f, w, 26.0f);
    g.setColour(p.bar.withAlpha(0.85f));
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(p.line);
    g.drawRoundedRectangle(r.reduced(0.25f), 8.0f, 0.75f);
    float x = r.getX() + 14.0f;
    const float cy = r.getCentreY();
    g.setFont(f);
    for (const auto& it : items) {
        g.setColour(it.c);
        if (it.kind == 0) g.fillRoundedRectangle(x, cy - 5.0f, 14.0f, 10.0f, 3.0f);
        else if (it.kind == 1) g.fillEllipse(x + 1.0f, cy - 6.0f, 12.0f, 12.0f);
        else
            for (const auto [dx, dy, d] : {std::tuple{2.0f, 1.5f, 3.2f}, {7.5f, -2.5f, 2.4f}, {12.5f, 1.0f, 2.8f}}) {
                g.setColour(it.c.withAlpha(0.25f));
                g.fillEllipse(x + dx - d, cy + dy - d, 2.0f * d, 2.0f * d);
                g.setColour(it.c.interpolatedWith(juce::Colours::white, 0.3f).withAlpha(0.8f));
                g.fillEllipse(x + dx - d * 0.4f, cy + dy - d * 0.4f, d * 0.8f, d * 0.8f);
            }
        x += it.kind == 2 ? 22.0f : 20.0f;
        g.setColour(p.text2);
        g.drawText(it.t, juce::Rectangle<float>(x, r.getY(), tw(it.t) + 2, r.getHeight()), juce::Justification::centredLeft);
        x += tw(it.t) + 14.0f;
    }
}

void StageView::paintCoach(juce::Graphics& g) {
    if (coachTitle_.isEmpty()) return;
    drawCoachBar(g, getLocalBounds(), coachBadge_, coachTitle_, coachText_, coachOk_);
}

void StageView::mouseMove(const juce::MouseEvent& e) {
    bool over = false;
    if (target_ == EditTarget::Sources) { const int s = hitSource(e.position); over = s >= 0 && s < firstLocked_; }
    else if (target_ == EditTarget::Speakers) over = hitSpeaker(e.position) >= 0;
    setMouseCursor(over ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void StageView::setScene(const Scene& s) {
    // While a speaker is dragged the panner is rebuilt once the drag ends, not on every move.
    if (!panningBuilt_ || !s.samePanning(scene_)) panningStale_ = true;
    const bool layoutChanged = panningStale_ && (!dragging_ || !panningBuilt_);
    if (layoutChanged) panningStale_ = false;
    panningBuilt_ = true;
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
        g.drawText(tr("vorne"), juce::Rectangle<float>(centre_.x - 40, showLegend_ && !legendBottom_ ? 44.0f : 8.0f, 80, 16), juce::Justification::centred);
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

    // ---- Sound particles -----------------------------------------------------------------
    // Glowing dots between each sounding source and the speakers it plays on; their density
    // shows how much of the source each speaker gets (gain squared times level).
    const int nSpk = (int)scene_.layout.speakers.size();
    std::array<float, rk::kMaxSpeakers> gains{};
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    int particles = 0;
    for (int s = 0; s < (int)scene_.sources.size() && s < dimFrom_; ++s) {
        const auto& src = scene_.sources[(size_t)s];
        const float lvl = s < (int)sourceLevels_.size() ? sourceLevels_[(size_t)s] : 0.0f;
        // Only sources that actually sound: silence shows nothing, not even for the selection.
        if (src.mute || lvl < 0.02f) continue;
        panner_.compute({src.position, src.spread}, gains.data());
        const auto a = toScreen(src.position);
        const juce::Colour col = sourceColour(s);
        const float width = src.spread * 18.0f;
        const float srcEnergy = std::sqrt(lvl);
        particles += drawSoundCloud(particles_, a, col, srcEnergy, 13.0f, 30.0f + src.spread * 40.0f, (uint32_t)(s * 977 + 7), now);
        for (int m = 0; m < nSpk; ++m) {
            const float gm = gains[(size_t)m];
            if (gm < 0.04f) continue;
            const auto b = toScreen(scene_.layout.speakers[(size_t)m].position);
            const float energy = gm * gm * std::sqrt(lvl);
            particles += drawSoundParticles(particles_, a, b, col, energy, (uint32_t)(s * 131 + m + 1), now, width);
            // Where the energy lands: a small cloud around the speaker.
            particles += drawSoundCloud(particles_, b, col, energy, 12.0f, 18.0f, (uint32_t)(s * 131 + m + 5000), now);
        }
        // Reverb: the source also flows, wider and in the reverb colour, to the speakers that
        // do not play it directly. There it is heard only as reverb.
        const float flow = reverbFlowEnergy(src, lvl, scene_.reverbWetDb);
        if (flow > 0.0f)
            for (int m = 0; m < nSpk; ++m) {
                const auto& sp = scene_.layout.speakers[(size_t)m];
                if (!sp.enabled || sp.isSubwoofer || gains[(size_t)m] >= 0.04f) continue;
                particles += drawSoundParticles(particles_, a, toScreen(sp.position), reverbColour(), flow,
                                                (uint32_t)(s * 131 + m + 70001), now, 10.0f, 0.8f);
            }
    }
    // Reverb: the engine plays it on every main speaker (each a different, decorrelated
    // mix), so a soft cloud in the reverb colour around each of them, fading with the tail.
    if (reverbLevel_ > 0.02f) {
        const float energy = 0.6f * reverbLevel_;
        for (int m = 0; m < nSpk; ++m) {
            const auto& sp = scene_.layout.speakers[(size_t)m];
            if (!sp.enabled || sp.isSubwoofer) continue;
            particles += drawSoundCloud(particles_, toScreen(sp.position), reverbColour(), energy, 10.0f, 34.0f, (uint32_t)(90001 + m * 37), now, 0.85f);
        }
    }
    particles_.draw(g, getLocalBounds());
    animating_ = particles > 0;
    if (animating_ != isTimerRunning()) {
        if (animating_) startTimerHz(30);
        else stopTimer();
    }

    paintCalibrationBelow(g);

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
            g.setColour(col.withAlpha(0.45f));
            if (showCone) g.fillPath(cone, tf);
            // The body fills from below with what the speaker plays right now.
            g.setColour(col.withAlpha(0.30f));
            g.fillPath(body, tf);
            if (lvl > 0.01f) {
                juce::Graphics::ScopedSaveState clip(g);
                const float h = 26.0f * juce::jlimit(0.0f, 1.0f, lvl);
                g.reduceClipRegion(juce::Rectangle<float>(c.x - 14.0f, c.y + 13.0f - h, 28.0f, h).getSmallestIntegerContainer());
                g.setColour(lvl > 0.97f ? p.bad : col);
                g.fillPath(body, tf);
            }
            g.setColour(col);
            g.strokePath(body, juce::PathStrokeType(1.2f), tf);
            g.setColour(juce::Colours::white);
            g.setFont(uiFont(10.5f, true));
            g.drawText(juce::String(m + 1), juce::Rectangle<float>(c.x - 10, c.y - 8, 20, 16), juce::Justification::centred);
        }
        if (sel) {
            g.setColour(p.text);
            g.drawEllipse(c.x - 16, c.y - 16, 32, 32, 1.5f);
        }
    }

    // ---- Sources -------------------------------------------------------------------------
    // Dimmed ones (other tracks) first, so the own source stays on top.
    std::vector<int> order;
    for (int s = (int)scene_.sources.size() - 1; s >= 0; --s) if (s >= dimFrom_) order.push_back(s);
    for (int s = 0; s < (int)scene_.sources.size() && s < dimFrom_; ++s) order.push_back(s);
    for (int s : order) {
        const auto& src = scene_.sources[(size_t)s];
        const auto c = toScreen(src.position);
        const float lvl = s < (int)sourceLevels_.size() ? sourceLevels_[(size_t)s] : 0.0f;
        const juce::Colour col = src.mute ? p.text3 : sourceColour(s);
        if (s >= dimFrom_) {
            g.setColour(col.withAlpha(0.45f));
            g.fillEllipse(c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f);
            if (sourceNames_) {
                g.setColour(p.text2.withAlpha(0.7f));
                g.setFont(uiFont(11.5f));
                g.drawText(src.name, juce::Rectangle<float>(c.x + 12, c.y - 8, 140, 16), juce::Justification::centredLeft, true);
            }
            continue;
        }
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
        // Radiation (top view): a soft fan in the direction the source faces, narrower the
        // more it is directed. Facing 0 points at the listener.
        if (src.directivity > 0.01f && proj_ == Projection::Top) {
            const float dx = centre_.x - c.x, dy = centre_.y - c.y;
            const float base = (std::abs(dx) + std::abs(dy) > 0.5f) ? std::atan2(dy, dx) : -juce::MathConstants<float>::halfPi;
            const float ang = base - src.yawDeg * juce::MathConstants<float>::pi / 180.0f; // positive = turned left
            const float half = juce::jmap(src.directivity, 1.2f, 0.45f);
            const float len = 24.0f + 14.0f * src.directivity;
            juce::Path fan;
            fan.startNewSubPath(c);
            for (int k = 0; k <= 12; ++k) {
                const float a = ang - half + 2.0f * half * (float)k / 12.0f;
                fan.lineTo(c.x + len * std::cos(a), c.y + len * std::sin(a));
            }
            fan.closeSubPath();
            g.setColour(col.withAlpha(0.16f + 0.12f * src.directivity));
            g.fillPath(fan);
            g.setColour(col.withAlpha(0.7f));
            g.drawLine(c.x + 17.0f * std::cos(ang), c.y + 17.0f * std::sin(ang), c.x + len * std::cos(ang), c.y + len * std::sin(ang), 1.5f);
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
        if (sourceNames_) {
            g.setColour(col);
            g.fillEllipse(c.x - 10.0f, c.y - 10.0f, 20.0f, 20.0f);
            g.setColour(juce::Colours::white);
            g.drawEllipse(c.x - 10.0f, c.y - 10.0f, 20.0f, 20.0f, 2.0f);
            g.setColour(p.text);
            g.setFont(uiFont(12.5f, true));
            g.drawText(src.name, juce::Rectangle<float>(c.x + 21, c.y - 9, 180, 18), juce::Justification::centredLeft, true);
        } else {
            drawNumberBadge(g, {c.x - 11, c.y - 11, 22, 22}, s + 1, col, false);
            if (sel) {
                g.setColour(p.text);
                g.drawEllipse(c.x - 21.5f, c.y - 21.5f, 43.0f, 43.0f, 1.2f);
            }
        }
        if (s >= firstDaw_ && !sourceNames_) { // from the DAW: small marker
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

    paintCalibrationAbove(g);
    if (showLegend_) paintLegend(g);
    if (caption_.isNotEmpty()) {
        g.setColour(p.text3);
        g.setFont(uiFont(11.0f, true));
        g.drawText(caption_.toUpperCase(), juce::Rectangle<float>(14.0f, showLegend_ && !legendBottom_ ? 44.0f : 12.0f, 120.0f, 16.0f),
                   juce::Justification::centredLeft);
    }
    paintCoach(g);
}

int StageView::hitSource(juce::Point<float> pt) const {
    for (int s = std::min((int)scene_.sources.size(), dimFrom_) - 1; s >= 0; --s)
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
    if (panningStale_) {
        panningStale_ = false;
        rk::DbapPanner::Params d{scene_.dbapRolloffDb, scene_.dbapBlur, scene_.dbapDirectivity};
        panner_.prepare(scene_.layout, scene_.effectivePanner(), d, 3);
    }
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
