#include "rk/speaker_processor.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rk {

struct SpeakerProcessor::Runtime {
    int numSpeakers = 0;
    std::array<float, kMaxSpeakers> gain{};
    std::vector<DelayLine> delays;
    bool headphones = false;
    BinauralFold fold;
};

SpeakerProcessor::SpeakerProcessor() {
    for (auto& p : peaks_) p.store(0.0f);
    layout_ = presets::stereo();
}

SpeakerProcessor::~SpeakerProcessor() = default;

std::unique_ptr<SpeakerProcessor::Runtime> SpeakerProcessor::build(const SpeakerLayout& layout, bool headphones) const {
    auto rt = std::make_unique<Runtime>();
    rt->numSpeakers = std::min(static_cast<int>(layout.speakers.size()), kMaxSpeakers);
    rt->delays.resize(static_cast<size_t>(rt->numSpeakers));
    for (int m = 0; m < rt->numSpeakers; ++m) {
        const Speaker& s = layout.speakers[static_cast<size_t>(m)];
        rt->gain[static_cast<size_t>(m)] = s.enabled ? dbToGain(s.gainDb) : 0.0f;
        rt->delays[static_cast<size_t>(m)].prepare(static_cast<int>(std::lround(s.delayMs * 0.001 * sampleRate_)));
    }
    rt->headphones = headphones;
    if (headphones) rt->fold.prepare(layout, sampleRate_);
    return rt;
}

void SpeakerProcessor::prepare(double sampleRate, int maxBlockSize) {
    std::lock_guard<std::mutex> lock(mutex_);
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 768000.0) ? sampleRate : 48000.0;
    maxBlock_ = std::clamp(maxBlockSize, 16, 8192);
    scratch_.assign(static_cast<size_t>(kMaxSpeakers + 2) * static_cast<size_t>(maxBlock_), 0.0f);
    for (auto& l : limiter_) l.prepare(sampleRate_);
    masterCur_ = dbToGain(masterGainDb.load());
    fade_ = 0.0f;
    fadingOut_ = false;
    runtime_.resetUnsafe(build(layout_, headphones_));
}

void SpeakerProcessor::setLayout(SpeakerLayout layout, bool headphones) {
    sanitize(layout);
    std::lock_guard<std::mutex> lock(mutex_);
    layout_ = std::move(layout);
    headphones_ = headphones;
    if (scratch_.empty()) return;
    runtime_.publish(build(layout_, headphones_));
}

void SpeakerProcessor::collectGarbage() {
    std::lock_guard<std::mutex> lock(mutex_);
    runtime_.collectGarbage();
}

float SpeakerProcessor::takePeak(int c) noexcept {
    if (c < 0 || c >= kMaxSpeakers) return 0.0f;
    return peaks_[static_cast<size_t>(c)].exchange(0.0f, std::memory_order_relaxed);
}

void SpeakerProcessor::process(float* const* io, int numChannels, int numFrames) noexcept {
    ScopedNoDenormals noDenormals;
    if (io == nullptr || numFrames <= 0 || numChannels <= 0) return;
    numChannels = std::min(numChannels, kMaxSpeakers);
    if (scratch_.empty()) {
        for (int c = 0; c < numChannels; ++c)
            if (io[c] != nullptr) std::fill(io[c], io[c] + numFrames, 0.0f);
        return;
    }
    for (int off = 0; off < numFrames;) {
        const int n = std::min(maxBlock_, numFrames - off);
        processChunk(io, numChannels, off, n);
        off += n;
    }
}

void SpeakerProcessor::processChunk(float* const* io, int numChannels, int offset, int n) noexcept {
    const float fadeStep = 1.0f / std::max(1.0f, static_cast<float>(0.01 * sampleRate_));
    if (!fadingOut_ && runtime_.hasPending()) fadingOut_ = true;
    if (fadingOut_ && fade_ <= 0.0f && runtime_.acquire()) {
        fadingOut_ = false;
        for (auto& l : limiter_) l.reset();
    }
    Runtime* rt = runtime_.current();
    const size_t mb = static_cast<size_t>(maxBlock_);
    if (rt == nullptr) {
        for (int c = 0; c < numChannels; ++c)
            if (io[c] != nullptr) std::fill(io[c] + offset, io[c] + offset + n, 0.0f);
        return;
    }

    // Copy the speaker feeds into scratch (missing channels count as silence).
    const int numSpk = rt->numSpeakers;
    float* spk[kMaxSpeakers];
    for (int m = 0; m < numSpk; ++m) {
        spk[m] = &scratch_[static_cast<size_t>(m) * mb];
        if (m < numChannels && io[m] != nullptr) {
            for (int i = 0; i < n; ++i) spk[m][i] = sanitizeSample(io[m][offset + i]);
        } else {
            std::fill(spk[m], spk[m] + n, 0.0f);
        }
    }

    const int test = speakerTest.load(std::memory_order_relaxed);
    if (test >= 0 && test < numSpk)
        for (int i = 0; i < n; ++i) spk[test][i] += noise_.next();

    for (int m = 0; m < numSpk; ++m) {
        rt->delays[static_cast<size_t>(m)].process(spk[m], n);
        const float g = rt->gain[static_cast<size_t>(m)];
        if (g != 1.0f)
            for (int i = 0; i < n; ++i) spk[m][i] *= g;
    }

    float* finalPtr[kMaxSpeakers];
    int numFinal = numSpk;
    if (rt->headphones) {
        float* l = &scratch_[static_cast<size_t>(kMaxSpeakers) * mb];
        float* r = l + mb;
        rt->fold.process(spk, numSpk, l, r, n);
        finalPtr[0] = l;
        finalPtr[1] = r;
        numFinal = 2;
    } else {
        for (int m = 0; m < numSpk; ++m) finalPtr[m] = spk[m];
    }

    const float target = dbToGain(safeClamp(masterGainDb.load(std::memory_order_relaxed), -80.0f, 12.0f, 0.0f));
    const float mStep = (target - masterCur_) / static_cast<float>(n);
    const bool limit = limiterEnabled.load(std::memory_order_relaxed);
    bool limited = false;
    float fadeEnd = fade_;
    for (int c = 0; c < numFinal; ++c) {
        float mg = masterCur_, fg = fade_;
        float* buf = finalPtr[c];
        for (int i = 0; i < n; ++i) {
            mg += mStep;
            fg = fadingOut_ ? std::max(0.0f, fg - fadeStep) : std::min(1.0f, fg + fadeStep);
            buf[i] *= mg * fg;
        }
        fadeEnd = fg;
        float peak = 0.0f;
        limited |= limiter_[static_cast<size_t>(c)].process(buf, n, limit, peak);
        const float prev = peaks_[static_cast<size_t>(c)].load(std::memory_order_relaxed);
        if (peak > prev) peaks_[static_cast<size_t>(c)].store(peak, std::memory_order_relaxed);
    }
    if (numFinal == 0) {
        fadeEnd = fadingOut_ ? std::max(0.0f, fade_ - fadeStep * static_cast<float>(n))
                             : std::min(1.0f, fade_ + fadeStep * static_cast<float>(n));
    }
    masterCur_ = target;
    fade_ = fadeEnd;
    if (limited) limiterEvents_.fetch_add(1, std::memory_order_relaxed);

    for (int c = 0; c < numChannels; ++c) {
        if (io[c] == nullptr) continue;
        if (c < numFinal) std::memcpy(io[c] + offset, finalPtr[c], sizeof(float) * static_cast<size_t>(n));
        else std::fill(io[c] + offset, io[c] + offset + n, 0.0f);
    }
}

} // namespace rk
