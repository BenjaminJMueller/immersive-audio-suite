// Raumklang core: small math helpers. Header-only, realtime-safe.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rk {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;
inline constexpr float kSpeedOfSound = 343.0f; // m/s at 20 °C

/// Cartesian position in metres. Listener reference is the origin.
/// x = right, y = front, z = up (azimuth 0° = front, positive azimuth = left,
/// matching the usual audio convention of counter-clockwise angles seen from above).
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr bool operator==(const Vec3&) const = default;
};

constexpr float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }

/// Returns a unit vector, or `fallback` if v is (near) zero or not finite.
inline Vec3 normalized(Vec3 v, Vec3 fallback = {0.0f, 1.0f, 0.0f}) {
    const float len = length(v);
    if (!(len > 1.0e-9f) || !std::isfinite(len)) return fallback;
    return v * (1.0f / len);
}

inline bool isFinite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

/// Spherical coordinates in degrees and metres.
struct Polar {
    float azimuthDeg = 0.0f;   // 0 = front, +90 = left
    float elevationDeg = 0.0f; // +90 = up
    float distance = 1.0f;
};

inline Vec3 toCartesian(Polar p) {
    const float az = p.azimuthDeg * kDegToRad;
    const float el = p.elevationDeg * kDegToRad;
    const float c = std::cos(el);
    return {-std::sin(az) * c * p.distance, std::cos(az) * c * p.distance, std::sin(el) * p.distance};
}

inline Polar toPolar(Vec3 v) {
    Polar p;
    p.distance = length(v);
    if (p.distance < 1.0e-9f) return {0.0f, 0.0f, 0.0f};
    p.azimuthDeg = std::atan2(-v.x, v.y) * kRadToDeg;
    p.elevationDeg = std::asin(std::clamp(v.z / p.distance, -1.0f, 1.0f)) * kRadToDeg;
    return p;
}

inline float dbToGain(float db) { return db <= -144.0f ? 0.0f : std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g) { return g <= 1.0e-8f ? -160.0f : 20.0f * std::log10(g); }

/// Clamps a value and maps NaN to `fallback`. Used to sanitise every external parameter.
inline float safeClamp(float v, float lo, float hi, float fallback) {
    if (!std::isfinite(v)) return std::isnan(v) ? fallback : (v > 0 ? hi : lo);
    return std::clamp(v, lo, hi);
}

inline Vec3 safeClamp(Vec3 v, float limit) {
    return {safeClamp(v.x, -limit, limit, 0.0f), safeClamp(v.y, -limit, limit, 0.0f),
            safeClamp(v.z, -limit, limit, 0.0f)};
}

/// Flushes denormals / non-finite values to zero. Cheap enough per sample.
inline float sanitizeSample(float s) {
    if (std::isnan(s)) return 0.0f;
    if (std::fabs(s) < 1.0e-20f) return 0.0f;
    return std::clamp(s, -1.0e6f, 1.0e6f); // also catches +/- infinity
}

/// Wraps an angle in degrees into [-180, 180).
inline float wrapDegrees(float deg) {
    if (!std::isfinite(deg)) return 0.0f;
    float r = std::fmod(deg + 180.0f, 360.0f);
    if (r < 0.0f) r += 360.0f;
    return r - 180.0f;
}

/// RAII guard that enables flush-to-zero / denormals-are-zero on x86 and ARM.
class ScopedNoDenormals {
public:
    ScopedNoDenormals() noexcept;
    ~ScopedNoDenormals() noexcept;
    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
    std::intptr_t previous_ = 0;
};

} // namespace rk
