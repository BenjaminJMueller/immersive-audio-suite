#include "rk/engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rk {

// ---------------------------------------------------------------------------
// SourceControl
// ---------------------------------------------------------------------------

void SourceControl::setPosition(Vec3 p) noexcept {
    p = safeClamp(p, 1000.0f);
    x.store(p.x, std::memory_order_relaxed);
    y.store(p.y, std::memory_order_relaxed);
    z.store(p.z, std::memory_order_relaxed);
    touch();
}

Vec3 SourceControl::position() const noexcept {
    return safeClamp(Vec3{x.load(std::memory_order_relaxed), y.load(std::memory_order_relaxed),
                          z.load(std::memory_order_relaxed)},
                     1000.0f);
}

// ---------------------------------------------------------------------------
// Runtime: everything that depends on the layout, built off the audio thread.
// ---------------------------------------------------------------------------

struct Engine::Runtime {
    EngineConfig cfg;
    Panner panner;
    int numSpeakers = 0;
    std::array<float, kMaxSpeakers> speakerGain{};
    std::vector<DelayLine> delays;
    std::vector<int> mains;
    std::vector<int> subs;
    float refRadius = 2.0f;
    float maxRadius = 2.0f;
    bool headphones = false;
    BinauralFold fold;
};

std::unique_ptr<Engine::Runtime> Engine::buildRuntime(const EngineConfig& cfg) const {
    auto rt = std::make_unique<Runtime>();
    rt->cfg = cfg;
    rt->numSpeakers = std::min(static_cast<int>(cfg.layout.speakers.size()), kMaxSpeakers);
    rt->panner.prepare(cfg.layout, cfg.panner, cfg.dbap, cfg.ambiOrder);
    rt->delays.resize(static_cast<size_t>(rt->numSpeakers));
    float radiusSum = 0.0f;
    int radiusCount = 0;
    rt->maxRadius = 0.0f;
    for (int m = 0; m < rt->numSpeakers; ++m) {
        const Speaker& s = cfg.layout.speakers[static_cast<size_t>(m)];
        rt->speakerGain[static_cast<size_t>(m)] = s.enabled ? dbToGain(s.gainDb) : 0.0f;
        const int delay = static_cast<int>(std::lround(s.delayMs * 0.001 * sampleRate_));
        rt->delays[static_cast<size_t>(m)].prepare(std::clamp(delay, 0, static_cast<int>(0.5 * sampleRate_) + 1));
        if (!s.enabled) continue;
        if (s.isSubwoofer) {
            rt->subs.push_back(m);
        } else {
            rt->mains.push_back(m);
            const float r = length(s.position);
            radiusSum += r;
            ++radiusCount;
            rt->maxRadius = std::max(rt->maxRadius, r);
        }
    }
    rt->refRadius = radiusCount > 0 ? std::max(0.5f, radiusSum / static_cast<float>(radiusCount)) : 2.0f;
    rt->maxRadius = std::max(rt->maxRadius, 0.5f);
    rt->headphones = cfg.outputMode == OutputMode::Headphones;
    if (rt->headphones) rt->fold.prepare(cfg.layout, sampleRate_);
    return rt;
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

Engine::Engine() {
    for (auto& p : outPeak_) p.store(0.0f);
    for (auto& g : limiterGr_) g.store(0.0f);
    for (auto& p : srcPeak_) p.store(0.0f);
    const ReverbParams def;
    reverbParams_[0].store(def.decaySec);
    reverbParams_[1].store(def.dampingHz);
    reverbParams_[2].store(def.preDelayMs);
    reverbParams_[3].store(def.size);
    reverbParams_[4].store(def.lowRatio);
    reverbParams_[5].store(def.earlyDb);
    reverbParams_[6].store(def.clusterDb);
    reverbParams_[7].store(def.tailDb);
    config_.layout = presets::stereo();
    for (int i = 0; i < kMaxSources; ++i) sources_[static_cast<size_t>(i)].inputChannel.store(-1);
}

Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int maxBlockSize) {
    std::lock_guard<std::mutex> lock(configMutex_);
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 768000.0) ? sampleRate : 48000.0;
    maxBlock_ = std::clamp(maxBlockSize, 16, 8192);
    const size_t mb = static_cast<size_t>(maxBlock_);
    inputCopy_.assign(static_cast<size_t>(kMaxInputs) * mb, 0.0f);
    mix_.assign(static_cast<size_t>(kMaxSpeakers) * mb, 0.0f);
    reverbBus_.assign(mb, 0.0f);
    subBus_.assign(mb, 0.0f);
    srcTmp_.assign(mb, 0.0f);
    outTmp_.assign(2 * mb, 0.0f);
    directTmp_.assign(mb, 0.0f);
    // 0.25 s covers 85 m; farther sources are held at that delay.
    dopplerLen_ = static_cast<int>(std::ceil(0.25 * sampleRate_)) + maxBlock_ + 4;
    dopplerLines_.assign(static_cast<size_t>(kDopplerSlots) * static_cast<size_t>(dopplerLen_), 0.0f);
    dopplerPos_.fill(0);
    dopplerUsed_.fill(false);
    for (int m = 0; m < kMaxSpeakers; ++m) mixPtr_[static_cast<size_t>(m)] = &mix_[static_cast<size_t>(m) * mb];
    state_.assign(static_cast<size_t>(kMaxSources), SourceState{});
    reverb_.prepare(sampleRate_);
    reverb_.setParams(reverb());
    reverbSeen_ = reverbVersion_.load();
    for (auto& l : limiter_) l.prepare(sampleRate_);
    std::memset(subLp_, 0, sizeof(subLp_));
    masterCur_ = dbToGain(masterGainDb.load());
    wetCur_ = 0.0f;
    fade_ = Fade::In;
    fadeGain_ = 0.0f;
    auto rt = buildRuntime(config_);
    numOutputChannels_.store(rt->headphones ? 2 : rt->numSpeakers);
    runtime_.resetUnsafe(std::move(rt));
}

void Engine::setConfig(EngineConfig cfg) {
    sanitize(cfg.layout);
    cfg.ambiOrder = std::clamp(cfg.ambiOrder, 1, kMaxAmbiOrder);
    if (static_cast<int>(cfg.panner) < 0 || cfg.panner >= PannerKind::count) cfg.panner = PannerKind::DBAP;
    std::lock_guard<std::mutex> lock(configMutex_);
    config_ = cfg;
    if (inputCopy_.empty()) return; // not prepared yet; prepare() builds it
    auto rt = buildRuntime(config_);
    numOutputChannels_.store(rt->headphones ? 2 : rt->numSpeakers);
    runtime_.publish(std::move(rt));
}

EngineConfig Engine::config() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return config_;
}

SourceControl& Engine::source(int index) noexcept {
    return sources_[static_cast<size_t>(std::clamp(index, 0, kMaxSources - 1))];
}

const SourceControl& Engine::source(int index) const noexcept {
    return sources_[static_cast<size_t>(std::clamp(index, 0, kMaxSources - 1))];
}

void Engine::setReverb(const ReverbParams& raw) noexcept {
    const ReverbParams p = sanitize(raw);
    reverbParams_[0].store(p.decaySec);
    reverbParams_[1].store(p.dampingHz);
    reverbParams_[2].store(p.preDelayMs);
    reverbParams_[3].store(p.size);
    reverbParams_[4].store(p.lowRatio);
    reverbParams_[5].store(p.earlyDb);
    reverbParams_[6].store(p.clusterDb);
    reverbParams_[7].store(p.tailDb);
    reverbVersion_.fetch_add(1, std::memory_order_release);
}

ReverbParams Engine::reverb() const noexcept {
    ReverbParams p;
    p.decaySec = reverbParams_[0].load();
    p.dampingHz = reverbParams_[1].load();
    p.preDelayMs = reverbParams_[2].load();
    p.size = reverbParams_[3].load();
    p.lowRatio = reverbParams_[4].load();
    p.earlyDb = reverbParams_[5].load();
    p.clusterDb = reverbParams_[6].load();
    p.tailDb = reverbParams_[7].load();
    return p;
}

void Engine::collectGarbage() {
    std::lock_guard<std::mutex> lock(configMutex_);
    runtime_.collectGarbage();
}

float Engine::takeOutputPeak(int c) noexcept {
    if (c < 0 || c >= kMaxSpeakers) return 0.0f;
    return outPeak_[static_cast<size_t>(c)].exchange(0.0f, std::memory_order_relaxed);
}

float Engine::takeLimiterReduction(int c) noexcept {
    if (c < 0 || c >= kMaxSpeakers) return 0.0f;
    const float gr = limiterGr_[static_cast<size_t>(c)].exchange(0.0f, std::memory_order_relaxed);
    return gr <= 1.0e-6f ? 0.0f : -20.0f * std::log10(std::max(1.0e-5f, 1.0f - gr));
}

float Engine::takeSourcePeak(int s) noexcept {
    if (s < 0 || s >= kMaxSources) return 0.0f;
    return srcPeak_[static_cast<size_t>(s)].exchange(0.0f, std::memory_order_relaxed);
}

void Engine::resetSourceState() noexcept {
    for (auto& st : state_) {
        st.seenVersion = 0xffffffffu;
        st.pan.fill(0.0f);
        st.cur.fill(0.0f);
        st.target.fill(0.0f);
        st.step.fill(0.0f);
        st.rampLeft = 0;
        st.airState = 0.0f;
        st.dirState = 0.0f;
        st.dopplerSlot = -1;
        st.sendCur = 0.0f;
        st.subCur = 0.0f;
        st.hadSignal = false;
    }
    dopplerUsed_.fill(false);
    for (auto& l : limiter_) l.reset();
}

namespace {

inline void storeMax(std::atomic<float>& a, float v) noexcept {
    if (v > a.load(std::memory_order_relaxed)) a.store(v, std::memory_order_relaxed);
}

// 2nd-order Butterworth low-pass (RBJ cookbook), direct form I with external state.
struct Biquad {
    float b0, b1, b2, a1, a2;
};

Biquad lowpass(double sr, double fc) {
    const double w = 2.0 * 3.14159265358979323846 * fc / sr;
    const double q = 0.7071067811865476;
    const double alpha = std::sin(w) / (2.0 * q);
    const double c = std::cos(w);
    const double a0 = 1.0 + alpha;
    return {static_cast<float>((1.0 - c) / 2.0 / a0), static_cast<float>((1.0 - c) / a0),
            static_cast<float>((1.0 - c) / 2.0 / a0), static_cast<float>(-2.0 * c / a0),
            static_cast<float>((1.0 - alpha) / a0)};
}

} // namespace

void Engine::process(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                     int numFrames) noexcept {
    ScopedNoDenormals noDenormals;
    if (numFrames <= 0 || outputs == nullptr) return;
    numOutputs = std::max(0, numOutputs);
    numInputs = (inputs == nullptr) ? 0 : std::clamp(numInputs, 0, kMaxInputs);

    if (inputCopy_.empty()) { // not prepared: output silence, never crash
        for (int c = 0; c < numOutputs; ++c)
            if (outputs[c] != nullptr) std::fill(outputs[c], outputs[c] + numFrames, 0.0f);
        return;
    }
    for (int off = 0; off < numFrames;) {
        const int n = std::min(maxBlock_, numFrames - off);
        processChunk(inputs, numInputs, outputs, numOutputs, off, n);
        off += n;
    }
}

void Engine::processChunk(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                          int offset, int n) noexcept {
    const size_t mb = static_cast<size_t>(maxBlock_);
    const float fadeStep = 1.0f / std::max(1.0f, static_cast<float>(0.01 * sampleRate_)); // 10 ms

    // ---- 1. layout handover with fade-out / fade-in ---------------------------
    if (fade_ == Fade::Running && runtime_.hasPending()) fade_ = Fade::Out;
    if ((fade_ == Fade::Out || fade_ == Fade::In) && fadeGain_ <= 0.0f && runtime_.hasPending()) {
        if (runtime_.acquire()) {
            resetSourceState();
            fade_ = Fade::In;
        }
    }
    Runtime* rt = runtime_.current();
    if (rt == nullptr) {
        for (int c = 0; c < numOutputs; ++c)
            if (outputs[c] != nullptr) std::fill(outputs[c] + offset, outputs[c] + offset + n, 0.0f);
        return;
    }
    const int numSpk = rt->numSpeakers;

    // ---- 2. copy and sanitise inputs (they may alias the outputs) -------------
    uint32_t invalid = 0;
    for (int ch = 0; ch < numInputs; ++ch) {
        float* dst = &inputCopy_[static_cast<size_t>(ch) * mb];
        const float* src = inputs[ch];
        if (src == nullptr) {
            std::fill(dst, dst + n, 0.0f);
            continue;
        }
        src += offset;
        for (int i = 0; i < n; ++i) {
            const float v = src[i];
            if (!std::isfinite(v)) { ++invalid; dst[i] = 0.0f; }
            else dst[i] = std::clamp(v, -64.0f, 64.0f);
        }
    }
    if (invalid > 0) invalidInputs_.fetch_add(invalid, std::memory_order_relaxed);

    // ---- 3. reverb parameter update -------------------------------------------
    const uint32_t rv = reverbVersion_.load(std::memory_order_acquire);
    if (rv != reverbSeen_) {
        reverbSeen_ = rv;
        reverb_.setParams(reverb());
    }

    // ---- 4. clear busses --------------------------------------------------------
    for (int m = 0; m < numSpk; ++m) std::fill(mixPtr_[static_cast<size_t>(m)], mixPtr_[static_cast<size_t>(m)] + n, 0.0f);
    std::fill(reverbBus_.begin(), reverbBus_.begin() + n, 0.0f);
    std::fill(subBus_.begin(), subBus_.begin() + n, 0.0f);

    // ---- 5. sources --------------------------------------------------------------
    bool anySolo = false;
    for (const auto& c : sources_)
        if (c.active.load(std::memory_order_relaxed) && c.solo.load(std::memory_order_relaxed)) { anySolo = true; break; }

    const int rampLen = std::max(1, static_cast<int>(0.01 * sampleRate_));
    const bool isDbap = rt->panner.kind() == PannerKind::DBAP;
    const float invN = 1.0f / static_cast<float>(n);
    float* x = srcTmp_.data();

    for (int s = 0; s < kMaxSources; ++s) {
        SourceControl& c = sources_[static_cast<size_t>(s)];
        SourceState& st = state_[static_cast<size_t>(s)];
        const bool active = c.active.load(std::memory_order_relaxed);
        if (!active && !st.hadSignal) continue;
        int ch = c.inputChannel.load(std::memory_order_relaxed);
        if (ch < 0) ch = s;
        const bool hasInput = ch < numInputs;
        const bool on = active && hasInput && !c.mute.load(std::memory_order_relaxed) &&
                        !(anySolo && !c.solo.load(std::memory_order_relaxed));

        // Geometry: only recomputed when the position changed.
        const uint32_t v = c.version.load(std::memory_order_acquire);
        if (v != st.seenVersion) {
            st.seenVersion = v;
            PanInput pin;
            pin.position = c.position();
            pin.spread = safeClamp(c.spread.load(std::memory_order_relaxed), 0.0f, 1.0f, 0.0f);
            rt->panner.compute(pin, st.pan.data());
            const float amt = safeClamp(c.distanceAmount.load(std::memory_order_relaxed), 0.0f, 2.0f, 1.0f);
            const float d = length(pin.position);
            float over;
            float att;
            if (isDbap) {
                over = std::max(0.0f, d - rt->maxRadius);
                att = std::pow(1.0f / (1.0f + over), amt);
            } else {
                over = std::max(0.0f, d - rt->refRadius);
                att = d <= rt->refRadius ? 1.0f : std::pow(rt->refRadius / d, amt);
            }
            st.distanceGain = std::max(att, 1.0e-4f);
            const float fc = std::max(1500.0f, 20000.0f * std::exp(-over * amt / 25.0f));
            st.airCoef = (amt <= 0.0f || over <= 0.0f) ? 1.0f
                                                       : static_cast<float>(1.0 - std::exp(-kTwoPi * fc / sampleRate_));
            // Directivity: how far the source looks away from the listener.
            const float dir = safeClamp(c.directivity.load(std::memory_order_relaxed), 0.0f, 1.0f, 0.0f);
            const float yaw = safeClamp(c.yawDeg.load(std::memory_order_relaxed), -360.0f, 360.0f, 0.0f);
            const float away = dir * 0.5f * (1.0f - std::cos(yaw * kDegToRad)); // 0 facing .. 1 turned away
            st.dirGain = 1.0f - 0.75f * away;
            const float dfc = 20000.0f * std::pow(0.15f, away);
            st.dirCoef = away < 1.0e-3f ? 1.0f : static_cast<float>(1.0 - std::exp(-kTwoPi * std::min(dfc, 0.45f * static_cast<float>(sampleRate_)) / sampleRate_));
            st.dopplerTarget = std::min(d / kSpeedOfSound * static_cast<float>(sampleRate_),
                                        static_cast<float>(dopplerLen_ - maxBlock_ - 4));
        }

        const float srcGain = on ? dbToGain(safeClamp(c.gainDb.load(std::memory_order_relaxed), -80.0f, 12.0f, 0.0f)) : 0.0f;
        const float scalar = srcGain * st.distanceGain * st.dirGain;

        // New target gains -> start a 10 ms linear ramp.
        bool changed = false;
        for (int m = 0; m < numSpk; ++m) {
            const float t = st.pan[static_cast<size_t>(m)] * scalar;
            if (std::fabs(t - st.target[static_cast<size_t>(m)]) > 1.0e-7f) { changed = true; break; }
        }
        if (changed) {
            for (int m = 0; m < numSpk; ++m) {
                const size_t mi = static_cast<size_t>(m);
                st.target[mi] = st.pan[mi] * scalar;
                st.step[mi] = (st.target[mi] - st.cur[mi]) / static_cast<float>(rampLen);
            }
            st.rampLeft = rampLen;
        }

        const float sendTarget = srcGain * dbToGain(safeClamp(c.reverbSendDb.load(std::memory_order_relaxed), -80.0f, 6.0f, -12.0f));
        const float subTarget = srcGain * st.distanceGain;
        bool silent = !on && st.rampLeft == 0 && st.sendCur == 0.0f && st.subCur == 0.0f;
        if (silent) {
            for (int m = 0; m < numSpk && silent; ++m) silent = st.cur[static_cast<size_t>(m)] == 0.0f;
        }
        if (silent) {
            st.hadSignal = false;
            st.airState = 0.0f;
            st.dirState = 0.0f;
            if (st.dopplerSlot >= 0) { dopplerUsed_[static_cast<size_t>(st.dopplerSlot)] = false; st.dopplerSlot = -1; }
            continue;
        }
        st.hadSignal = true;

        // Input with air absorption (one-pole low-pass, bypassed when close).
        const float* in = hasInput ? &inputCopy_[static_cast<size_t>(ch) * mb] : nullptr;
        float peak = 0.0f;
        if (in == nullptr) {
            std::fill(x, x + n, 0.0f);
        } else if (st.airCoef >= 1.0f) {
            for (int i = 0; i < n; ++i) { x[i] = in[i]; peak = std::max(peak, std::fabs(in[i])); }
            st.airState = x[n - 1];
        } else {
            float y = st.airState;
            for (int i = 0; i < n; ++i) {
                y += st.airCoef * (in[i] - y);
                x[i] = y;
                peak = std::max(peak, std::fabs(in[i]));
            }
            st.airState = sanitizeSample(y);
        }
        storeMax(srcPeak_[static_cast<size_t>(s)], peak);

        // Doppler: read the source back from a delay line as long as its distance.
        const bool wantDoppler = c.doppler.load(std::memory_order_relaxed);
        if (!wantDoppler && st.dopplerSlot >= 0) {
            dopplerUsed_[static_cast<size_t>(st.dopplerSlot)] = false;
            st.dopplerSlot = -1;
        }
        if (wantDoppler && st.dopplerSlot < 0) {
            for (int k = 0; k < kDopplerSlots; ++k)
                if (!dopplerUsed_[static_cast<size_t>(k)]) {
                    dopplerUsed_[static_cast<size_t>(k)] = true;
                    st.dopplerSlot = k;
                    float* line = &dopplerLines_[static_cast<size_t>(k) * static_cast<size_t>(dopplerLen_)];
                    std::fill(line, line + dopplerLen_, 0.0f);
                    dopplerPos_[static_cast<size_t>(k)] = 0;
                    st.dopplerCur = st.dopplerTarget; // start in place, no sweep
                    break;
                }
        }
        if (st.dopplerSlot >= 0) {
            const size_t k = static_cast<size_t>(st.dopplerSlot);
            float* line = &dopplerLines_[k * static_cast<size_t>(dopplerLen_)];
            int pos = dopplerPos_[k];
            // At most 10 % pitch change: the delay moves by at most 0.1 samples per sample.
            const float maxMove = 0.1f * static_cast<float>(n);
            const float next = st.dopplerCur + std::clamp(st.dopplerTarget - st.dopplerCur, -maxMove, maxMove);
            const float dstep = (next - st.dopplerCur) * invN;
            float del = st.dopplerCur;
            const float len = static_cast<float>(dopplerLen_);
            for (int i = 0; i < n; ++i) {
                line[pos] = x[i];
                del += dstep;
                float rp = static_cast<float>(pos) - del;
                if (rp < 0.0f) rp += len;
                const int i0 = static_cast<int>(rp);
                const float fr = rp - static_cast<float>(i0);
                const int i1 = i0 + 1 < dopplerLen_ ? i0 + 1 : 0;
                x[i] = line[i0] + fr * (line[i1] - line[i0]);
                pos = pos + 1 < dopplerLen_ ? pos + 1 : 0;
            }
            dopplerPos_[k] = pos;
            st.dopplerCur = next;
        }

        // Directivity colours only the direct sound; the room hears the source from all sides.
        const float* direct = x;
        if (st.dirCoef < 1.0f) {
            float* xd = directTmp_.data();
            float y = st.dirState;
            for (int i = 0; i < n; ++i) {
                y += st.dirCoef * (x[i] - y);
                xd[i] = y;
            }
            st.dirState = sanitizeSample(y);
            direct = xd;
        } else {
            st.dirState = x[n - 1];
        }

        // Pan into the speaker mix.
        const int k = std::min(n, st.rampLeft);
        for (int m = 0; m < numSpk; ++m) {
            const size_t mi = static_cast<size_t>(m);
            float g = st.cur[mi];
            const float t = st.target[mi];
            if (g == 0.0f && t == 0.0f) continue;
            float* out = mixPtr_[mi];
            const float step = st.step[mi];
            int i = 0;
            for (; i < k; ++i) {
                g += step;
                out[i] += direct[i] * g;
            }
            for (; i < n; ++i) out[i] += direct[i] * t;
            st.cur[mi] = (st.rampLeft <= n) ? t : g;
        }
        st.rampLeft = std::max(0, st.rampLeft - n);

        // Reverb send and LFE bus, linearly ramped over the chunk.
        const float sendStep = (sendTarget - st.sendCur) * invN;
        const float subStep = (subTarget - st.subCur) * invN;
        float sg = st.sendCur, bg = st.subCur;
        for (int i = 0; i < n; ++i) {
            sg += sendStep;
            bg += subStep;
            reverbBus_[static_cast<size_t>(i)] += x[i] * sg;
            subBus_[static_cast<size_t>(i)] += x[i] * bg;
        }
        st.sendCur = sendTarget;
        st.subCur = subTarget;
    }

    // ---- 6. reverb into the main speakers ---------------------------------------
    {
        const float wetTarget = dbToGain(safeClamp(reverbWetDb.load(std::memory_order_relaxed), -80.0f, 6.0f, -6.0f));
        const float step = (wetTarget - wetCur_) * invN;
        float w = wetCur_;
        for (int i = 0; i < n; ++i) { w += step; reverbBus_[static_cast<size_t>(i)] *= w; }
        wetCur_ = wetTarget;
        float* mainPtr[kMaxSpeakers];
        int numMain = 0;
        for (int m : rt->mains) mainPtr[numMain++] = mixPtr_[static_cast<size_t>(m)];
        // Scale so that the total reverb energy does not grow with the number of speakers.
        const float norm = numMain > 0 ? 1.0f / std::sqrt(static_cast<float>(numMain)) * 1.4142f : 0.0f;
        if (numMain > 0) reverb_.process(reverbBus_.data(), mainPtr, numMain, n, norm);
    }

    // ---- 7. subwoofers: 4th order Linkwitz-Riley low-pass at 100 Hz --------------
    if (!rt->subs.empty()) {
        const Biquad bq = lowpass(sampleRate_, 100.0);
        for (int stage = 0; stage < 2; ++stage) {
            float z1 = subLp_[stage][0], z2 = subLp_[stage][1];
            for (int i = 0; i < n; ++i) {
                const float in = subBus_[static_cast<size_t>(i)];
                const float out = bq.b0 * in + z1;
                z1 = bq.b1 * in - bq.a1 * out + z2;
                z2 = bq.b2 * in - bq.a2 * out;
                subBus_[static_cast<size_t>(i)] = out;
            }
            subLp_[stage][0] = sanitizeSample(z1);
            subLp_[stage][1] = sanitizeSample(z2);
        }
        const float g = dbToGain(safeClamp(subLevelDb.load(std::memory_order_relaxed), -80.0f, 12.0f, -6.0f)) /
                        std::sqrt(static_cast<float>(rt->subs.size()));
        for (int m : rt->subs) {
            float* out = mixPtr_[static_cast<size_t>(m)];
            for (int i = 0; i < n; ++i) out[i] += subBus_[static_cast<size_t>(i)] * g;
        }
    }

    // ---- 8. speaker test signal, calibration gain and delay -----------------------
    const int test = speakerTest.load(std::memory_order_relaxed);
    if (test >= 0 && test < numSpk) {
        float* buf = mixPtr_[static_cast<size_t>(test)];
        for (int i = 0; i < n; ++i) buf[i] += testNoise_.next();
    }
    for (int m = 0; m < numSpk; ++m) {
        float* buf = mixPtr_[static_cast<size_t>(m)];
        rt->delays[static_cast<size_t>(m)].process(buf, n);
        const float g = rt->speakerGain[static_cast<size_t>(m)];
        if (g != 1.0f)
            for (int i = 0; i < n; ++i) buf[i] *= g;
    }

    // ---- 9. master gain, fade, limiter, sanitise ----------------------------------
    // Per-sample common gain curve (master ramp * layout fade).
    float* curve = srcTmp_.data();
    {
        const float masterTarget = dbToGain(safeClamp(masterGainDb.load(std::memory_order_relaxed), -80.0f, 12.0f, 0.0f));
        const float mStep = (masterTarget - masterCur_) * invN;
        float mg = masterCur_;
        float fg = fadeGain_;
        for (int i = 0; i < n; ++i) {
            mg += mStep;
            if (fade_ == Fade::Out) fg = std::max(0.0f, fg - fadeStep);
            else if (fade_ == Fade::In) fg = std::min(1.0f, fg + fadeStep);
            curve[i] = mg * fg;
        }
        masterCur_ = masterTarget;
        fadeGain_ = fg;
        if (fade_ == Fade::In && fadeGain_ >= 1.0f) fade_ = Fade::Running;
    }

    float* finalPtr[kMaxSpeakers];
    int numFinal;
    if (rt->headphones) {
        float* l = outTmp_.data();
        float* r = outTmp_.data() + mb;
        rt->fold.process(mixPtr_.data(), numSpk, l, r, n);
        finalPtr[0] = l;
        finalPtr[1] = r;
        numFinal = 2;
    } else {
        for (int m = 0; m < numSpk; ++m) finalPtr[m] = mixPtr_[static_cast<size_t>(m)];
        numFinal = numSpk;
    }

    const bool limit = limiterEnabled.load(std::memory_order_relaxed);
    const float ceilingDb = limiterCeilingDb.load(std::memory_order_relaxed);
    const float releaseMs = limiterReleaseMs.load(std::memory_order_relaxed);
    bool limited = false;
    for (int c = 0; c < numFinal; ++c) {
        float* buf = finalPtr[c];
        for (int i = 0; i < n; ++i) buf[i] *= curve[i];
        float peak = 0.0f;
        auto& lim = limiter_[static_cast<size_t>(c)];
        lim.setCeilingDb(ceilingDb);
        lim.setRelease(releaseMs);
        limited |= lim.process(buf, n, limit, peak);
        storeMax(outPeak_[static_cast<size_t>(c)], peak);
        storeMax(limiterGr_[static_cast<size_t>(c)], 1.0f - lim.minGain());
    }
    if (limited) limiterEvents_.fetch_add(1, std::memory_order_relaxed);

    // ---- 10. write outputs ---------------------------------------------------------
    for (int c = 0; c < numOutputs; ++c) {
        float* out = outputs[c];
        if (out == nullptr) continue;
        out += offset;
        if (c < numFinal) std::memcpy(out, finalPtr[c], sizeof(float) * static_cast<size_t>(n));
        else std::fill(out, out + n, 0.0f);
    }
}

} // namespace rk
