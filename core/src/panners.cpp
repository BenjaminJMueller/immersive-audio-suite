#include "rk/panners.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rk {
namespace {

constexpr float kEps = 1.0e-5f;

void normaliseEnergy(float* g, int n) noexcept {
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) sum += g[i] * g[i];
    if (!(sum > 1.0e-12f) || !std::isfinite(sum)) {
        for (int i = 0; i < n; ++i) g[i] = 0.0f;
        return;
    }
    const float s = 1.0f / std::sqrt(sum);
    for (int i = 0; i < n; ++i) g[i] *= s;
}

bool isMain(const Speaker& s) { return s.enabled && !s.isSubwoofer; }

// ---------------------------------------------------------------------------
// Incremental 3D convex hull for up to a few hundred points.
// Returns false if the points are (nearly) coplanar.
// ---------------------------------------------------------------------------
struct Face {
    int a, b, c;
    Vec3 n;
    float d;
    bool alive;
};

Face makeFace(const std::vector<Vec3>& p, int a, int b, int c, Vec3 interior) {
    Face f{a, b, c, {}, 0.0f, true};
    f.n = normalized(cross(p[static_cast<size_t>(b)] - p[static_cast<size_t>(a)], p[static_cast<size_t>(c)] - p[static_cast<size_t>(a)]), {0, 0, 1});
    f.d = dot(f.n, p[static_cast<size_t>(a)]);
    if (dot(f.n, interior) - f.d > 0.0f) {
        std::swap(f.b, f.c);
        f.n = -f.n;
        f.d = -f.d;
    }
    return f;
}

bool convexHull(const std::vector<Vec3>& p, std::vector<std::array<int, 3>>& out) {
    out.clear();
    const int n = static_cast<int>(p.size());
    if (n < 4) return false;
    auto P = [&p](int i) { return p[static_cast<size_t>(i)]; };

    int i0 = 0, i1 = -1, i2 = -1, i3 = -1;
    float best = 0.0f;
    for (int i = 1; i < n; ++i) {
        const float d = length(P(i) - P(i0));
        if (d > best) { best = d; i1 = i; }
    }
    if (i1 < 0 || best < 1.0e-4f) return false;
    best = 0.0f;
    const Vec3 lineDir = normalized(P(i1) - P(i0));
    for (int i = 0; i < n; ++i) {
        const Vec3 v = P(i) - P(i0);
        const float d = length(v - lineDir * dot(v, lineDir));
        if (d > best) { best = d; i2 = i; }
    }
    if (i2 < 0 || best < 1.0e-4f) return false;
    best = 0.0f;
    const Vec3 planeN = normalized(cross(P(i1) - P(i0), P(i2) - P(i0)));
    for (int i = 0; i < n; ++i) {
        const float d = std::fabs(dot(P(i) - P(i0), planeN));
        if (d > best) { best = d; i3 = i; }
    }
    if (i3 < 0 || best < 1.0e-3f) return false;

    const Vec3 interior = (P(i0) + P(i1) + P(i2) + P(i3)) * 0.25f;
    std::vector<Face> faces;
    faces.reserve(static_cast<size_t>(8 * n));
    faces.push_back(makeFace(p, i0, i1, i2, interior));
    faces.push_back(makeFace(p, i0, i1, i3, interior));
    faces.push_back(makeFace(p, i0, i2, i3, interior));
    faces.push_back(makeFace(p, i1, i2, i3, interior));

    std::vector<std::pair<int, int>> edges;
    std::vector<size_t> visible;
    for (int i = 0; i < n; ++i) {
        if (i == i0 || i == i1 || i == i2 || i == i3) continue;
        visible.clear();
        for (size_t f = 0; f < faces.size(); ++f)
            if (faces[f].alive && dot(faces[f].n, P(i)) - faces[f].d > 1.0e-6f) visible.push_back(f);
        if (visible.empty()) continue;
        edges.clear();
        for (size_t f : visible) {
            edges.emplace_back(faces[f].a, faces[f].b);
            edges.emplace_back(faces[f].b, faces[f].c);
            edges.emplace_back(faces[f].c, faces[f].a);
            faces[f].alive = false;
        }
        for (const auto& e : edges) {
            const bool shared = std::find(edges.begin(), edges.end(), std::make_pair(e.second, e.first)) != edges.end();
            if (!shared) faces.push_back(makeFace(p, e.first, e.second, i, interior));
        }
    }
    for (const auto& f : faces)
        if (f.alive) out.push_back({f.a, f.b, f.c});
    return out.size() >= 4;
}

bool invert3(Vec3 a, Vec3 b, Vec3 c, float* inv) {
    const float det = dot(a, cross(b, c));
    if (std::fabs(det) < 1.0e-6f) return false;
    const Vec3 r0 = cross(b, c) * (1.0f / det);
    const Vec3 r1 = cross(c, a) * (1.0f / det);
    const Vec3 r2 = cross(a, b) * (1.0f / det);
    const float m[9] = {r0.x, r0.y, r0.z, r1.x, r1.y, r1.z, r2.x, r2.y, r2.z};
    std::copy(m, m + 9, inv);
    return true;
}

} // namespace

// ===========================================================================
// VBAP
// ===========================================================================

void VbapPanner::prepare(const SpeakerLayout& layout) {
    numSpeakers_ = std::min(static_cast<int>(layout.speakers.size()), kMaxSpeakers);
    dirs_.clear();
    owners_.clear();
    pairs_.clear();
    tris_.clear();
    azimuths_.clear();
    numReal_ = 0;

    const float sameDir = std::cos(1.0f * kDegToRad);
    for (int i = 0; i < numSpeakers_; ++i) {
        const Speaker& s = layout.speakers[static_cast<size_t>(i)];
        if (!isMain(s) || length(s.position) < 1.0e-3f) continue;
        const Vec3 d = normalized(s.position);
        bool merged = false;
        for (size_t k = 0; k < dirs_.size(); ++k) {
            if (dot(dirs_[k], d) > sameDir) {
                owners_[k].push_back(i);
                merged = true;
                break;
            }
        }
        if (!merged) {
            dirs_.push_back(d);
            owners_.push_back({i});
        }
    }
    numReal_ = static_cast<int>(dirs_.size());
    if (numReal_ == 0) return;

    float minEl = 90.0f, maxEl = -90.0f;
    for (const auto& d : dirs_) {
        const float el = std::asin(std::clamp(d.z, -1.0f, 1.0f)) * kRadToDeg;
        minEl = std::min(minEl, el);
        maxEl = std::max(maxEl, el);
    }
    flat_ = numReal_ < 4 || (maxEl - minEl) < 10.0f;
    if (!flat_) {
        build3D();
        if (tris_.empty()) {
            flat_ = true;
            dirs_.resize(static_cast<size_t>(numReal_));
        }
    }
    if (flat_) build2D();
}

void VbapPanner::build2D() {
    azimuths_.resize(static_cast<size_t>(numReal_));
    std::vector<int> order(static_cast<size_t>(numReal_));
    for (int i = 0; i < numReal_; ++i) {
        const Vec3 d = dirs_[static_cast<size_t>(i)];
        azimuths_[static_cast<size_t>(i)] = std::atan2(-d.x, d.y);
        order[static_cast<size_t>(i)] = i;
    }
    std::sort(order.begin(), order.end(), [this](int a, int b) { return azimuths_[static_cast<size_t>(a)] < azimuths_[static_cast<size_t>(b)]; });
    if (numReal_ < 2) return;
    for (int k = 0; k < numReal_; ++k) {
        const int a = order[static_cast<size_t>(k)];
        const int b = order[static_cast<size_t>((k + 1) % numReal_)];
        if (numReal_ == 2 && k == 1) break; // only one pair for two speakers
        float gap = azimuths_[static_cast<size_t>(b)] - azimuths_[static_cast<size_t>(a)];
        if (gap <= 0.0f) gap += kTwoPi;
        if (gap >= kPi - 1.0e-3f) {
            if (numReal_ == 2) {
                // Two speakers: use the shorter arc, whichever it is.
                if (kTwoPi - gap >= kPi - 1.0e-3f) continue;
            } else {
                continue; // gap of 180° or more: no phantom source in between
            }
        }
        const Vec3 da = dirs_[static_cast<size_t>(a)], db = dirs_[static_cast<size_t>(b)];
        const float ax = da.x, ay = da.y, bx = db.x, by = db.y;
        const float la = std::sqrt(ax * ax + ay * ay), lb = std::sqrt(bx * bx + by * by);
        if (la < 1.0e-4f || lb < 1.0e-4f) continue;
        const float uax = ax / la, uay = ay / la, ubx = bx / lb, uby = by / lb;
        const float det = uax * uby - ubx * uay;
        if (std::fabs(det) < 1.0e-5f) continue;
        Pair p{a, b, {uby / det, -ubx / det, -uay / det, uax / det}};
        pairs_.push_back(p);
    }
}

void VbapPanner::build3D() {
    std::vector<Vec3> pts(dirs_.begin(), dirs_.begin() + numReal_);
    std::vector<std::array<int, 3>> hull;

    // Add imaginary speakers until the listener is well enclosed by the hull.
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (!convexHull(pts, hull)) { tris_.clear(); return; }
        Vec3 worstNormal{};
        float worst = 1.0f;
        for (const auto& t : hull) {
            const Vec3 a = pts[static_cast<size_t>(t[0])], b = pts[static_cast<size_t>(t[1])], c = pts[static_cast<size_t>(t[2])];
            // Hull faces are oriented outwards, so d > 0 means the origin is on the inner side.
            const Vec3 nrm = normalized(cross(b - a, c - a));
            const float d = dot(nrm, a);
            if (d < worst) { worst = d; worstNormal = nrm; }
        }
        if (worst > 0.1f) break;
        pts.push_back(worstNormal); // imaginary speaker in the direction of the open side
    }
    dirs_ = pts;

    for (const auto& t : hull) {
        Tri tri{t[0], t[1], t[2], {}};
        if (invert3(dirs_[static_cast<size_t>(t[0])], dirs_[static_cast<size_t>(t[1])], dirs_[static_cast<size_t>(t[2])], tri.inv))
            tris_.push_back(tri);
    }

    // Imaginary speakers pass their gain on to the real speakers adjacent in the hull.
    const int numImag = static_cast<int>(dirs_.size()) - numReal_;
    for (int im = 0; im < numImag; ++im) {
        const int id = numReal_ + im;
        std::vector<int> neighbours;
        for (const auto& t : hull) {
            if (t[0] != id && t[1] != id && t[2] != id) continue;
            for (int v : t)
                if (v < numReal_ && std::find(neighbours.begin(), neighbours.end(), v) == neighbours.end()) neighbours.push_back(v);
        }
        owners_.push_back(neighbours); // owners_ index id refers to real direction indices here
    }
}

void VbapPanner::addUniqueGain(int dirIndex, float g, float* gains) const noexcept {
    if (dirIndex < 0) return;
    if (dirIndex < numReal_) {
        const auto& o = owners_[static_cast<size_t>(dirIndex)];
        const float share = g / std::sqrt(static_cast<float>(o.size()));
        for (int spk : o) gains[spk] += share;
        return;
    }
    // Imaginary: distribute to neighbouring real directions.
    if (static_cast<size_t>(dirIndex) >= owners_.size()) return;
    const auto& nb = owners_[static_cast<size_t>(dirIndex)];
    if (nb.empty()) return;
    const float share = g / std::sqrt(static_cast<float>(nb.size()));
    for (int d : nb) addUniqueGain(d, share, gains);
}

void VbapPanner::computeDirection(Vec3 dir, float* gains) const noexcept {
    for (int i = 0; i < numSpeakers_; ++i) gains[i] = 0.0f;
    if (numReal_ == 0) return;
    if (numReal_ == 1) {
        addUniqueGain(0, 1.0f, gains);
        normaliseEnergy(gains, numSpeakers_);
        return;
    }
    dir = normalized(dir);

    if (flat_) {
        const float h = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        float g[kMaxSpeakers] = {};
        if (h > 1.0e-4f) {
            const float px = dir.x / h, py = dir.y / h;
            int bestPair = -1;
            float bestMin = -1.0e9f, b1 = 0.0f, b2 = 0.0f;
            for (size_t k = 0; k < pairs_.size(); ++k) {
                const Pair& pr = pairs_[k];
                const float g1 = pr.inv[0] * px + pr.inv[1] * py;
                const float g2 = pr.inv[2] * px + pr.inv[3] * py;
                const float m = std::min(g1, g2);
                if (m > bestMin) { bestMin = m; bestPair = static_cast<int>(k); b1 = g1; b2 = g2; }
                if (m >= -kEps) break;
            }
            if (bestPair >= 0 && bestMin >= -kEps) {
                g[pairs_[static_cast<size_t>(bestPair)].a] = std::max(0.0f, b1);
                g[pairs_[static_cast<size_t>(bestPair)].b] = std::max(0.0f, b2);
            } else {
                // Outside every pair (gap ≥ 180°): snap to the nearest speaker.
                int nearest = 0;
                float bestDot = -2.0f;
                for (int i = 0; i < numReal_; ++i) {
                    const Vec3 d = dirs_[static_cast<size_t>(i)];
                    const float l = std::sqrt(d.x * d.x + d.y * d.y);
                    const float c = l > 1.0e-6f ? (d.x * px + d.y * py) / l : -1.0f;
                    if (c > bestDot) { bestDot = c; nearest = i; }
                }
                g[nearest] = 1.0f;
            }
            normaliseEnergy(g, numReal_);
        }
        // Near the poles of a flat layout the direction is undefined: blend towards all speakers.
        constexpr float kBlend = 0.35f;
        if (h < kBlend) {
            const float t = h / kBlend;
            const float eq = (1.0f - t) / std::sqrt(static_cast<float>(numReal_));
            for (int i = 0; i < numReal_; ++i) g[i] = g[i] * t + eq;
            normaliseEnergy(g, numReal_);
        }
        for (int i = 0; i < numReal_; ++i)
            if (g[i] != 0.0f) addUniqueGain(i, g[i], gains);
        normaliseEnergy(gains, numSpeakers_);
        return;
    }

    // 3D: pick the triangle whose smallest gain is largest.
    int best = -1;
    float bestMin = -1.0e9f, bg[3] = {0, 0, 0};
    for (size_t k = 0; k < tris_.size(); ++k) {
        const Tri& t = tris_[k];
        const float g0 = t.inv[0] * dir.x + t.inv[1] * dir.y + t.inv[2] * dir.z;
        const float g1 = t.inv[3] * dir.x + t.inv[4] * dir.y + t.inv[5] * dir.z;
        const float g2 = t.inv[6] * dir.x + t.inv[7] * dir.y + t.inv[8] * dir.z;
        const float m = std::min(g0, std::min(g1, g2));
        if (m > bestMin) {
            bestMin = m;
            best = static_cast<int>(k);
            bg[0] = g0; bg[1] = g1; bg[2] = g2;
        }
        if (m >= -kEps) break;
    }
    if (best < 0) return;
    const Tri& t = tris_[static_cast<size_t>(best)];
    float n = 0.0f;
    for (float& v : bg) { v = std::max(0.0f, v); n += v * v; }
    if (n < 1.0e-12f) return;
    n = 1.0f / std::sqrt(n);
    addUniqueGain(t.a, bg[0] * n, gains);
    addUniqueGain(t.b, bg[1] * n, gains);
    addUniqueGain(t.c, bg[2] * n, gains);
    normaliseEnergy(gains, numSpeakers_);
}

void VbapPanner::compute(const PanInput& in, float* gains) const noexcept {
    const Vec3 pos = isFinite(in.position) ? in.position : Vec3{0, 1, 0};
    const float spread = safeClamp(in.spread, 0.0f, 1.0f, 0.0f);
    const float dist = length(pos);
    const Vec3 dir = normalized(pos);

    if (spread < 1.0e-3f) {
        computeDirection(dir, gains);
    } else {
        // MDAP: sum of a ring of directions around the main one.
        for (int i = 0; i < numSpeakers_; ++i) gains[i] = 0.0f;
        float tmp[kMaxSpeakers];
        const float angle = spread * 90.0f * kDegToRad;
        const Vec3 helper = std::fabs(dir.z) < 0.9f ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
        const Vec3 u = normalized(cross(dir, helper));
        const Vec3 v = cross(dir, u);
        constexpr int kRing = 8;
        computeDirection(dir, tmp);
        for (int i = 0; i < numSpeakers_; ++i) gains[i] += tmp[i];
        for (int k = 0; k < kRing; ++k) {
            const float phi = kTwoPi * static_cast<float>(k) / kRing;
            const Vec3 d = dir * std::cos(angle) + (u * std::cos(phi) + v * std::sin(phi)) * std::sin(angle);
            computeDirection(d, tmp);
            for (int i = 0; i < numSpeakers_; ++i) gains[i] += tmp[i];
        }
        normaliseEnergy(gains, numSpeakers_);
    }

    // A source at the listener position has no direction: fade to all speakers.
    constexpr float kInner = 0.3f;
    if (dist < kInner && numReal_ > 0) {
        const float t = dist / kInner;
        int active = 0;
        for (int d = 0; d < numReal_; ++d) active += static_cast<int>(owners_[static_cast<size_t>(d)].size());
        const float eq = (1.0f - t) / std::sqrt(static_cast<float>(std::max(1, active)));
        for (int d = 0; d < numReal_; ++d)
            for (int spk : owners_[static_cast<size_t>(d)]) gains[spk] = gains[spk] * t + eq;
        normaliseEnergy(gains, numSpeakers_);
    }
}

// ===========================================================================
// DBAP
// ===========================================================================

void DbapPanner::prepare(const SpeakerLayout& layout, const Params& params) {
    numSpeakers_ = std::min(static_cast<int>(layout.speakers.size()), kMaxSpeakers);
    params_.rolloffDb = safeClamp(params.rolloffDb, 1.0f, 18.0f, 6.0f);
    params_.blur = safeClamp(params.blur, 0.01f, 50.0f, 0.3f);
    params_.directivity = safeClamp(params.directivity, 0.0f, 1.0f, 0.7f);
    exponent_ = params_.rolloffDb / (20.0f * std::log10(2.0f));
    spk_.clear();
    layoutRadius_ = 1.0f;
    for (int i = 0; i < numSpeakers_; ++i) {
        const Speaker& s = layout.speakers[static_cast<size_t>(i)];
        if (!isMain(s)) continue;
        spk_.push_back({s.position, s.aimDirection(), i});
        layoutRadius_ = std::max(layoutRadius_, length(s.position));
    }
}

void DbapPanner::compute(const PanInput& in, float* gains) const noexcept {
    for (int i = 0; i < numSpeakers_; ++i) gains[i] = 0.0f;
    if (spk_.empty()) return;
    const Vec3 p = isFinite(in.position) ? in.position : Vec3{0, 1, 0};
    const float spread = safeClamp(in.spread, 0.0f, 1.0f, 0.0f);
    const float blur = params_.blur + spread * layoutRadius_;
    const float blur2 = blur * blur;
    const float dirW = params_.directivity;
    for (const auto& s : spk_) {
        const Vec3 v = p - s.pos;
        const float d2 = dot(v, v) + blur2;
        float w = std::pow(d2, -0.5f * exponent_);
        if (dirW > 0.0f) {
            const float len = std::sqrt(dot(v, v));
            const float c = len > 1.0e-4f ? dot(s.aim, v) / len : 1.0f;
            // Blend the cardioid with the spread so that diffuse sources use all speakers.
            const float card = 0.5f + 0.5f * c;
            const float effective = dirW * (1.0f - spread);
            w *= (1.0f - effective) + effective * card;
        }
        gains[s.index] = std::isfinite(w) ? w : 0.0f;
    }
    normaliseEnergy(gains, numSpeakers_);
}

// ===========================================================================
// Ambisonics
// ===========================================================================

void encodeSN3D(Vec3 d, int order, float* out) noexcept {
    // AmbiX axes: X front, Y left, Z up.
    const float x = d.y, y = -d.x, z = d.z;
    out[0] = 1.0f;
    if (order < 1) return;
    out[1] = y;
    out[2] = z;
    out[3] = x;
    if (order < 2) return;
    const float s3 = std::sqrt(3.0f);
    out[4] = s3 * x * y;
    out[5] = s3 * y * z;
    out[6] = 0.5f * (3.0f * z * z - 1.0f);
    out[7] = s3 * x * z;
    out[8] = 0.5f * s3 * (x * x - y * y);
    if (order < 3) return;
    const float c58 = std::sqrt(5.0f / 8.0f), s15 = std::sqrt(15.0f), c38 = std::sqrt(3.0f / 8.0f);
    out[9] = c58 * y * (3.0f * x * x - y * y);
    out[10] = s15 * x * y * z;
    out[11] = c38 * y * (5.0f * z * z - 1.0f);
    out[12] = 0.5f * z * (5.0f * z * z - 3.0f);
    out[13] = c38 * x * (5.0f * z * z - 1.0f);
    out[14] = 0.5f * s15 * z * (x * x - y * y);
    out[15] = c58 * x * (x * x - 3.0f * y * y);
}

float maxReWeight(int n, int order) noexcept {
    if (n <= 0) return 1.0f;
    const float x = std::cos(137.9f * kDegToRad / (static_cast<float>(order) + 1.51f));
    // Legendre polynomial P_n(x) via recurrence.
    float p0 = 1.0f, p1 = x;
    for (int k = 2; k <= n; ++k) {
        const float p2 = ((2.0f * k - 1.0f) * x * p1 - (k - 1.0f) * p0) / static_cast<float>(k);
        p0 = p1;
        p1 = p2;
    }
    return p1;
}

void AmbisonicsPanner::prepare(const SpeakerLayout& layout, int order) {
    order_ = std::clamp(order, 1, kMaxAmbiOrder);
    numSpeakers_ = std::min(static_cast<int>(layout.speakers.size()), kMaxSpeakers);
    vbap_.prepare(layout);
    decoder_.assign(static_cast<size_t>(numSpeakers_) * kMaxAmbiChannels, 0.0f);
    const int nch = numChannels();

    // Uniform-ish virtual loudspeaker grid (Fibonacci sphere).
    constexpr int kGrid = 2000;
    const float golden = kPi * (3.0f - std::sqrt(5.0f));
    float y[kMaxAmbiChannels];
    float g[kMaxSpeakers];
    float w[kMaxAmbiChannels];
    for (int c = 0; c < nch; ++c) {
        const int n = static_cast<int>(std::sqrt(static_cast<float>(c)) + 1.0e-4f);
        w[c] = (2.0f * n + 1.0f) * maxReWeight(n, order_);
    }
    for (int k = 0; k < kGrid; ++k) {
        const float zz = 1.0f - 2.0f * (static_cast<float>(k) + 0.5f) / kGrid;
        const float r = std::sqrt(std::max(0.0f, 1.0f - zz * zz));
        const float phi = golden * static_cast<float>(k);
        const Vec3 d{r * std::cos(phi), r * std::sin(phi), zz};
        encodeSN3D(d, order_, y);
        vbap_.computeDirection(d, g);
        for (int m = 0; m < numSpeakers_; ++m) {
            if (g[m] == 0.0f) continue;
            float* row = &decoder_[static_cast<size_t>(m) * kMaxAmbiChannels];
            for (int c = 0; c < nch; ++c) row[c] += g[m] * w[c] * y[c] / kGrid;
        }
    }
}

void AmbisonicsPanner::compute(const PanInput& in, float* gains) const noexcept {
    for (int i = 0; i < numSpeakers_; ++i) gains[i] = 0.0f;
    const Vec3 pos = isFinite(in.position) ? in.position : Vec3{0, 1, 0};
    const float spread = safeClamp(in.spread, 0.0f, 1.0f, 0.0f);
    float y[kMaxAmbiChannels] = {};
    encodeSN3D(normalized(pos), order_, y);
    // Spread and proximity to the listener fade out the directional components.
    const float dist = length(pos);
    const float near = std::min(1.0f, dist / 0.3f);
    for (int c = 1; c < numChannels(); ++c) {
        const int n = static_cast<int>(std::sqrt(static_cast<float>(c)) + 1.0e-4f);
        y[c] *= near * std::pow(1.0f - spread, static_cast<float>(n));
    }
    decode(y, gains);
    normaliseEnergy(gains, numSpeakers_);
}

void AmbisonicsPanner::decode(const float* ambi, float* speakers) const noexcept {
    const int nch = numChannels();
    for (int m = 0; m < numSpeakers_; ++m) {
        const float* row = &decoder_[static_cast<size_t>(m) * kMaxAmbiChannels];
        float s = 0.0f;
        for (int c = 0; c < nch; ++c) s += row[c] * ambi[c];
        speakers[m] = s;
    }
}

// ===========================================================================
// Dispatcher
// ===========================================================================

void Panner::prepare(const SpeakerLayout& layout, PannerKind kind, const DbapPanner::Params& dbap, int ambiOrder) {
    kind_ = kind;
    numSpeakers_ = std::min(static_cast<int>(layout.speakers.size()), kMaxSpeakers);
    switch (kind) {
    case PannerKind::VBAP: vbap_.prepare(layout); break;
    case PannerKind::Ambisonics: ambi_.prepare(layout, ambiOrder); break;
    case PannerKind::DBAP:
    default:
        kind_ = PannerKind::DBAP;
        dbap_.prepare(layout, dbap);
        break;
    }
}

void Panner::compute(const PanInput& in, float* gains) const noexcept {
    switch (kind_) {
    case PannerKind::VBAP: vbap_.compute(in, gains); break;
    case PannerKind::Ambisonics: ambi_.compute(in, gains); break;
    case PannerKind::DBAP:
    default: dbap_.compute(in, gains); break;
    }
}

} // namespace rk
