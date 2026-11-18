#include "rk/bridge.hpp"

#include "rk/engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>
#include <type_traits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace rk::bridge {

static_assert(std::atomic<uint64_t>::is_always_lock_free, "64-bit atomics must be lock-free");
static_assert(std::atomic<int64_t>::is_always_lock_free);
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(sizeof(std::atomic<float>) == sizeof(float));
static_assert((kRingFrames & (kRingFrames - 1)) == 0, "ring size must be a power of two");

namespace {
constexpr int kNumParams = 8;
constexpr uint64_t kRingMask = kRingFrames - 1;
} // namespace

// Lives in shared memory. Only lock-free atomics, so it is valid when zero-filled
// and can be accessed by several processes at once.
struct Slot {
    std::atomic<uint64_t> owner;       // 0 = free, otherwise the claiming plugin's token
    std::atomic<int64_t> heartbeatMs;  // last sign of life of the owner
    std::atomic<uint32_t> channels;    // 1 or 2
    std::atomic<uint32_t> sampleRate;  // Hz
    std::atomic<uint32_t> blockSize;   // largest block the DAW sent recently
    std::atomic<uint32_t> nameSeq;     // seqlock for name: odd while writing
    std::atomic<uint32_t> name[kNameWords];
    std::atomic<float> params[kNumParams];
    std::atomic<uint64_t> writePos;    // frames written since the slot was claimed
    std::atomic<int64_t> lastWriteMs;
    std::atomic<float> audio[kMaxChannels][kRingFrames];
};

struct Shared {
    std::atomic<uint32_t> magic;
    std::atomic<uint32_t> version;
    std::atomic<int64_t> appHeartbeatMs;
    std::atomic<uint32_t> appSampleRate;
    std::atomic<uint32_t> reserved;
    Slot slots[kSlots];
};

static_assert(std::is_standard_layout_v<Slot>);
static_assert(std::is_standard_layout_v<Shared>);

const char* defaultName() {
#if defined(_WIN32)
    return "Local\\RaumklangBridgeV1";
#else
    return "/raumklang-bridge-v1";
#endif
}

int64_t nowMs() {
    // steady_clock is system-wide on Windows (QPC), macOS (mach time) and Linux (CLOCK_MONOTONIC).
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

size_t sharedSize() { return sizeof(Shared); }

// ---- parameters ---------------------------------------------------------------

TrackParams TrackParams::sanitized() const noexcept {
    TrackParams d;
    TrackParams r;
    r.azimuthDeg = wrapDegrees(safeClamp(azimuthDeg, -1.0e6f, 1.0e6f, d.azimuthDeg));
    r.elevationDeg = safeClamp(elevationDeg, -90.0f, 90.0f, d.elevationDeg);
    r.distance = safeClamp(distance, 0.0f, 100.0f, d.distance);
    r.spread = safeClamp(spread, 0.0f, 1.0f, d.spread);
    r.gainDb = safeClamp(gainDb, -80.0f, 12.0f, d.gainDb);
    r.reverbSendDb = safeClamp(reverbSendDb, -80.0f, 6.0f, d.reverbSendDb);
    r.stereoWidthDeg = safeClamp(stereoWidthDeg, 0.0f, 180.0f, d.stereoWidthDeg);
    r.distanceAmount = safeClamp(distanceAmount, 0.0f, 2.0f, d.distanceAmount);
    return r;
}

void applyTrack(Engine& engine, int first, int inputBase, int channels, const TrackParams& raw, bool active) noexcept {
    const TrackParams p = raw.sanitized();
    channels = std::clamp(channels, 1, 2);
    const float half = channels == 2 ? p.stereoWidthDeg * 0.5f : 0.0f;
    for (int ch = 0; ch < 2; ++ch) {
        if (first + ch < 0 || first + ch >= kMaxSources) continue;
        auto& s = engine.source(first + ch);
        const bool on = active && ch < channels;
        if (!on) {
            if (s.active.load(std::memory_order_relaxed)) {
                s.active = false;
                s.touch();
            }
            continue;
        }
        s.active = true;
        s.inputChannel = inputBase + ch;
        s.gainDb = p.gainDb + (channels == 2 ? -3.0f : 0.0f);
        s.spread = p.spread;
        s.reverbSendDb = p.reverbSendDb;
        s.distanceAmount = p.distanceAmount;
        const float a = p.azimuthDeg + (ch == 0 ? half : -half);
        const Vec3 pos = toCartesian({wrapDegrees(a), p.elevationDeg, p.distance});
        if (pos.x != s.x.load() || pos.y != s.y.load() || pos.z != s.z.load()) s.setPosition(pos);
        else s.touch();
    }
}

// ---- shared memory ------------------------------------------------------------

SharedMemory::~SharedMemory() { close(); }

#if defined(_WIN32)
bool SharedMemory::open(const std::string& name, size_t size) {
    close();
    std::wstring wname(name.begin(), name.end());
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                  static_cast<DWORD>((uint64_t)size >> 32), static_cast<DWORD>(size & 0xffffffffu),
                                  wname.c_str());
    if (h == nullptr) return false;
    void* p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (p == nullptr) {
        CloseHandle(h);
        return false;
    }
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(p, &info, sizeof(info)) == 0 || info.RegionSize < size) {
        UnmapViewOfFile(p);
        CloseHandle(h);
        return false;
    }
    handle_ = h;
    data_ = p;
    size_ = size;
    return true;
}

void SharedMemory::close() {
    if (data_ != nullptr) UnmapViewOfFile(data_);
    if (handle_ != nullptr) CloseHandle(static_cast<HANDLE>(handle_));
    data_ = nullptr;
    handle_ = nullptr;
    size_ = 0;
}

void SharedMemory::unlink(const std::string&) {}
#else
bool SharedMemory::open(const std::string& name, size_t size) {
    close();
    const int fd = shm_open(name.c_str(), O_RDWR | O_CREAT, 0600);
    if (fd < 0) return false;
    struct stat st {};
    if (fstat(fd, &st) != 0) {
        ::close(fd);
        return false;
    }
    // macOS only allows setting the size once, so only the creator does it.
    if (st.st_size == 0 && ftruncate(fd, static_cast<off_t>(size)) != 0) {
        ::close(fd);
        return false;
    }
    if (fstat(fd, &st) != 0 || static_cast<size_t>(st.st_size) < size) {
        ::close(fd);
        return false;
    }
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    if (p == MAP_FAILED) return false;
    data_ = p;
    size_ = size;
    return true;
}

void SharedMemory::close() {
    if (data_ != nullptr) munmap(data_, size_);
    data_ = nullptr;
    size_ = 0;
}

void SharedMemory::unlink(const std::string& name) { shm_unlink(name.c_str()); }
#endif

namespace {

Shared* attach(SharedMemory& mem, const std::string& name) {
    if (!mem.open(name, sizeof(Shared))) return nullptr;
    auto* s = static_cast<Shared*>(mem.data());
    uint32_t expected = 0;
    if (s->magic.compare_exchange_strong(expected, kMagic)) {
        s->version.store(kVersion);
    } else if (expected != kMagic) {
        mem.close();
        return nullptr;
    }
    return s;
}

uint64_t randomToken() {
    std::random_device rd;
    uint64_t t = 0;
    while (t == 0) t = (uint64_t(rd()) << 32) ^ uint64_t(rd()) ^ uint64_t(nowMs());
    return t;
}

void writeName(Slot& s, const std::string& name) {
    const uint32_t seq = s.nameSeq.load(std::memory_order_relaxed);
    s.nameSeq.store(seq | 1u, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    char buf[kNameWords * 4] = {};
    std::memcpy(buf, name.data(), std::min(name.size(), sizeof(buf) - 1));
    for (int i = 0; i < kNameWords; ++i) {
        uint32_t w = 0;
        std::memcpy(&w, buf + i * 4, 4);
        s.name[i].store(w, std::memory_order_relaxed);
    }
    s.nameSeq.store((seq | 1u) + 1u, std::memory_order_release);
}

std::string readName(const Slot& s) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        const uint32_t a = s.nameSeq.load(std::memory_order_acquire);
        if (a & 1u) continue;
        char buf[kNameWords * 4 + 1] = {};
        for (int i = 0; i < kNameWords; ++i) {
            const uint32_t w = s.name[i].load(std::memory_order_relaxed);
            std::memcpy(buf + i * 4, &w, 4);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        if (s.nameSeq.load(std::memory_order_relaxed) != a) continue;
        buf[kNameWords * 4] = 0;
        std::string r(buf);
        // Drop control characters; the UI shows whatever remains.
        r.erase(std::remove_if(r.begin(), r.end(), [](char c) { return (unsigned char)c < 0x20; }), r.end());
        return r;
    }
    return {};
}

TrackParams loadParams(const Slot& s) noexcept {
    TrackParams p;
    p.azimuthDeg = s.params[0].load(std::memory_order_relaxed);
    p.elevationDeg = s.params[1].load(std::memory_order_relaxed);
    p.distance = s.params[2].load(std::memory_order_relaxed);
    p.spread = s.params[3].load(std::memory_order_relaxed);
    p.gainDb = s.params[4].load(std::memory_order_relaxed);
    p.reverbSendDb = s.params[5].load(std::memory_order_relaxed);
    p.stereoWidthDeg = s.params[6].load(std::memory_order_relaxed);
    p.distanceAmount = s.params[7].load(std::memory_order_relaxed);
    return p.sanitized();
}

} // namespace

// ---- sender -------------------------------------------------------------------

Sender::Sender() = default;
Sender::~Sender() { release(); }

bool Sender::open(const std::string& name) {
    if (shared_ != nullptr) return true;
    shared_ = attach(mem_, name);
    return shared_ != nullptr;
}

int Sender::claim() {
    if (shared_ == nullptr) return -1;
    if (slot() >= 0) return slot();
    const uint64_t token = randomToken();
    const int64_t now = nowMs();
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < kSlots; ++i) {
            Slot& s = shared_->slots[i];
            uint64_t cur = s.owner.load();
            const bool free = cur == 0;
            const bool abandoned = !free && now - s.heartbeatMs.load() > kStaleMs;
            if (pass == 0 ? !free : !abandoned) continue;
            if (!s.owner.compare_exchange_strong(cur, token)) continue;
            s.heartbeatMs.store(now);
            s.writePos.store(0);
            s.lastWriteMs.store(0);
            s.channels.store(1);
            token_ = token;
            slot_.store(i, std::memory_order_release);
            return i;
        }
    }
    return -1;
}

void Sender::release() {
    const int i = slot_.exchange(-1);
    if (shared_ == nullptr || i < 0) return;
    uint64_t t = token_;
    shared_->slots[i].owner.compare_exchange_strong(t, 0);
    token_ = 0;
}

void Sender::heartbeat(const std::string& trackName) {
    const int i = slot();
    if (shared_ == nullptr || i < 0) return;
    Slot& s = shared_->slots[i];
    if (s.owner.load() != token_) { // somebody took the slot over (we were stalled for long)
        slot_.store(-1);
        token_ = 0;
        claim();
        return;
    }
    s.heartbeatMs.store(nowMs());
    writeName(s, trackName);
}

bool Sender::appRunning() const noexcept {
    if (shared_ == nullptr) return false;
    return nowMs() - shared_->appHeartbeatMs.load(std::memory_order_relaxed) < kStaleMs;
}

double Sender::appSampleRate() const noexcept {
    return shared_ != nullptr ? (double)shared_->appSampleRate.load(std::memory_order_relaxed) : 0.0;
}

void Sender::write(const float* const* channels, int numChannels, int numFrames, double sampleRate,
                   const TrackParams& params) noexcept {
    const int i = slot_.load(std::memory_order_acquire);
    if (shared_ == nullptr || i < 0 || channels == nullptr || numFrames <= 0) return;
    Slot& s = shared_->slots[i];
    numChannels = std::clamp(numChannels, 1, kMaxChannels);
    s.channels.store((uint32_t)numChannels, std::memory_order_relaxed);
    s.sampleRate.store((uint32_t)std::clamp(sampleRate, 0.0, 1.0e6), std::memory_order_relaxed);
    const uint32_t prevBlock = s.blockSize.load(std::memory_order_relaxed);
    s.blockSize.store((uint32_t)std::max<int>(numFrames, (int)(prevBlock * 0.999)), std::memory_order_relaxed);
    const TrackParams p = params.sanitized();
    const float vals[kNumParams] = {p.azimuthDeg, p.elevationDeg, p.distance, p.spread,
                                    p.gainDb, p.reverbSendDb, p.stereoWidthDeg, p.distanceAmount};
    for (int k = 0; k < kNumParams; ++k) s.params[k].store(vals[k], std::memory_order_relaxed);

    uint64_t w = s.writePos.load(std::memory_order_relaxed);
    int done = 0;
    while (done < numFrames) {
        // Never write more than half the ring at once, so the reader always has room.
        const int n = std::min(numFrames - done, kRingFrames / 2);
        for (int ch = 0; ch < numChannels; ++ch) {
            const float* src = channels[ch] != nullptr ? channels[ch] + done : nullptr;
            auto* dst = s.audio[ch];
            for (int k = 0; k < n; ++k)
                dst[(w + (uint64_t)k) & kRingMask].store(src != nullptr ? sanitizeSample(src[k]) : 0.0f,
                                                        std::memory_order_relaxed);
        }
        w += (uint64_t)n;
        s.writePos.store(w, std::memory_order_release);
        done += n;
    }
    s.lastWriteMs.store(nowMs(), std::memory_order_relaxed);
}

// ---- receiver -----------------------------------------------------------------

Receiver::Receiver() = default;
Receiver::~Receiver() = default;

bool Receiver::open(const std::string& name) {
    if (shared_ != nullptr) return true;
    shared_ = attach(mem_, name);
    return shared_ != nullptr;
}

void Receiver::heartbeat(double sampleRate) {
    if (shared_ == nullptr) return;
    shared_->appSampleRate.store((uint32_t)std::clamp(sampleRate, 0.0, 1.0e6));
    shared_->appHeartbeatMs.store(nowMs());
}

std::vector<TrackInfo> Receiver::scan() const {
    std::vector<TrackInfo> out;
    if (shared_ == nullptr) return out;
    const int64_t now = nowMs();
    const uint32_t appRate = shared_->appSampleRate.load();
    for (int i = 0; i < kSlots; ++i) {
        const Slot& s = shared_->slots[i];
        if (s.owner.load() == 0 || now - s.heartbeatMs.load() > kStaleMs) continue;
        TrackInfo t;
        t.slot = i;
        t.name = readName(s);
        t.channels = std::clamp((int)s.channels.load(), 1, kMaxChannels);
        t.params = loadParams(s);
        t.sending = now - s.lastWriteMs.load() < 500;
        t.sampleRate = s.sampleRate.load();
        t.sampleRateMismatch = t.sending && appRate != 0 && s.sampleRate.load() != appRate;
        out.push_back(std::move(t));
    }
    return out;
}

void Receiver::beginBlock(double sampleRate) noexcept {
    if (sampleRate != sampleRate_) {
        sampleRate_ = sampleRate;
        for (auto& st : state_) st.running = false;
    }
}

int Receiver::read(int slot, float* const* out, int numFrames, TrackParams& params) noexcept {
    auto silence = [&] {
        for (int ch = 0; ch < kMaxChannels; ++ch) std::fill(out[ch], out[ch] + std::max(0, numFrames), 0.0f);
    };
    if (shared_ == nullptr || slot < 0 || slot >= kSlots || numFrames <= 0) {
        silence();
        return 0;
    }
    Slot& s = shared_->slots[slot];
    ReadState& st = state_[(size_t)slot];
    const uint64_t owner = s.owner.load(std::memory_order_acquire);
    const uint32_t rate = s.sampleRate.load(std::memory_order_relaxed);
    if (owner == 0 || owner != st.owner) {
        st = ReadState{};
        st.owner = owner;
    }
    // Different sample rates would play at the wrong pitch: stay silent, the UI says why.
    if (owner == 0 || rate == 0 || std::abs((double)rate - sampleRate_) > 0.5) {
        st.running = false;
        silence();
        return 0;
    }
    params = loadParams(s);
    const int channels = std::clamp((int)s.channels.load(std::memory_order_relaxed), 1, kMaxChannels);
    const uint64_t w = s.writePos.load(std::memory_order_acquire);
    const int block = std::clamp((int)s.blockSize.load(std::memory_order_relaxed), 1, kRingFrames / 4);
    const double target = std::clamp(2.0 * block + numFrames + 64.0, 256.0, kRingFrames / 2.0);

    if (!st.running) {
        if ((double)w < target) {
            silence();
            return 0;
        }
        st.running = true;
        st.readPos = (double)w - target;
        st.smoothedError = 0.0;
        st.fade = 0.0f;
    }
    double fill = (double)w - st.readPos;
    // The writer jumped (new owner, stall, garbage): start again at the target distance.
    if (fill < 0.0 || fill > kRingFrames - (double)block - 2.0) {
        dropouts_.fetch_add(1, std::memory_order_relaxed);
        st.readPos = (double)w - target;
        st.smoothedError = 0.0;
        fill = target;
    }
    // Slow drift correction: at most 0.5 % faster or slower, smoothed over many blocks.
    const double error = fill - target;
    st.smoothedError += (error - st.smoothedError) * std::min(1.0, numFrames / (0.5 * sampleRate_));
    const double ratio = 1.0 + std::clamp(st.smoothedError * 2.0e-5, -0.005, 0.005);
    const double needed = numFrames * ratio + 2.0;
    if (fill < needed) {
        // Ran dry (DAW stopped calling the plugin or stalled): silence until data is back.
        if (w != st.lastWrite) dropouts_.fetch_add(1, std::memory_order_relaxed);
        st.lastWrite = w;
        st.running = false;
        silence();
        return 0;
    }
    st.lastWrite = w;

    const float fadeStep = 1.0f / std::max(1.0f, 0.005f * (float)sampleRate_);
    for (int ch = 0; ch < kMaxChannels; ++ch) {
        float* dst = out[ch];
        if (ch >= channels) {
            std::fill(dst, dst + numFrames, 0.0f);
            continue;
        }
        const auto* ring = s.audio[ch];
        double pos = st.readPos;
        float fade = st.fade;
        for (int k = 0; k < numFrames; ++k) {
            const uint64_t i0 = (uint64_t)pos;
            const float frac = (float)(pos - (double)i0);
            const float a = ring[i0 & kRingMask].load(std::memory_order_relaxed);
            const float b = ring[(i0 + 1) & kRingMask].load(std::memory_order_relaxed);
            fade = std::min(1.0f, fade + fadeStep);
            dst[k] = sanitizeSample(a + (b - a) * frac) * fade;
            pos += ratio;
        }
        if (ch == channels - 1) st.fade = fade;
    }
    st.readPos += numFrames * ratio;
    return channels;
}

} // namespace rk::bridge
