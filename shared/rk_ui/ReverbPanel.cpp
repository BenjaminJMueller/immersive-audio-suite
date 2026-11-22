#include "ReverbPanel.h"

#include "Strings.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rkui {
using rk::kClusterMs;
using rk::kEarlyGain;
using rk::kEarlyMs;

namespace {

constexpr float kFMin = 20.0f, kFMax = 20000.0f;
constexpr int kHistory = 10;
const float kBandHz[3] = {100.0f, 1000.0f, 8000.0f};

rk::ReverbParams preset(float decay, float damping, float pre, float size, float low, float early, float cluster, float tail) {
    rk::ReverbParams p;
    p.decaySec = decay;
    p.dampingHz = damping;
    p.preDelayMs = pre;
    p.size = size;
    p.lowRatio = low;
    p.earlyDb = early;
    p.clusterDb = cluster;
    p.tailDb = tail;
    return p;
}

bool near(float a, float b, float tol) { return std::abs(a - b) <= tol; }

float niceStep(float range) {
    if (range <= 1.2f) return 0.2f;
    if (range <= 3.0f) return 0.5f;
    if (range <= 8.0f) return 1.0f;
    return 2.0f;
}

juce::String secText(float s) { return num(s, s < 10.0f ? 1 : 0) + " s"; }

juce::String hzText(float hz) {
    if (hz >= 1000.0f) return juce::String(juce::roundToInt(hz / 1000.0f)) + "k";
    return juce::String(juce::roundToInt(hz));
}

} // namespace

const std::vector<RoomPreset>& roomPresets() {
    static const std::vector<RoomPreset> list = {
        {"Studio", preset(0.5f, 9000.0f, 4.0f, 0.5f, 1.1f, -4.0f, -10.0f, -2.0f), -10.0f},
        {"Kammer", preset(1.1f, 7000.0f, 8.0f, 0.7f, 1.2f, -5.0f, -9.0f, -1.0f), -8.0f},
        {"Saal", preset(2.4f, 5500.0f, 24.0f, 1.2f, 1.3f, -6.0f, -9.0f, -4.0f), -6.0f},
        {"Kirche", preset(4.8f, 4000.0f, 40.0f, 1.9f, 1.4f, -9.0f, -8.0f, 0.0f), -5.0f},
        {"Platte", preset(1.8f, 12000.0f, 0.0f, 0.45f, 0.8f, -80.0f, -12.0f, 0.0f), -8.0f},
    };
    return list;
}

int matchRoomPreset(const rk::ReverbParams& raw, float wetDb) {
    const rk::ReverbParams p = rk::sanitize(raw);
    const auto& list = roomPresets();
    for (int i = 0; i < (int)list.size(); ++i) {
        const rk::ReverbParams& q = list[(size_t)i].params;
        if (near(p.decaySec, q.decaySec, 0.01f) && near(p.dampingHz, q.dampingHz, 5.0f) && near(p.preDelayMs, q.preDelayMs, 0.5f) &&
            near(p.size, q.size, 0.005f) && near(p.lowRatio, q.lowRatio, 0.005f) && near(p.earlyDb, q.earlyDb, 0.05f) &&
            near(p.clusterDb, q.clusterDb, 0.05f) && near(p.tailDb, q.tailDb, 0.05f) && near(wetDb, list[(size_t)i].wetDb, 0.05f))
            return i;
    }
    return -1;
}

float bandFrequency3(int band) { return kBandHz[juce::jlimit(0, 2, band)]; }

float reverbBandRt(const rk::ReverbParams& p, int band) { return rk::rt60At(p, bandFrequency3(band)); }

float roomVolume(float size) { return 1000.0f * size * size * size; }

void setReverbBandRt(rk::ReverbParams& p, int band, float seconds) {
    p = rk::sanitize(p);
    seconds = juce::jlimit(0.1f, 20.0f, seconds);
    const float lowBefore = reverbBandRt(p, 0), highBefore = reverbBandRt(p, 2);
    auto solveLow = [&p](float target) {
        // RT at 100 Hz rises with lowRatio: bisect.
        float lo = 0.3f, hi = 3.0f;
        for (int i = 0; i < 30; ++i) {
            p.lowRatio = 0.5f * (lo + hi);
            if (reverbBandRt(p, 0) < target) lo = p.lowRatio;
            else hi = p.lowRatio;
        }
        p.lowRatio = 0.5f * (lo + hi);
    };
    auto solveHigh = [&p](float target) { p.dampingHz = rk::dampingForRt(p, bandFrequency3(2), target); };
    if (band == 0) {
        solveLow(seconds);
    } else if (band == 2) {
        solveHigh(seconds);
    } else {
        // decaySec sets the mid band; find the value that lands exactly at 1 kHz.
        float lo = 0.2f, hi = 12.0f;
        for (int i = 0; i < 30; ++i) {
            p.decaySec = 0.5f * (lo + hi);
            if (reverbBandRt(p, 1) < seconds) lo = p.decaySec;
            else hi = p.decaySec;
        }
        p.decaySec = 0.5f * (lo + hi);
        solveLow(lowBefore);
        solveHigh(highBefore);
    }
    p = rk::sanitize(p);
}

// ---------------------------------------------------------------------------------------

ReverbPanel::ReverbPanel() {
    setTooltip(tr("Die Kurve zeigt, wie lange der Raum je Tonhöhe nachklingt. Die Punkte für Tiefen, Mitten und Höhen lassen sich nach oben und unten ziehen."));
}

void ReverbPanel::setParams(const rk::ReverbParams& raw, float wetDb) {
    const rk::ReverbParams p = rk::sanitize(raw);
    const bool same = std::memcmp(&p, &params_, sizeof(p)) == 0 && std::memcmp(&wetDb, &wetDb_, sizeof(float)) == 0;
    if (same) return;
    params_ = p;
    wetDb_ = wetDb;
    if (drag_ < 0) scaleRt_ = maxRt();
    repaint();
}

void ReverbPanel::setBands(const std::array<float, SpectrumAnalyzer::kBands>& db) {
    history_.insert(history_.begin(), db);
    if ((int)history_.size() > kHistory) history_.resize(kHistory);
    repaint(curveArea().toNearestInt().expanded(2));
}

void ReverbPanel::setCoach(juce::String badge, juce::String title, juce::String text) {
    coachBadge_ = std::move(badge);
    coachTitle_ = std::move(title);
    coachText_ = std::move(text);
    repaint();
}

float ReverbPanel::maxRt() const {
    float m = 0.0f;
    for (int i = 0; i <= 40; ++i) m = std::max(m, rk::rt60At(params_, kFMin * std::pow(kFMax / kFMin, (float)i / 40.0f)));
    const float step = niceStep(m * 1.25f);
    return std::max(step * 2.0f, std::ceil(m * 1.25f / step) * step);
}

juce::Rectangle<float> ReverbPanel::timeArea() const {
    const float coach = coachTitle_.isNotEmpty() ? 96.0f : 24.0f;
    const float h = juce::jlimit(80.0f, 150.0f, getHeight() * 0.2f);
    return {64.0f, getHeight() - coach - 22.0f - h, std::max(10.0f, getWidth() - 64.0f - 48.0f), h};
}

juce::Rectangle<float> ReverbPanel::curveArea() const {
    const auto t = timeArea();
    const float top = 52.0f;
    const float bottom = t.getY() - 48.0f - 52.0f; // frequency axis + time lane header
    return {t.getX(), top, t.getWidth(), std::max(40.0f, bottom - top)};
}

float ReverbPanel::xForHz(float hz) const {
    const auto a = curveArea();
    return a.getX() + a.getWidth() * std::log(hz / kFMin) / std::log(kFMax / kFMin);
}

float ReverbPanel::yForRt(float rt) const {
    const auto a = curveArea();
    return a.getBottom() - a.getHeight() * juce::jlimit(0.0f, 1.0f, rt / scaleRt_);
}

juce::Point<float> ReverbPanel::handlePos(int band) const {
    return {xForHz(bandFrequency3(band)), yForRt(reverbBandRt(params_, band))};
}

int ReverbPanel::hitHandle(juce::Point<float> p) const {
    for (int b = 0; b < 3; ++b)
        if (handlePos(b).getDistanceFrom(p) < 14.0f) return b;
    return -1;
}

void ReverbPanel::paint(juce::Graphics& g) {
    g.fillAll(palette().stage);
    paintCurve(g);
    paintTime(g);
    paintCoach(g);
}

void ReverbPanel::paintCurve(juce::Graphics& g) {
    const Palette& p = palette();
    const auto a = curveArea();
    const bool off = wetDb_ <= -79.0f;
    const juce::Colour blue = p.accent.withAlpha(off ? 0.4f : 1.0f);

    g.setFont(uiFont(11.5f));
    g.setColour(p.text3);
    g.drawText(tr("Nachhallzeit"), juce::Rectangle<float>(a.getX() - 44.0f, a.getY() - 30.0f, 200.0f, 16.0f), juce::Justification::centredLeft);

    // Grid: seconds horizontally, octave-ish frequencies vertically.
    const float step = niceStep(scaleRt_);
    for (float s = 0.0f; s <= scaleRt_ + 1e-3f; s += step) {
        const float y = yForRt(s);
        g.setColour(p.text.withAlpha(0.07f));
        g.drawHorizontalLine(juce::roundToInt(y), a.getX(), a.getRight());
        g.setColour(p.text3);
        g.drawText(secText(s), juce::Rectangle<float>(a.getX() - 50.0f, y - 8.0f, 42.0f, 16.0f), juce::Justification::centredRight);
    }
    for (float f : {20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f}) {
        const float x = xForHz(f);
        g.setColour(p.text.withAlpha(0.07f));
        g.drawVerticalLine(juce::roundToInt(x), a.getY(), a.getBottom());
        g.setColour(p.text3);
        g.drawText(hzText(f), juce::Rectangle<float>(x - 24.0f, a.getBottom() + 8.0f, 48.0f, 16.0f), juce::Justification::centred);
    }
    g.drawText(tr("Frequenz (Hz)"), juce::Rectangle<float>(a.getX(), a.getBottom() + 26.0f, a.getWidth(), 16.0f), juce::Justification::centred);

    // The curve from the real network.
    juce::Path curve;
    const int n = 160;
    for (int i = 0; i <= n; ++i) {
        const float f = kFMin * std::pow(kFMax / kFMin, (float)i / (float)n);
        const juce::Point<float> pt(xForHz(f), yForRt(rk::rt60At(params_, f)));
        if (i == 0) curve.startNewSubPath(pt);
        else curve.lineTo(pt);
    }
    juce::Path fill(curve);
    fill.lineTo(a.getRight(), a.getBottom());
    fill.lineTo(a.getX(), a.getBottom());
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(blue.withAlpha(off ? 0.12f : 0.30f), 0.0f, a.getY(), blue.withAlpha(0.04f), 0.0f, a.getBottom(), false));
    g.fillPath(fill);

    // What is playing right now, as faint lines inside the room's curve (newest on top).
    float loudest = -200.0f;
    for (const auto& frame : history_)
        for (float v : frame) loudest = std::max(loudest, v);
    if (!history_.empty() && !off && loudest > -80.0f) {
        g.saveState();
        g.reduceClipRegion(fill);
        for (int k = (int)history_.size() - 1; k >= 0; --k) {
            juce::Path line;
            const auto& db = history_[(size_t)k];
            for (int b = 0; b < SpectrumAnalyzer::kBands; ++b) {
                const float f = SpectrumAnalyzer::bandFrequency(b);
                const float v = juce::jlimit(0.0f, 1.0f, (db[(size_t)b] + 84.0f) / 84.0f) * (1.0f - 0.06f * (float)k);
                const float top = yForRt(rk::rt60At(params_, f));
                const juce::Point<float> pt(xForHz(f), a.getBottom() - (a.getBottom() - top) * v);
                if (b == 0) line.startNewSubPath(pt);
                else line.lineTo(pt);
            }
            g.setColour(juce::Colours::white.withAlpha(0.16f * (1.0f - (float)k / (float)kHistory)));
            g.strokePath(line, juce::PathStrokeType(1.0f));
        }
        g.restoreState();
    }

    g.setColour(blue);
    g.strokePath(curve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The three points.
    const juce::String names[3] = {tr("Tiefen"), tr("Mitten"), tr("Höhen")};
    for (int b = 0; b < 3; ++b) {
        const auto h = handlePos(b);
        const bool active = b == drag_ || b == hover_;
        g.setColour(blue.withAlpha(0.45f));
        const float dash[] = {3.0f, 3.0f};
        g.drawDashedLine(juce::Line<float>(h.x, h.y + 9.0f, h.x, a.getBottom()), dash, 2, 1.0f);
        const auto r = juce::Rectangle<float>(18.0f, 18.0f).withCentre(h);
        g.setColour(active ? p.accent : p.stage);
        g.fillEllipse(r);
        g.setColour(juce::Colours::white);
        g.drawEllipse(r.reduced(1.0f), 2.0f);
        g.setFont(uiFont(12.5f, active));
        g.setColour(p.text);
        const juce::String label = names[b] + juce::String::fromUTF8(" \xc2\xb7 ") + secText(reverbBandRt(params_, b));
        g.drawText(label, juce::Rectangle<float>(h.x - 70.0f, h.y - 32.0f, 140.0f, 16.0f), juce::Justification::centred);
    }
}

void ReverbPanel::paintTime(juce::Graphics& g) {
    const Palette& p = palette();
    const auto a = timeArea();
    const bool off = wetDb_ <= -79.0f;
    const float dim = off ? 0.35f : 1.0f;
    const float size = params_.size;
    const float pre = params_.preDelayMs;

    // Range: enough for the reflections and half of the tail.
    const float need = std::max(pre + kClusterMs[rk::kClusterTaps - 1] * size + 60.0f, params_.decaySec * 500.0f);
    float range = 5000.0f;
    for (float r : {300.0f, 600.0f, 1200.0f, 2500.0f, 5000.0f})
        if (r >= need) { range = r; break; }
    auto xOf = [&](float ms) { return a.getX() + a.getWidth() * juce::jlimit(0.0f, 1.0f, ms / range); };
    auto hOf = [&](float amp) { return a.getHeight() * juce::jlimit(0.0f, 1.0f, std::sqrt(std::max(0.0f, amp))); };
    auto gainOf = [](float db) { return db <= -79.9f ? 0.0f : std::pow(10.0f, db / 20.0f); };

    g.setFont(uiFont(11.5f));
    g.setColour(p.text3);
    g.drawText(tr("Zeitverlauf"), juce::Rectangle<float>(a.getX() - 44.0f, a.getY() - 46.0f, 200.0f, 16.0f), juce::Justification::centredLeft);
    g.setColour(p.text.withAlpha(0.15f));
    g.drawHorizontalLine(juce::roundToInt(a.getBottom()), a.getX(), a.getRight());
    const float tick = range / 6.0f;
    for (int i = 0; i <= 6; ++i) {
        const float ms = tick * (float)i;
        g.setColour(p.text3);
        g.drawText(juce::String(juce::roundToInt(ms)) + " ms", juce::Rectangle<float>(xOf(ms) - 36.0f, a.getBottom() + 6.0f, 72.0f, 14.0f),
                   juce::Justification::centred);
    }

    const juce::Colour light = juce::Colour(0xff64d2ff).withMultipliedAlpha(dim);
    const juce::Colour blue = p.accent.withMultipliedAlpha(dim);

    // Tail: from the first pass through the network, fading with the mid reverb time.
    const float tailStart = pre + 29.7f * size;
    const float tailAmp = 0.3f * gainOf(params_.tailDb);
    if (tailAmp > 0.0f) {
        juce::Path line;
        for (int i = 0; i <= 80; ++i) {
            const float t = tailStart + (range - tailStart) * (float)i / 80.0f;
            const float amp = tailAmp * std::pow(10.0f, -3.0f * (t - tailStart) * 0.001f / params_.decaySec);
            if (i == 0) line.startNewSubPath(xOf(t), a.getBottom() - hOf(amp));
            else line.lineTo(xOf(t), a.getBottom() - hOf(amp));
        }
        juce::Path env(line);
        env.lineTo(a.getRight(), a.getBottom());
        env.lineTo(xOf(tailStart), a.getBottom());
        env.closeSubPath();
        g.setGradientFill(juce::ColourGradient(blue.withAlpha(0.35f * dim), 0.0f, a.getY(), blue.withAlpha(0.05f * dim), 0.0f, a.getBottom(), false));
        g.fillPath(env);
        g.setColour(blue);
        g.strokePath(line, juce::PathStrokeType(1.5f));
    }

    // Cluster: the diffused taps, fading with the room.
    const float clusterLevel = gainOf(params_.clusterDb);
    if (clusterLevel > 0.0f)
        for (int k = 0; k < rk::kClusterTaps; ++k) {
            const float ms = pre + kClusterMs[k] * size;
            const float amp = 0.9f * clusterLevel * std::pow(10.0f, -3.0f * kClusterMs[k] * size * 0.001f / params_.decaySec);
            g.setColour(blue);
            g.fillRect(juce::Rectangle<float>(xOf(ms) - 1.0f, a.getBottom() - hOf(amp), 2.0f, hOf(amp)));
            // The diffusers smear every tap into a dense little burst.
            const float next = k + 1 < rk::kClusterTaps ? pre + kClusterMs[k + 1] * size : ms + 12.0f * size;
            g.setColour(blue.withMultipliedAlpha(0.55f));
            for (int j = 1; j < 6; ++j) {
                const float t = ms + (next - ms) * (float)j / 6.0f;
                const float jitter = 0.35f + 0.5f * (float)(((k * 7 + j * 13) % 10)) / 10.0f;
                const float h = hOf(amp * jitter * (1.0f - 0.1f * (float)j));
                g.fillRect(juce::Rectangle<float>(xOf(t) - 0.5f, a.getBottom() - h, 1.0f, h));
            }
        }

    // Early reflections.
    const float earlyLevel = gainOf(params_.earlyDb);
    if (earlyLevel > 0.0f)
        for (int k = 0; k < rk::kEarlyTaps; ++k) {
            const float ms = pre + kEarlyMs[k] * size;
            const float amp = kEarlyGain[k] * earlyLevel;
            g.setColour(light);
            g.fillRect(juce::Rectangle<float>(xOf(ms) - 1.5f, a.getBottom() - hOf(amp), 3.0f, hOf(amp)));
        }

    // Direct sound.
    g.setColour(p.bad.withMultipliedAlpha(0.9f));
    g.fillRect(juce::Rectangle<float>(xOf(0.0f) - 1.0f, a.getY(), 2.0f, a.getHeight()));
    g.setColour(p.text3);
    g.setFont(uiFont(11.0f));
    g.drawText(tr("Direktschall"), juce::Rectangle<float>(xOf(0.0f) + 6.0f, a.getY() - 2.0f, 120.0f, 14.0f), juce::Justification::centredLeft);

    // Labels for the parts, left to right without overlapping.
    struct Lbl { juce::String text; float ms; bool on; };
    const Lbl labels[3] = {{tr("Frühe Reflexionen"), pre + kEarlyMs[0] * size, earlyLevel > 0.0f},
                           {tr("Cluster"), pre + kClusterMs[4] * size, clusterLevel > 0.0f},
                           {tr("Nachhall"), std::max(tailStart, range * 0.35f), tailAmp > 0.0f}};
    float minX = 0.0f;
    g.setFont(uiFont(12.5f));
    for (const auto& l : labels) {
        const float w = juce::GlyphArrangement::getStringWidth(uiFont(12.5f), l.text) + 4.0f;
        const float x = std::max(xOf(l.ms), minX);
        g.setColour((l.on ? p.text : p.text3).withMultipliedAlpha(dim));
        g.drawText(l.text, juce::Rectangle<float>(x, a.getY() - 24.0f, w, 16.0f), juce::Justification::centredLeft);
        minX = x + w + 12.0f;
    }
}

void ReverbPanel::paintCoach(juce::Graphics& g) {
    if (coachTitle_.isEmpty()) return;
    drawCoachBar(g, getLocalBounds(), coachBadge_, coachTitle_, coachText_, false);
}

void ReverbPanel::mouseMove(const juce::MouseEvent& e) {
    const int h = hitHandle(e.position);
    if (h != hover_) {
        hover_ = h;
        setMouseCursor(h >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ReverbPanel::mouseExit(const juce::MouseEvent&) {
    if (hover_ >= 0) {
        hover_ = -1;
        repaint();
    }
}

void ReverbPanel::mouseDown(const juce::MouseEvent& e) {
    drag_ = hitHandle(e.position);
    if (drag_ >= 0) scaleRt_ = maxRt();
}

void ReverbPanel::mouseDrag(const juce::MouseEvent& e) {
    if (drag_ < 0) return;
    const auto a = curveArea();
    const float rt = scaleRt_ * juce::jlimit(0.02f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
    setReverbBandRt(params_, drag_, rt);
    repaint();
    if (onChange) onChange(params_);
}

void ReverbPanel::mouseUp(const juce::MouseEvent&) {
    if (drag_ < 0) return;
    drag_ = -1;
    scaleRt_ = maxRt();
    repaint();
    if (onCommit) onCommit();
}

} // namespace rkui
