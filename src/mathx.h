// mathx.h - kleine Vektor-/Noise-Bibliothek. Die Noise-Funktionen sind bitgenau
// identisch zu shaders/common.glsl, damit CPU (Platzierung von Vegetation) und
// GPU (Einfaerbung von Boden/Fassaden) dieselben Masken sehen.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

struct vec3 {
    float x = 0, y = 0, z = 0;
    vec3() {}
    vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    explicit vec3(float a) : x(a), y(a), z(a) {}
    vec3 operator+(const vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    vec3 operator-(const vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    vec3 operator*(const vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    vec3 operator-() const { return {-x, -y, -z}; }
    vec3& operator+=(const vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    vec3& operator-=(const vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};
inline float dot(const vec3& a, const vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(const vec3& a, const vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(const vec3& a) { return std::sqrt(dot(a, a)); }
inline vec3 normalize(const vec3& a) { float l = length(a); return l > 1e-12f ? a / l : vec3(0, 1, 0); }
inline vec3 mix(const vec3& a, const vec3& b, float t) { return a + (b - a) * t; }
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float smoothstepf(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0.f, 1.f); return t * t * (3.f - 2.f * t); }

// 3x3 Matrix (Spalten = Achsen)
struct mat3 {
    vec3 c0{1, 0, 0}, c1{0, 1, 0}, c2{0, 0, 1};
    vec3 operator*(const vec3& v) const { return c0 * v.x + c1 * v.y + c2 * v.z; }
    mat3 operator*(const mat3& o) const { return {(*this) * o.c0, (*this) * o.c1, (*this) * o.c2}; }
};
inline mat3 rotY(float a) { float c = std::cos(a), s = std::sin(a); return {{c, 0, -s}, {0, 1, 0}, {s, 0, c}}; }
inline mat3 rotX(float a) { float c = std::cos(a), s = std::sin(a); return {{1, 0, 0}, {0, c, s}, {0, -s, c}}; }
inline mat3 rotZ(float a) { float c = std::cos(a), s = std::sin(a); return {{c, s, 0}, {-s, c, 0}, {0, 0, 1}}; }
inline mat3 scaleM(float s) { return {{s, 0, 0}, {0, s, 0}, {0, 0, s}}; }

// Affine Transformation: Rotation/Skalierung + Translation
struct xform {
    mat3 r; vec3 t;
    vec3 operator*(const vec3& v) const { return r * v + t; }
    xform operator*(const xform& o) const { return {r * o.r, r * o.t + t}; }
    void toRows(float* m) const { // 3x4, zeilenweise (VkTransformMatrixKHR)
        m[0] = r.c0.x; m[1] = r.c1.x; m[2] = r.c2.x; m[3] = t.x;
        m[4] = r.c0.y; m[5] = r.c1.y; m[6] = r.c2.y; m[7] = t.y;
        m[8] = r.c0.z; m[9] = r.c1.z; m[10] = r.c2.z; m[11] = t.z;
    }
};

struct RNG {
    uint32_t s;
    explicit RNG(uint32_t seed = 1) : s(seed * 747796405u + 2891336453u) {}
    uint32_t u() { s = s * 747796405u + 2891336453u; uint32_t w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u; return (w >> 22u) ^ w; }
    float f() { return (u() >> 8) * (1.0f / 16777216.0f); }
    float r(float a, float b) { return a + (b - a) * f(); }
    int i(int a, int b) { return a + int(u() % uint32_t(b - a + 1)); } // inklusive
    vec3 unit() { for (;;) { vec3 v(r(-1, 1), r(-1, 1), r(-1, 1)); float l = dot(v, v); if (l > 0.01f && l < 1.f) return v / std::sqrt(l); } }
};

// ---- Noise (identisch zu common.glsl) ----
inline uint32_t hashu(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
inline float hash1(uint32_t x) { return float(hashu(x)) * (1.0f / 4294967296.0f); }
inline float hash2i(int x, int y) { return hash1(uint32_t(x) * 0x9E3779B1U ^ hashu(uint32_t(y) + 0x7F4A7C15U)); }
inline float vnoise(float px, float py) {
    float ix = std::floor(px), iy = std::floor(py);
    float fx = px - ix, fy = py - iy;
    fx = fx * fx * (3.f - 2.f * fx); fy = fy * fy * (3.f - 2.f * fy);
    int x = int(ix), y = int(iy);
    float a = hash2i(x, y), b = hash2i(x + 1, y), c = hash2i(x, y + 1), d = hash2i(x + 1, y + 1);
    return mixf(mixf(a, b, fx), mixf(c, d, fx), fy);
}
inline float fbm2(float px, float py, int oct) {
    float s = 0, a = 0.5f;
    for (int i = 0; i < oct; i++) { s += a * vnoise(px, py); px = px * 2.03f + 17.3f; py = py * 2.03f + 9.1f; a *= 0.5f; }
    return s;
}
// Wie stark hat sich die Natur einen Punkt der Stadt zurueckgeholt (0..1)
inline float overgrow(float x, float z) {
    float n = fbm2(x * 0.03f, z * 0.03f, 3);
    float w = clampf((200.f - x) / 900.f, 0.f, 1.f);
    return smoothstepf(0.38f, 0.60f, n + w * 0.22f);
}
// Efeu-Maske auf Fassaden (u entlang der Wand, v = Hoehe, jeweils in Metern)
inline float ivyMask(float u, float v, float seed, float level) {
    float n = fbm2(u * 0.07f + seed * 1.37f, v * 0.07f + seed * 0.73f, 3);
    float g = n + 0.30f * std::exp(-v * 0.05f) + (level - 0.5f) * 0.45f;
    return smoothstepf(0.58f, 0.70f, g);
}
