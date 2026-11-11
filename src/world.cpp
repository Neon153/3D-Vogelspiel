// world.cpp - prozedurale Erzeugung der verlassenen Stadt
#include "world.h"
#include <cstring>

static const float PI = 3.14159265f;
static const float FX0 = -1600, FX1 = 1120, FZ0 = -1120, FZ1 = 1280, CH = 160;
static const int CNX = 17, CNZ = 15;
static const float CITY_X = 560, CITY_Z = 520;   // halbe Ausdehnung des Stadt-Rechtecks
static const float GROUND = 0.16f;                // Oberkante Gehweg/Block

// ------------------------------------------------------------------ Packen
static uint32_t packN(vec3 n) {
    n = normalize(n);
    float s = std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z);
    float x = n.x / s, y = n.y / s;
    if (n.z < 0) { float ox = (1 - std::fabs(y)) * (x >= 0 ? 1.f : -1.f), oy = (1 - std::fabs(x)) * (y >= 0 ? 1.f : -1.f); x = ox; y = oy; }
    uint32_t ux = (uint32_t)std::lround(clampf(x * 0.5f + 0.5f, 0, 1) * 65535.f), uy = (uint32_t)std::lround(clampf(y * 0.5f + 0.5f, 0, 1) * 65535.f);
    return ux | (uy << 16);
}
static uint32_t packC(vec3 c, float a = 1) {
    auto b = [](float v) { return (uint32_t)std::lround(clampf(std::sqrt(std::max(v, 0.f)), 0, 1) * 255.f); };
    return b(c.x) | (b(c.y) << 8) | (b(c.z) << 16) | ((uint32_t)std::lround(clampf(a, 0, 1) * 255.f) << 24);
}
static uint32_t matp(uint32_t id, uint32_t A = 0, uint32_t B = 0, uint32_t C = 0) { return id | ((A & 255) << 8) | ((B & 255) << 16) | ((C & 255) << 24); }

static uint32_t addV(MeshB& m, vec3 p, vec3 n, float u, float v, uint32_t col, uint32_t mat) {
    Vtx x; x.px = p.x; x.py = p.y; x.pz = p.z; x.n = packN(n); x.u = u; x.v = v; x.col = col; x.mat = mat;
    m.v.push_back(x); return (uint32_t)m.v.size() - 1;
}
// a(u0,v0) b(u1,v0) c(u1,v1) d(u0,v1)
static void quad(MeshB& m, vec3 a, vec3 b, vec3 c, vec3 d, vec3 n, uint32_t col, uint32_t mat, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
    uint32_t i = addV(m, a, n, u0, v0, col, mat); addV(m, b, n, u1, v0, col, mat); addV(m, c, n, u1, v1, col, mat); addV(m, d, n, u0, v1, col, mat);
    m.io.insert(m.io.end(), {i, i + 1, i + 2, i, i + 2, i + 3});
}
static void quadAuto(MeshB& m, vec3 a, vec3 b, vec3 c, vec3 d, uint32_t col, uint32_t mat) {
    vec3 n = normalize(cross(b - a, d - a));
    float w = length(b - a), h = length(d - a);
    float u0 = a.x + a.z, v0 = a.y;
    quad(m, a, b, c, d, n, col, mat, u0, v0, u0 + w, v0 + h);
}
static void tri(MeshB& m, vec3 a, vec3 b, vec3 c, vec3 n, uint32_t col, uint32_t mat) {
    uint32_t i = addV(m, a, n, a.x + a.z, a.y, col, mat); addV(m, b, n, b.x + b.z, b.y, col, mat); addV(m, c, n, c.x + c.z, c.y, col, mat);
    m.io.insert(m.io.end(), {i, i + 1, i + 2});
}
// p[i], i = x + 2y + 4z
static void hexa(MeshB& m, const vec3* p, uint32_t col, uint32_t matSide, uint32_t matTop, uint32_t colTop, bool bottom = false) {
    quadAuto(m, p[0], p[4], p[6], p[2], col, matSide);
    quadAuto(m, p[5], p[1], p[3], p[7], col, matSide);
    quadAuto(m, p[1], p[0], p[2], p[3], col, matSide);
    quadAuto(m, p[4], p[5], p[7], p[6], col, matSide);
    { vec3 n = normalize(cross(p[6] - p[2], p[3] - p[2])); quad(m, p[2], p[6], p[7], p[3], n, colTop, matTop, p[2].z, p[2].x, p[6].z, p[3].x); }
    if (bottom) quadAuto(m, p[0], p[1], p[5], p[4], col, matSide);
}
static void box(MeshB& m, vec3 mn, vec3 mx, uint32_t col, uint32_t matSide, uint32_t matTop, uint32_t colTop, bool bottom = false) {
    vec3 p[8]; for (int i = 0; i < 8; i++) p[i] = vec3((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
    hexa(m, p, col, matSide, matTop, colTop, bottom);
}
static void boxR(MeshB& m, vec3 c, vec3 half, const mat3& r, uint32_t col, uint32_t mat) {
    vec3 p[8]; for (int i = 0; i < 8; i++) p[i] = c + r * vec3((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z);
    hexa(m, p, col, mat, mat, col, true);
}
static void frustum(MeshB& m, vec3 a, vec3 b, float ra, float rb, int sides, uint32_t col, uint32_t mat, float va = 0, float vb = 1) {
    vec3 d = normalize(b - a);
    vec3 t = std::fabs(d.y) < 0.9f ? normalize(cross(d, vec3(0, 1, 0))) : normalize(cross(d, vec3(1, 0, 0)));
    vec3 bt = cross(d, t);
    uint32_t base = (uint32_t)m.v.size();
    for (int i = 0; i <= sides; i++) {
        float ang = i * 2 * PI / sides; vec3 n = t * std::cos(ang) + bt * std::sin(ang);
        float u = float(i) / sides * 2 * PI * ra;
        addV(m, a + n * ra, n, u, va, col, mat); addV(m, b + n * rb, n, u, vb, col, mat);
    }
    for (int i = 0; i < sides; i++) { uint32_t k = base + 2 * i; m.io.insert(m.io.end(), {k, k + 2, k + 1, k + 1, k + 2, k + 3}); }
}
// Blattkarte: uv (0,0) (1,0) (1,1) (0,1) - Reihenfolge ist fuer die Alpha-Maske im Shader verbindlich
static void leafCard(MeshB& m, vec3 c, vec3 ax, vec3 ay, vec3 n, uint32_t col) {
    uint32_t mt = matp(M_LEAF);
    uint32_t i = addV(m, c - ax - ay, n, 0, 0, col, mt); addV(m, c + ax - ay, n, 1, 0, col, mt); addV(m, c + ax + ay, n, 1, 1, col, mt); addV(m, c - ax + ay, n, 0, 1, col, mt);
    m.il.insert(m.il.end(), {i, i + 1, i + 2, i, i + 2, i + 3});
}
// ------------------------------------------------------------------ Vegetation
struct TreeGen {
    MeshB& m; RNG& rng;
    vec3 crown; int maxD = 3; float leafSize = 0.6f; vec3 leafCol{0.09f, 0.19f, 0.045f}; vec3 barkCol{0.13f, 0.10f, 0.075f};
    bool leaves = true; int nCards = 14;
    void cluster(vec3 p, float R, int n) {
        for (int i = 0; i < n; i++) {
            vec3 c = p + rng.unit() * (R * std::cbrt(rng.f()));
            vec3 out = normalize(c - crown);
            vec3 nn = normalize(rng.unit() * 0.9f + vec3(0, 0.7f, 0) + out * 0.9f);
            vec3 t = normalize(cross(nn, rng.unit())); vec3 b = cross(nn, t);
            float s = leafSize * rng.r(0.75f, 1.25f);
            vec3 col = leafCol * rng.r(0.65f, 1.3f); col.x *= rng.r(0.8f, 1.35f);
            leafCard(m, c, t * s, b * s, normalize(nn * 0.5f + out * 0.8f + vec3(0, 0.35f, 0)), packC(col));
        }
    }
    void branch(vec3 p, vec3 d, float len, float r, int depth) {
        int segs = depth == 0 ? 3 : 2;
        int sides = depth == 0 ? 8 : (depth == 1 ? 6 : 4);
        uint32_t bc = packC(barkCol * rng.r(0.8f, 1.2f));
        for (int s = 0; s < segs; s++) {
            vec3 d2 = normalize(d + rng.unit() * (depth == 0 ? 0.08f : 0.22f) + vec3(0, depth > 0 ? 0.06f : 0, 0));
            vec3 q = p + d2 * (len / segs);
            frustum(m, p - d2 * 0.05f, q, r, r * 0.82f, sides, bc, matp(M_BARK), p.y * 0.5f, q.y * 0.5f);
            p = q; d = d2; r *= 0.82f;
        }
        if (depth >= maxD) { if (leaves) cluster(p, len * 0.95f, nCards); return; }
        if (leaves && depth == maxD - 1) cluster(p, len * 0.6f, nCards / 2);
        int nb = depth == 0 ? rng.i(3, 5) : rng.i(2, 3);
        float ph0 = rng.r(0, 2 * PI);
        vec3 t = std::fabs(d.y) < 0.9f ? normalize(cross(d, vec3(0, 1, 0))) : normalize(cross(d, vec3(1, 0, 0)));
        vec3 bt = cross(d, t);
        for (int i = 0; i < nb; i++) {
            float ph = ph0 + i * 2 * PI / nb + rng.r(-0.4f, 0.4f), tilt = rng.r(0.5f, 1.0f);
            vec3 cd = normalize(d * std::cos(tilt) + (t * std::cos(ph) + bt * std::sin(ph)) * std::sin(tilt));
            branch(p, cd, len * rng.r(0.6f, 0.78f), r * 0.62f, depth + 1);
        }
        if (depth < 2) branch(p, normalize(d + rng.unit() * 0.25f), len * 0.62f, r * 0.7f, depth + 1);
    }
};
static MeshB makeDeciduous(RNG& rng, float H, vec3 leafCol, bool leaves, int maxD = 3) {
    MeshB m; m.leafType = 1;
    TreeGen g{m, rng}; g.crown = vec3(0, H * 0.62f, 0); g.leafCol = leafCol; g.leaves = leaves; g.maxD = maxD;
    g.leafSize = 0.8f; if (!leaves) g.barkCol = vec3(0.16f, 0.15f, 0.14f);
    g.branch(vec3(0, -0.4f, 0), vec3(0, 1, 0), H * 0.36f, H * 0.02f + 0.1f, 0);
    return m;
}
static MeshB makeConifer(RNG& rng, float H) {
    MeshB m; m.leafType = 2;
    float r0 = H * 0.016f + 0.1f; uint32_t bc = packC(vec3(0.11f, 0.08f, 0.06f));
    for (int i = 0; i < 5; i++) { float a = i / 5.f, b = (i + 1) / 5.f; frustum(m, vec3(0, a * H - 0.4f, 0), vec3(0, b * H, 0), r0 * (1 - a * 0.95f), r0 * (1 - b * 0.95f), 6, bc, matp(M_BARK), a * H * 0.5f, b * H * 0.5f); }
    float y = H * rng.r(0.14f, 0.24f); float ybase = y;
    while (y < H * 0.985f) {
        float t = (y - ybase) / (H - ybase);
        float L = std::pow(1 - t, 0.85f) * H * rng.r(0.19f, 0.23f) + 0.3f;
        int nb = t < 0.6f ? 6 : 5; float ph0 = rng.r(0, 2 * PI);
        for (int i = 0; i < nb; i++) {
            float ph = ph0 + i * 2 * PI / nb + rng.r(-0.25f, 0.25f);
            vec3 dir = normalize(vec3(std::cos(ph), -rng.r(0.2f, 0.55f) * (1 - t * 0.7f), std::sin(ph)));
            vec3 base(0, y + rng.r(-0.2f, 0.2f), 0), tip = base + dir * (L * rng.r(0.85f, 1.1f));
            vec3 side = normalize(cross(dir, vec3(0, 1, 0))); vec3 upv = cross(side, dir);
            vec3 col = vec3(0.028f, 0.075f, 0.032f) * rng.r(0.7f, 1.35f);
            vec3 n = normalize(upv * 0.8f + dir * 0.6f);
            leafCard(m, (base + tip) * 0.5f, (tip - base) * 0.5f, side * (L * 0.26f), n, packC(col));
            leafCard(m, (base + tip) * 0.5f - upv * (L * 0.08f), (tip - base) * 0.5f, upv * (L * 0.16f), normalize(side * (rng.f() < 0.5f ? 1.f : -1.f) + dir * 0.6f + vec3(0, 0.4f, 0)), packC(col * 0.85f));
        }
        y += rng.r(0.6f, 0.85f);
    }
    return m;
}
static MeshB makeBush(RNG& rng, float R, vec3 leafCol) {
    MeshB m; m.leafType = 1;
    for (int i = 0; i < 5; i++) { vec3 d = normalize(vec3(rng.r(-1, 1), rng.r(0.8f, 2), rng.r(-1, 1))); frustum(m, vec3(0, -0.2f, 0), d * (R * 0.9f), 0.04f, 0.015f, 4, packC(vec3(0.1f, 0.08f, 0.06f)), matp(M_BARK)); }
    for (int i = 0; i < 70; i++) {
        vec3 u = rng.unit(); u.y = std::fabs(u.y) * 0.8f; vec3 c = u * (R * std::cbrt(rng.f())) + vec3(0, 0.25f, 0);
        vec3 nn = normalize(u + rng.unit() * 0.6f + vec3(0, 0.5f, 0));
        vec3 t = normalize(cross(nn, rng.unit())), b = cross(nn, t); float s = rng.r(0.32f, 0.5f);
        vec3 col = leafCol * rng.r(0.7f, 1.3f);
        leafCard(m, c, t * s, b * s, normalize(nn + u), packC(col));
    }
    return m;
}
static MeshB makeGrass(RNG& rng, float R, float hmax, vec3 colBase, vec3 colTip) {
    MeshB m; m.minor = true;
    for (int i = 0; i < 46; i++) {
        float a = rng.r(0, 2 * PI), rr = R * std::sqrt(rng.f());
        vec3 p(std::cos(a) * rr, -0.05f, std::sin(a) * rr);
        float h = hmax * rng.r(0.35f, 1.0f) * (1 - 0.4f * rr / R), w = rng.r(0.05f, 0.1f);
        float fa = rng.r(0, 2 * PI); vec3 f(std::cos(fa), 0, std::sin(fa)); vec3 s(-f.z, 0, f.x);
        vec3 lean = f * (h * rng.r(0.1f, 0.55f));
        vec3 n = normalize(vec3(0, 1, 0) + f * 0.35f);
        vec3 ct = mix(colBase, colTip, rng.r(0.6f, 1.0f)) * rng.r(0.8f, 1.2f);
        uint32_t c0 = packC(colBase * 0.7f), c1 = packC(mix(colBase, ct, 0.6f)), c2 = packC(ct);
        uint32_t mt = matp(M_GRASS);
        uint32_t k = addV(m, p - s * w, n, 0, 0, c0, mt); addV(m, p + s * w, n, 1, 0, c0, mt);
        vec3 mid = p + vec3(0, h * 0.55f, 0) + lean * 0.35f;
        addV(m, mid + s * (w * 0.7f), n, 1, 0.55f, c1, mt); addV(m, mid - s * (w * 0.7f), n, 0, 0.55f, c1, mt);
        addV(m, p + vec3(0, h, 0) + lean, n, 0.5f, 1, c2, mt);
        m.io.insert(m.io.end(), {k, k + 1, k + 2, k, k + 2, k + 3, k + 3, k + 2, k + 4});
    }
    return m;
}
static MeshB makeCar(RNG& rng, int kind) {
    MeshB m; uint32_t paint = packC(vec3(0.5f)), mp = matp(M_CARPAINT), mg = matp(M_CARGLASS);
    float L = kind == 1 ? 2.6f : 2.15f, W = 0.88f, Hb = kind == 1 ? 1.0f : 0.82f;
    box(m, vec3(-W, 0.28f, -L), vec3(W, Hb, L), paint, mp, mp, paint, true);
    vec3 p[8]; float z0 = kind == 1 ? -L + 0.1f : -1.25f, z1 = kind == 1 ? 0.9f : 0.75f, top = kind == 1 ? 1.85f : 1.42f;
    for (int i = 0; i < 8; i++) { bool up = i & 2; float xi = (i & 1) ? 1.f : -1.f; float zz = (i & 4) ? z1 : z0;
        p[i] = vec3(xi * (up ? W - 0.14f : W - 0.02f), up ? top : Hb, up ? zz + ((i & 4) ? -0.45f : 0.3f) : zz); }
    hexa(m, p, packC(vec3(0.3f)), mg, mp, paint);
    uint32_t tire = packC(vec3(0.015f));
    for (int i = 0; i < 4; i++) { float x = (i & 1) ? W - 0.1f : -W - 0.12f, z = (i & 2) ? L * 0.62f : -L * 0.62f;
        frustum(m, vec3(x, 0.33f, z), vec3(x + 0.22f, 0.33f, z), 0.33f, 0.33f, 8, tire, matp(M_METAL, 1)); }
    (void)rng; return m;
}
static MeshB makeLamp() {
    MeshB m; uint32_t c = packC(vec3(0.22f, 0.24f, 0.23f)), mt = matp(M_METAL);
    frustum(m, vec3(0, -0.3f, 0), vec3(0, 7.6f, 0), 0.11f, 0.07f, 6, c, mt);
    frustum(m, vec3(0, 7.5f, 0), vec3(1.6f, 7.9f, 0), 0.05f, 0.04f, 5, c, mt);
    box(m, vec3(1.3f, 7.8f, -0.18f), vec3(2.1f, 7.98f, 0.18f), c, mt, mt, c, true);
    return m;
}

// ------------------------------------------------------------------ Vogel
// Der Vogel ist EIN zusammenhaengendes Netz. Seine Form ist als Distanzfunktion beschrieben (Rumpf, Hals, Kopf,
// Schnabel, Fluegel mit Handschwingen, Schwanzfaecher), deren Teile weich ineinander uebergehen (smooth minimum).
// Daraus wird einmalig per "Surface Nets" ein Dreiecksnetz gewonnen; fuer den Fluegelschlag wird dieses Netz pro
// Frame gebogen (poseBird), es gibt also keine Naehte oder starr gedrehten Einzelteile mehr.
static float smin(float a, float b, float k) { float h = std::max(k - std::fabs(a - b), 0.f) / k; return std::min(a, b) - h * h * k * 0.25f; }
static float sdEll(vec3 p, vec3 c, vec3 r) {
    vec3 q = p - c; float k0 = length(vec3(q.x / r.x, q.y / r.y, q.z / r.z)), k1 = length(vec3(q.x / (r.x * r.x), q.y / (r.y * r.y), q.z / (r.z * r.z)));
    return k1 > 1e-6f ? k0 * (k0 - 1.f) / k1 : -std::min(r.x, std::min(r.y, r.z));
}
static float sdCapsule(vec3 p, vec3 a, vec3 b, float ra, float rb) { vec3 ab = b - a; float t = clampf(dot(p - a, ab) / dot(ab, ab), 0, 1); return length(p - (a + ab * t)) - mixf(ra, rb, t); }
static const float BIRD_XS = 0.075f, BIRD_YS = 0.03f, BIRD_XW = 0.40f; // Schulter- und Handgelenk
static float wingChord(float ax, float z, float& yc, float& s) { // liefert Profiltiefe c (0 = Vorderkante)
    s = clampf((ax - 0.06f) / 0.5f, 0, 1);
    float zL = mixf(0.10f, 0.07f, s) + 0.035f * std::sin(PI * s);
    float zT = mixf(-0.15f, -0.09f, s * s) - 0.03f * std::sin(PI * s) + 0.010f * std::fabs(std::sin(ax * PI / 0.05f));
    float c = clampf((zL - z) / (zL - zT), 0, 1);
    yc = 0.032f + 0.022f * std::sin(PI * c) * (1 - 0.6f * s) - 0.01f * s;
    return c;
}
static float birdSDF(vec3 p) {
    float ax = std::fabs(p.x); vec3 q(ax, p.y, p.z);
    float d = sdEll(q, vec3(0, 0, 0), vec3(0.082f, 0.078f, 0.215f));
    d = smin(d, sdEll(q, vec3(0, -0.008f, 0.07f), vec3(0.086f, 0.082f, 0.12f)), 0.03f);                        // Brust
    d = smin(d, sdCapsule(q, vec3(0, 0.02f, 0.17f), vec3(0, 0.047f, 0.262f), 0.058f, 0.047f), 0.035f);         // Hals
    d = smin(d, sdEll(q, vec3(0, 0.05f, 0.285f), vec3(0.046f, 0.045f, 0.055f)), 0.025f);                       // Kopf
    d = smin(d, sdCapsule(q, vec3(0, 0.045f, 0.325f), vec3(0, 0.03f, 0.372f), 0.021f, 0.009f), 0.012f);        // Schnabel
    d = smin(d, sdCapsule(q, vec3(0, 0.03f, 0.372f), vec3(0, 0.013f, 0.383f), 0.009f, 0.005f), 0.006f);        // Hakenspitze
    {   // Schwanzfaecher
        float t = clampf((-0.14f - p.z) / 0.38f, 0, 1);
        float w = mixf(0.05f, 0.17f, std::pow(t, 0.8f));
        float zEnd = -0.52f + 0.06f * (ax / 0.17f) * (ax / 0.17f) + 0.008f * std::fabs(std::sin(ax * PI / 0.048f));
        float dp = std::max(std::max(ax - w, zEnd - p.z), p.z + 0.12f);
        float yc = 0.005f - 0.02f * t, th = mixf(0.02f, 0.006f, t);
        d = smin(d, std::max(dp, std::fabs(p.y - yc) - th) - 0.003f, 0.03f);
    }
    {   // Fluegelflaeche mit gewoelbtem Profil
        float yc, s; float c = wingChord(ax, p.z, yc, s);
        float zL = mixf(0.10f, 0.07f, s) + 0.035f * std::sin(PI * s);
        float zT = mixf(-0.15f, -0.09f, s * s) - 0.03f * std::sin(PI * s) + 0.010f * std::fabs(std::sin(ax * PI / 0.05f));
        float th = std::max(mixf(0.019f, 0.008f, s) * (2.2f * std::sqrt(c) * (1 - c) + 0.3f), 0.0065f);
        float dp = std::max(std::max(p.z - zL, zT - p.z), std::max(0.04f - ax, ax - 0.56f));
        float dw = std::max(dp, std::fabs(p.y - yc) - th) - 0.004f;
        // Handschwingen: sieben flache, gefingerte Federn
        for (int i = 0; i < 7; i++) {
            float t = i / 6.f, a = mixf(-0.12f, 1.15f, t), len = mixf(0.30f, 0.20f, t);
            float rx = 0.50f, rz = mixf(0.045f, -0.085f, t), dx = std::cos(a), dz = -std::sin(a);
            float u = clampf(((ax - rx) * dx + (p.z - rz) * dz) / len, 0, 1);
            float ex = ax - (rx + dx * len * u), ez = p.z - (rz + dz * len * u);
            float d2 = std::sqrt(ex * ex + ez * ez) - 0.025f * (1 - 0.55f * u * u * u);
            float yf = 0.02f - 0.0035f * i - 0.012f * u * u;
            dw = smin(dw, std::max(d2, std::fabs(p.y - yf) - 0.0055f) - 0.003f, 0.006f);
        }
        d = smin(d, dw, 0.045f);
    }
    return d;
}
static void makeBird(World& w) {
    // Gitter: quer zur duennen Fluegelflaeche (y) feiner aufgeloest
    const vec3 o(-0.86f, -0.125f, -0.57f), cs(0.011f, 0.005f, 0.011f);
    const int NX = 157, NY = 54, NZ = 91;
    auto nid = [&](int i, int j, int k) { return (size_t(k) * (NY + 1) + j) * (NX + 1) + i; };
    std::vector<float> val(size_t(NX + 1) * (NY + 1) * (NZ + 1));
    for (int k = 0; k <= NZ; k++) for (int j = 0; j <= NY; j++) for (int i = 0; i <= NX; i++) val[nid(i, j, k)] = birdSDF(o + vec3(i * cs.x, j * cs.y, k * cs.z));
    auto cid = [&](int i, int j, int k) { return (size_t(k) * NY + j) * NX + i; };
    std::vector<int> cv(size_t(NX) * NY * NZ, -1);
    std::vector<vec3> pos;
    // Surface Nets: pro Zelle mit Vorzeichenwechsel ein Vertex im Schwerpunkt der Kanten-Schnittpunkte
    for (int k = 0; k < NZ; k++) for (int j = 0; j < NY; j++) for (int i = 0; i < NX; i++) {
        float v[8]; int neg = 0;
        for (int c = 0; c < 8; c++) { v[c] = val[nid(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1))]; neg += v[c] < 0; }
        if (neg == 0 || neg == 8) continue;
        vec3 acc(0, 0, 0); int n = 0;
        for (int a = 0; a < 8; a++) for (int ax = 0; ax < 3; ax++) { int b = a | (1 << ax); if (b == a) continue;
            if ((v[a] < 0) == (v[b] < 0)) continue;
            float t = v[a] / (v[a] - v[b]);
            vec3 pa(float(a & 1), float((a >> 1) & 1), float((a >> 2) & 1)), pb(float(b & 1), float((b >> 1) & 1), float((b >> 2) & 1));
            acc += pa + (pb - pa) * t; n++; }
        acc = acc / float(n);
        cv[cid(i, j, k)] = (int)pos.size();
        pos.push_back(o + vec3((i + acc.x) * cs.x, (j + acc.y) * cs.y, (k + acc.z) * cs.z));
    }
    auto quadI = [&](int a, int b, int c, int d, bool flip) {
        if (a < 0 || b < 0 || c < 0 || d < 0) return;
        if (flip) w.birdIdx.insert(w.birdIdx.end(), {(uint32_t)a, (uint32_t)d, (uint32_t)c, (uint32_t)a, (uint32_t)c, (uint32_t)b});
        else w.birdIdx.insert(w.birdIdx.end(), {(uint32_t)a, (uint32_t)b, (uint32_t)c, (uint32_t)a, (uint32_t)c, (uint32_t)d});
    };
    w.birdIdx.clear();
    for (int k = 1; k < NZ; k++) for (int j = 1; j < NY; j++) for (int i = 1; i < NX; i++) {
        float v0 = val[nid(i, j, k)];
        float vx = val[nid(i + 1, j, k)], vy = val[nid(i, j + 1, k)], vz = val[nid(i, j, k + 1)];
        if ((v0 < 0) != (vx < 0)) quadI(cv[cid(i, j - 1, k - 1)], cv[cid(i, j, k - 1)], cv[cid(i, j, k)], cv[cid(i, j - 1, k)], !(v0 < 0));
        if ((v0 < 0) != (vy < 0)) quadI(cv[cid(i - 1, j, k - 1)], cv[cid(i, j, k - 1)], cv[cid(i, j, k)], cv[cid(i - 1, j, k)], v0 < 0);
        if ((v0 < 0) != (vz < 0)) quadI(cv[cid(i - 1, j - 1, k)], cv[cid(i, j - 1, k)], cv[cid(i, j, k)], cv[cid(i - 1, j, k)], !(v0 < 0));
    }
    // Gefieder: Farbe und Material je Vertex aus Lage und Flaechennormale der Ruhepose
    w.birdRest.resize(pos.size()); w.birdW.resize(pos.size() * 3);
    const vec3 brown(0.16f, 0.10f, 0.058f), dark(0.045f, 0.032f, 0.026f), buff(0.46f, 0.36f, 0.24f), cream(0.56f, 0.5f, 0.4f), rufous(0.3f, 0.14f, 0.07f);
    for (size_t i = 0; i < pos.size(); i++) {
        vec3 p = pos[i]; float ax = std::fabs(p.x); const float e = 0.003f;
        vec3 n = normalize(vec3(birdSDF(p + vec3(e, 0, 0)) - birdSDF(p - vec3(e, 0, 0)), birdSDF(p + vec3(0, e, 0)) - birdSDF(p - vec3(0, e, 0)), birdSDF(p + vec3(0, 0, e)) - birdSDF(p - vec3(0, 0, e))));
        float under = smoothstepf(0.25f, -0.35f, n.y);            // 1 = Unterseite
        float mot = vnoise(p.x * 55.f + 3.f, p.z * 55.f), streak = vnoise(p.x * 140.f, p.z * 28.f + 7.f);
        vec3 c; uint32_t mat = matp(M_FEATHER);
        float wingF = smoothstepf(0.09f, 0.16f, ax) * std::max(smoothstepf(-0.25f, -0.19f, p.z), smoothstepf(0.17f, 0.22f, ax));
        // Rumpf: oben braun, unten hell mit dunkler Strichelung
        c = mix(brown * (0.8f + 0.4f * mot), buff * (0.75f + 0.4f * mot) * mixf(1.f, 0.55f, smoothstepf(0.6f, 0.75f, streak)), under);
        // Kopf/Hals heller
        float headF = smoothstepf(0.16f, 0.24f, p.z) * (1 - wingF);
        c = mix(c, cream * (0.8f + 0.3f * mot) * mixf(1.f, 0.7f, smoothstepf(0.55f, 0.7f, streak) * 0.7f), headF);
        if (wingF > 0) {
            float yc, s; float ch = wingChord(std::min(ax, 0.56f), p.z, yc, s);
            float tipF = smoothstepf(0.5f, 0.62f, ax);                 // Handschwingen
            float bars = 0.5f + 0.5f * std::sin(ax * 62.f + p.z * 9.f);
            vec3 top = mix(brown * (0.85f + 0.3f * mot), dark * 1.3f, smoothstepf(0.3f, 0.75f, ch));       // Decken -> Schwungfedern
            top = mix(top, dark, tipF);
            vec3 bot = mix(buff * 0.9f, cream * mixf(1.f, 0.45f, smoothstepf(0.55f, 0.8f, bars)), smoothstepf(0.25f, 0.5f, ch)); // gebaenderte Unterseite
            bot = mix(bot, dark * 1.6f, std::max(tipF * 0.9f, smoothstepf(0.9f, 1.0f, ch) * 0.8f));          // dunkle Spitzen und Hinterkante
            float carpal = std::exp(-((ax - 0.4f) * (ax - 0.4f) + (p.z - 0.06f) * (p.z - 0.06f)) / 0.0035f);
            bot = mix(bot, dark * 1.4f, carpal * 0.8f);                                                      // dunkler Bugfleck
            c = mix(c, mix(top, bot, under), wingF);
        }
        float tailF = smoothstepf(-0.2f, -0.27f, p.z) * (1 - smoothstepf(0.17f, 0.22f, ax));
        if (tailF > 0) {
            float tb = 0.5f + 0.5f * std::sin(p.z * 95.f);
            vec3 tc = mix(brown, rufous, 0.22f) * mixf(1.f, 0.72f, smoothstepf(0.55f, 0.85f, tb)) * (0.85f + 0.3f * mot) * mixf(1.f, 1.6f, under);
            tc = mix(tc, dark * 1.3f, smoothstepf(-0.455f, -0.475f, p.z) * (1 - smoothstepf(-0.5f, -0.515f, p.z)));
            c = mix(c, tc, tailF);
        }
        // Schnabel und Augen
        float beakF = smoothstepf(0.318f, 0.33f, p.z);
        if (beakF > 0.5f) { c = mix(vec3(0.62f, 0.47f, 0.08f), vec3(0.05f, 0.045f, 0.04f), smoothstepf(0.345f, 0.368f, p.z)); mat = matp(M_BEAK); }
        vec3 eye(0.037f, 0.062f, 0.297f); vec3 de = vec3(ax, p.y, p.z) - eye;
        if (dot(de, de) < 0.0105f * 0.0105f) { c = vec3(0.008f); mat = matp(M_BEAK, 1); }
        else if (dot(de, de) < 0.017f * 0.017f) c = mix(c, vec3(0.5f, 0.38f, 0.08f), 0.7f); // gelber Augenring
        Vtx v; v.px = p.x; v.py = p.y; v.pz = p.z; v.n = packN(n); v.u = p.x; v.v = p.z; v.col = packC(c); v.mat = mat;
        w.birdRest[i] = v;
        // Biege-Gewichte: Schulter, Handgelenk, Federspitzen
        float mask = std::max(smoothstepf(-0.25f, -0.19f, p.z), smoothstepf(0.17f, 0.22f, ax));
        w.birdW[i * 3] = smoothstepf(0.05f, 0.17f, ax) * mask;
        w.birdW[i * 3 + 1] = smoothstepf(0.33f, 0.47f, ax);
        w.birdW[i * 3 + 2] = smoothstepf(0.5f, 0.8f, ax);
    }
}
// Verformt das Vogelnetz fuer den aktuellen Fluegelschlag und berechnet glatte Normalen neu.
// Winkel in rad: Schulter (auf/ab), Handgelenk, Federspitzen-Biegung, Pfeilung nach hinten.
void World::poseBird(Vtx* out, float shoulder, float wrist, float tip, float sweep) const {
    size_t n = birdRest.size();
    static std::vector<vec3> P, N; P.resize(n); N.assign(n, vec3(0, 0, 0));
    for (size_t i = 0; i < n; i++) {
        const Vtx& r = birdRest[i]; float sg = r.px < 0 ? -1.f : 1.f, x = std::fabs(r.px), y = r.py, z = r.pz;
        float w1 = birdW[i * 3], w2 = birdW[i * 3 + 1], w3 = birdW[i * 3 + 2];
        if (w2 > 0) { float a = sweep * w2, c = std::cos(a), s = std::sin(a), dx = x - BIRD_XW, dz = z - 0.05f; x = BIRD_XW + c * dx + s * dz; z = 0.05f - s * dx + c * dz; }
        if (w3 > 0) { float a = tip * w3, c = std::cos(a), s = std::sin(a), dx = x - 0.55f, dy = y - 0.03f; x = 0.55f + c * dx - s * dy; y = 0.03f + s * dx + c * dy; }
        if (w2 > 0) { float a = wrist * w2, c = std::cos(a), s = std::sin(a), dx = x - BIRD_XW, dy = y - 0.035f; x = BIRD_XW + c * dx - s * dy; y = 0.035f + s * dx + c * dy; }
        if (w1 > 0) { float a = shoulder * w1, c = std::cos(a), s = std::sin(a), dx = x - BIRD_XS, dy = y - BIRD_YS; x = BIRD_XS + c * dx - s * dy; y = BIRD_YS + s * dx + c * dy; }
        P[i] = vec3(sg * x, y, z);
    }
    for (size_t t = 0; t + 2 < birdIdx.size(); t += 3) {
        uint32_t a = birdIdx[t], b = birdIdx[t + 1], c = birdIdx[t + 2];
        vec3 fn = cross(P[b] - P[a], P[c] - P[a]); N[a] += fn; N[b] += fn; N[c] += fn;
    }
    for (size_t i = 0; i < n; i++) { Vtx v = birdRest[i]; v.px = P[i].x; v.py = P[i].y; v.pz = P[i].z; v.n = packN(N[i]); out[i] = v; }
}

// ------------------------------------------------------------------ Welt
float World::height(float x, float z) const {
    float dx = std::max(std::fabs(x) - CITY_X, 0.f), dz = std::max(std::fabs(z) - CITY_Z, 0.f);
    float d = std::sqrt(dx * dx + dz * dz);
    float a = smoothstepf(15.f, 420.f, d);
    float west = smoothstepf(-350.f, -1100.f, x);
    float hills = fbm2(x * 0.0022f + 3.1f, z * 0.0022f + 7.7f, 5);
    float h = a * (std::max(hills - 0.3f, 0.f) * 130.f * (0.35f + 0.65f * west) + fbm2(x * 0.012f, z * 0.012f, 3) * 6.f + 1.5f);
    float r = std::sqrt(x * x + z * z), m = smoothstepf(1700.f, 4200.f, r);
    h += m * fbm2(x * 0.0007f + 5.f, z * 0.0007f + 9.f, 5) * 520.f;
    float fl = (1.f - smoothstepf(30.f, 120.f, std::fabs(x - bridgeX))) * smoothstepf(520.f, 560.f, z) * (1.f - smoothstepf(930.f, 1080.f, z));
    h = mixf(h, 0.f, fl);
    float dr = std::fabs(z - riverZ(x)), rm = 1.f - smoothstepf(40.f, 100.f, dr);
    h = mixf(h, -4.5f + vnoise(x * 0.05f, z * 0.05f) * 1.5f, rm);
    return h;
}

static float forestMask(float x, float z) {
    float e = x + 70.f * std::sin(z * 0.011f) + (fbm2(x * 0.006f, z * 0.006f, 3) - 0.45f) * 260.f;
    float f = smoothstepf(-500.f, -640.f, e);
    float g = smoothstepf(0.50f, 0.60f, fbm2(x * 0.0035f + 40.f, z * 0.0035f + 70.f, 3));
    float dx = std::max(std::fabs(x) - CITY_X, 0.f), dz = std::max(std::fabs(z) - CITY_Z, 0.f);
    float out = smoothstepf(30.f, 140.f, std::sqrt(dx * dx + dz * dz));
    return std::max(f, g * out * 0.8f);
}

namespace {
struct Gen {
    World& w; RNG rng; float density;
    std::vector<uint8_t> cityMap; // 1 m Raster: 0 Strasse, 1 Block, 2 Gebaeude
    int mw = 1160, mh = 1080;
    std::vector<int> decid, conif, dead, bush, grass; int car[2], lamp;
    struct Street { bool vertical; float c, w, a, b; };
    std::vector<Street> streets;
    struct Lot { float x0, z0, x1, z1; };
    std::vector<Lot> emptyLots;

    Gen(World& w_, float d, uint32_t seed) : w(w_), rng(seed), density(d) { cityMap.assign(size_t(mw) * mh, 0); }
    MeshB& chunkAt(float x, float z) {
        int cx = (int)clampf(std::floor((x - FX0) / CH), 0, CNX - 1), cz = (int)clampf(std::floor((z - FZ0) / CH), 0, CNZ - 1);
        return w.chunks[cz * CNX + cx];
    }
    void mark(float x0, float z0, float x1, float z1, uint8_t v) {
        int a = std::max(0, int(x0 + mw / 2)), b = std::min(mw - 1, int(x1 + mw / 2)), c = std::max(0, int(z0 + mh / 2)), d = std::min(mh - 1, int(z1 + mh / 2));
        for (int z = c; z <= d; z++) for (int x = a; x <= b; x++) cityMap[size_t(z) * mw + x] = v;
    }
    int at(float x, float z) const { int ix = int(std::floor(x + mw / 2)), iz = int(std::floor(z + mh / 2)); if (ix < 0 || iz < 0 || ix >= mw || iz >= mh) return -1; return cityMap[size_t(iz) * mw + ix]; }
    void inst(int mesh, vec3 p, float yaw, float s, float tiltX = 0, float tiltZ = 0) {
        xform x; x.r = rotY(yaw) * rotX(tiltX) * rotZ(tiltZ) * scaleM(s); x.t = p;
        Inst i; x.toRows(i.m); i.mesh = (uint32_t)mesh; w.insts.push_back(i);
    }

    // ---- Fassadenwand mit Efeu
    void wall(MeshB& m, vec3 a, vec3 dir, vec3 n, float L, float y0, float y1, float base, uint32_t col, uint32_t mat, float bay, int wallIdx, float seed, float level, bool ivy, float ivyOfs = 0) {
        int nb = std::max(1, (int)std::lround(L / bay));
        float u0 = bay * 64.f * wallIdx, u1 = u0 + nb * bay;
        vec3 A = a + vec3(0, y0, 0), B = a + dir * L + vec3(0, y0, 0), up(0, y1 - y0, 0);
        quad(m, A, B, B + up, A + up, n, col, mat, u0, y0 - base, u1, y1 - base);
        if (!ivy || density <= 0) return;
        const float st = 0.85f;
        for (float s = 0.2f; s < L - 0.2f; s += st) for (float y = y0 + 0.2f; y < y1; y += st) {
            float ss = s + rng.r(-0.4f, 0.4f), yy = y + rng.r(-0.4f, 0.4f);
            float u = u0 + ss / L * (u1 - u0), v = yy - base;
            float mk = ivyMask(u, v, seed, level);
            if (rng.f() > mk * mk * 1.1f * std::min(density, 1.f)) continue;
            vec3 c = a + dir * ss + vec3(0, yy, 0) + n * (ivyOfs + rng.r(0.06f, 0.3f));
            float ph = rng.r(0, 2 * PI), sz = rng.r(0.5f, 0.8f);
            vec3 upv(0, 1, 0);
            vec3 ax = (dir * std::cos(ph) + upv * std::sin(ph)) * sz, ay = (dir * -std::sin(ph) + upv * std::cos(ph) + n * rng.r(-0.35f, 0.35f)) * sz;
            vec3 col2 = vec3(0.045f, 0.11f, 0.035f) * rng.r(0.6f, 1.5f); if (rng.f() < 0.07f) col2 = vec3(0.22f, 0.07f, 0.03f) * rng.r(0.7f, 1.2f);
            leafCard(m, c, ax, ay, normalize(n + rng.unit() * 0.45f + vec3(0, 0.3f, 0)), packC(col2));
        }
    }
    void tier(MeshB& m, float x0, float z0, float x1, float z1, float y0, float y1, float base, uint32_t col, uint32_t mat, float bay, float seed, float level, int wofs) {
        wall(m, vec3(x1, 0, z0), vec3(-1, 0, 0), vec3(0, 0, -1), x1 - x0, y0, y1, base, col, mat, bay, wofs + 0, seed, level, true);
        wall(m, vec3(x0, 0, z1), vec3(1, 0, 0), vec3(0, 0, 1), x1 - x0, y0, y1, base, col, mat, bay, wofs + 1, seed, level, true);
        wall(m, vec3(x0, 0, z0), vec3(0, 0, 1), vec3(-1, 0, 0), z1 - z0, y0, y1, base, col, mat, bay, wofs + 2, seed, level, true);
        wall(m, vec3(x1, 0, z1), vec3(0, 0, -1), vec3(1, 0, 0), z1 - z0, y0, y1, base, col, mat, bay, wofs + 3, seed, level, true);
        w.boxes.push_back({vec3(x0, y0, z0), vec3(x1, y1, z1)});
    }
    void roof(MeshB& m, float x0, float z0, float x1, float z1, float y, float level, bool props) {
        uint32_t rc = packC(vec3(0.11f, 0.105f, 0.1f) * rng.r(0.8f, 1.3f));
        quad(m, vec3(x0, y, z0), vec3(x0, y, z1), vec3(x1, y, z1), vec3(x1, y, z0), vec3(0, 1, 0), rc, matp(M_ROOF, 0, 0, uint32_t(level * 255)), z0, x0, z1, x1);
        uint32_t pc = packC(vec3(0.3f, 0.29f, 0.27f)); float t = 0.35f, ph = 0.9f;
        box(m, vec3(x0, y, z0), vec3(x1, y + ph, z0 + t), pc, matp(M_CONCRETE), matp(M_CONCRETE), pc);
        box(m, vec3(x0, y, z1 - t), vec3(x1, y + ph, z1), pc, matp(M_CONCRETE), matp(M_CONCRETE), pc);
        box(m, vec3(x0, y, z0 + t), vec3(x0 + t, y + ph, z1 - t), pc, matp(M_CONCRETE), matp(M_CONCRETE), pc);
        box(m, vec3(x1 - t, y, z0 + t), vec3(x1, y + ph, z1 - t), pc, matp(M_CONCRETE), matp(M_CONCRETE), pc);
        if (!props) return;
        float wx = x1 - x0, wz = z1 - z0;
        int np = rng.i(1, 4);
        for (int i = 0; i < np; i++) {
            float sx = rng.r(1.5f, std::min(5.f, wx * 0.3f)), sz = rng.r(1.5f, std::min(5.f, wz * 0.3f)), sy = rng.r(1.2f, 3.2f);
            float cx = rng.r(x0 + 2 + sx, x1 - 2 - sx), cz = rng.r(z0 + 2 + sz, z1 - 2 - sz);
            if (cx - sx < x0 + 1 || cx + sx > x1 - 1 || cz - sz < z0 + 1 || cz + sz > z1 - 1) continue;
            bool metal = rng.f() < 0.6f; uint32_t c = packC(metal ? vec3(0.3f, 0.32f, 0.33f) * rng.r(0.6f, 1.2f) : vec3(0.33f, 0.31f, 0.28f));
            uint32_t mt = metal ? matp(M_METAL) : matp(M_CONCRETE);
            box(m, vec3(cx - sx, y, cz - sz), vec3(cx + sx, y + sy, cz + sz), c, mt, mt, c);
            w.boxes.push_back({vec3(cx - sx, y, cz - sz), vec3(cx + sx, y + sy, cz + sz)});
        }
        // Dachbegruenung
        int nv = int((wx * wz) / 60.f * level * rng.r(0.3f, 1.6f) * std::min(density, 1.f));
        for (int i = 0; i < nv; i++) {
            vec3 p(rng.r(x0 + 1.5f, x1 - 1.5f), y, rng.r(z0 + 1.5f, z1 - 1.5f));
            float r = rng.f();
            if (r < 0.12f) inst(decid[rng.i(0, (int)decid.size() - 1)], p, rng.r(0, 6.28f), rng.r(0.22f, 0.5f));
            else if (r < 0.5f) inst(bush[rng.i(0, (int)bush.size() - 1)], p, rng.r(0, 6.28f), rng.r(0.7f, 1.6f));
            else inst(grass[rng.i(0, (int)grass.size() - 1)], p, rng.r(0, 6.28f), rng.r(0.8f, 1.5f));
        }
    }

    // ================= Gebaeude-Details (Version 2) =================
    struct Style { int type; uint32_t seedI, pB; float seed, level, bay, fh, base; vec3 wc; uint32_t col, fmat; int relief; float rdepth; bool balcony, fireEsc; int fireWall; vec3 trim; };

    // Quader auf einer Wand: s entlang der Wand, y absolut, d = Abstand von der Wandflaeche (drop senkt die Aussenkante ab)
    void slab(MeshB& m, vec3 a, vec3 dir, vec3 n, float s0, float s1, float y0, float y1, float d0, float d1, uint32_t col, uint32_t mat, float drop = 0) {
        vec3 p[8];
        for (int i = 0; i < 8; i++) p[i] = a + dir * ((i & 1) ? s1 : s0) + vec3(0, ((i & 2) ? y1 : y0) - ((i & 4) ? drop : 0.f), 0) + n * ((i & 4) ? d1 : d0);
        hexa(m, p, col, mat, mat, col, true);
    }
    void balconies(MeshB& m, vec3 a, vec3 dir, vec3 n, int nb, float bw, int j0, int j1, const Style& st) {
        uint32_t c = packC(st.type == 1 ? vec3(0.07f, 0.065f, 0.06f) : st.wc * 0.8f), mt = st.type == 1 ? matp(M_METAL) : matp(M_CONCRETE, st.seedI);
        uint32_t cf = packC(vec3(0.3f, 0.29f, 0.27f)), mf = matp(M_CONCRETE, st.seedI);
        for (int j = std::max(j0, 1); j < j1; j++) for (int k = 0; k < nb; k++) {
            if (((k + st.seedI) % 2) != 0 || (hashu(k * 131u + j * 977u + st.seedI) & 15u) == 0u) continue;
            float s0 = (k + 0.1f) * bw, s1 = (k + 0.9f) * bw, y = st.base + j * st.fh, D = 1.15f;
            slab(m, a, dir, n, s0, s1, y - 0.14f, y, 0, D, cf, mf);
            slab(m, a, dir, n, s0, s1, y, y + 1.0f, D - 0.06f, D, c, mt);
            slab(m, a, dir, n, s0, s0 + 0.06f, y, y + 1.0f, 0, D - 0.06f, c, mt);
            slab(m, a, dir, n, s1 - 0.06f, s1, y, y + 1.0f, 0, D - 0.06f, c, mt);
        }
    }
    void fireEscape(MeshB& m, vec3 a, vec3 dir, vec3 n, int nb, float bw, int j1, const Style& st) {
        uint32_t ic = packC(vec3(0.05f, 0.042f, 0.04f)), im = matp(M_METAL);
        int k = std::max(0, nb / 2 - 1); float s0 = k * bw + 0.15f, s1 = std::min((k + 2) * bw - 0.15f, s0 + 5.5f);
        for (int j = 1; j < j1; j++) {
            float y = st.base + (j + 0.3f) * st.fh - 0.28f;
            slab(m, a, dir, n, s0, s1, y - 0.05f, y, 0.02f, 1.0f, ic, im);
            slab(m, a, dir, n, s0, s1, y + 0.95f, y + 1.0f, 0.96f, 1.0f, ic, im);
            slab(m, a, dir, n, s0, s1, y + 0.47f, y + 0.51f, 0.97f, 1.0f, ic, im);
            for (float sp : {s0, (s0 + s1) * 0.5f - 0.02f, s1 - 0.04f}) slab(m, a, dir, n, sp, sp + 0.04f, y, y + 1.0f, 0.96f, 1.0f, ic, im);
            slab(m, a, dir, n, s0, s0 + 0.04f, y + 0.95f, y + 1.0f, 0, 1.0f, ic, im);
            slab(m, a, dir, n, s1 - 0.04f, s1, y + 0.95f, y + 1.0f, 0, 1.0f, ic, im);
            if (j + 1 < j1) { // Treppenlauf zum naechsten Podest, abwechselnd links/rechts
                bool fl = j & 1; float sa = fl ? s1 - 0.6f : s0 + 0.6f, sb = fl ? s0 + 1.0f : s1 - 1.0f;
                vec3 p[8];
                for (int i = 0; i < 8; i++) { bool hi = i & 1; p[i] = a + dir * (hi ? sb : sa) + vec3(0, (hi ? y + st.fh : y) + ((i & 2) ? 0.07f : 0.f), 0) + n * ((i & 4) ? 0.88f : 0.3f); }
                hexa(m, p, ic, im, im, ic, true);
            }
        }
        float yl = st.base + 1.3f * st.fh - 0.28f;
        slab(m, a, dir, n, s0 + 0.3f, s0 + 0.35f, yl - 2.6f, yl, 0.9f, 0.95f, ic, im);
        slab(m, a, dir, n, s0 + 0.75f, s0 + 0.8f, yl - 2.6f, yl, 0.9f, 0.95f, ic, im);
    }
    // Fassadenrelief passend zum Fensterraster des Shaders (Achsmass bw, Geschosshoehe fh)
    void relief(MeshB& m, vec3 a, vec3 dir, vec3 n, float L, float y0, float y1, const Style& st, int wallIdx, bool top, bool bottom) {
        int nb = std::max(1, (int)std::lround(L / st.bay)); float bw = L / nb, fh = st.fh;
        int j0 = (int)std::lround((y0 - st.base) / fh), j1 = (int)std::lround((y1 - st.base) / fh);
        auto Y = [&](float j) { return st.base + j * fh; };
        uint32_t cm = matp(M_CONCRETE, st.seedI);
        if (st.type == 0) { // Beton: Lisenen, Geschossbaender, Attika
            uint32_t c2 = packC(st.wc * 0.92f); float d = st.rdepth;
            if (st.relief == 0 || st.relief == 2) for (int k = 0; k <= nb; k++) slab(m, a, dir, n, k * bw - 0.2f, k * bw + 0.2f, y0, y1, 0, d, c2, cm);
            if (st.relief == 0 || st.relief == 1) for (int j = j0 + 1; j <= j1; j++) slab(m, a, dir, n, -d, L + d, Y(float(j)) - 0.2f, Y(float(j)) + 0.14f, 0, d * 0.85f, c2, cm);
            if (top) slab(m, a, dir, n, -d - 0.2f, L + d + 0.2f, y1 - 0.4f, y1 + 0.12f, 0, d + 0.22f, c2, cm);
            if (bottom) {
                slab(m, a, dir, n, -0.12f, L + 0.12f, y0, y0 + 0.5f, 0, d + 0.06f, packC(st.wc * 0.7f), cm);
                if (L > 12 && rng.f() < 0.4f) { float c = L * rng.r(0.3f, 0.7f); slab(m, a, dir, n, c - 2.2f, c + 2.2f, Y(0.84f), Y(0.84f) + 0.25f, 0, 2.4f, c2, cm); }
            }
            if (st.balcony) balconies(m, a, dir, n, nb, bw, j0, j1, st);
        } else if (st.type == 1) { // Ziegel: Fensterbaenke, Stuerze, Gesims, Balkone oder Feuertreppe, Markisen
            uint32_t tc = packC(st.trim);
            for (int j = std::max(j0, 1); j < j1; j++) for (int k = 0; k < nb; k++) {
                slab(m, a, dir, n, (k + 0.24f) * bw, (k + 0.76f) * bw, Y(j + 0.3f) - 0.13f, Y(j + 0.3f), 0, 0.13f, tc, cm);
                slab(m, a, dir, n, (k + 0.25f) * bw, (k + 0.75f) * bw, Y(j + 0.82f), Y(j + 0.82f) + 0.22f, 0, 0.05f, tc, cm);
            }
            if (bottom && j1 > 1) slab(m, a, dir, n, -0.1f, L + 0.1f, Y(1) - 0.15f, Y(1) + 0.12f, 0, 0.14f, tc, cm);
            if (top) { slab(m, a, dir, n, -0.38f, L + 0.38f, y1 - 0.5f, y1 + 0.12f, 0, 0.42f, tc, cm); slab(m, a, dir, n, -0.2f, L + 0.2f, y1 - 0.85f, y1 - 0.5f, 0, 0.2f, tc, cm); }
            if (st.balcony) balconies(m, a, dir, n, nb, bw, j0, j1, st);
            if (st.fireEsc && wallIdx == st.fireWall && nb >= 3 && j0 == 0) fireEscape(m, a, dir, n, nb, bw, j1, st);
            if (bottom) for (int k = 0; k < nb; k++) if (rng.f() < 0.2f) {
                const vec3 pal[4] = {{0.3f, 0.07f, 0.05f}, {0.06f, 0.17f, 0.1f}, {0.07f, 0.1f, 0.22f}, {0.4f, 0.36f, 0.26f}};
                slab(m, a, dir, n, (k + 0.06f) * bw, (k + 0.94f) * bw, Y(0.8f), Y(0.8f) + 0.05f, 0, 1.3f, packC(pal[rng.i(0, 3)] * rng.r(0.6f, 1.f)), matp(M_METAL, 1), 0.5f);
            }
        } else { // Glasturm: Aluminium-Lisenen, Deckleisten, Vordach
            uint32_t ac = packC(st.trim), am = matp(M_METAL, 2);
            int stepk = st.relief == 1 ? 2 : 1;
            if (st.relief != 2) for (int k = 0; k <= nb; k += stepk) slab(m, a, dir, n, k * bw - 0.04f, k * bw + 0.04f, y0, y1, 0, st.rdepth, ac, am);
            if (st.relief == 1) for (int j = j0 + 1; j <= j1; j++) slab(m, a, dir, n, 0, L, Y(float(j)) - 0.06f, Y(float(j)) + 0.06f, 0, st.rdepth * 0.6f, ac, am);
            if (st.relief == 2) { slab(m, a, dir, n, -0.25f, 0.25f, y0, y1, 0, 0.25f, ac, am); if (top) slab(m, a, dir, n, 0, L, y1 - 0.3f, y1 + 0.1f, 0, 0.2f, ac, am); }
            if (bottom && L > 14 && rng.f() < 0.5f) { float c = L * rng.r(0.35f, 0.65f); slab(m, a, dir, n, c - 3.f, c + 3.f, Y(0.9f), Y(0.9f) + 0.18f, 0, 3.0f, ac, am); }
        }
    }
    void tier2(MeshB& m, float x0, float z0, float x1, float z1, float y0, float y1, const Style& st, int wofs, bool top, bool bottom) {
        struct WS { vec3 a, dir, n; float L; } ws[4] = {
            {vec3(x1, 0, z0), vec3(-1, 0, 0), vec3(0, 0, -1), x1 - x0}, {vec3(x0, 0, z1), vec3(1, 0, 0), vec3(0, 0, 1), x1 - x0},
            {vec3(x0, 0, z0), vec3(0, 0, 1), vec3(-1, 0, 0), z1 - z0}, {vec3(x1, 0, z1), vec3(0, 0, -1), vec3(1, 0, 0), z1 - z0}};
        float ivo = st.type == 0 ? ((st.relief == 0 || st.relief == 2) ? st.rdepth : 0.05f) : (st.type == 1 ? 0.1f : 0.04f);
        for (int i = 0; i < 4; i++) {
            wall(m, ws[i].a, ws[i].dir, ws[i].n, ws[i].L, y0, y1, st.base, st.col, st.fmat, st.bay, wofs + i, st.seed, st.level, true, ivo);
            relief(m, ws[i].a, ws[i].dir, ws[i].n, ws[i].L, y0, y1, st, i, top, bottom);
        }
        w.boxes.push_back({vec3(x0, y0, z0), vec3(x1, y1, z1)});
    }
    // Satteldach fuer niedrige Ziegelbauten
    void gable(MeshB& m, float x0, float z0, float x1, float z1, float y, const Style& st) {
        bool alongX = (x1 - x0) >= (z1 - z0);
        float ov = 0.45f, half = (alongX ? (z1 - z0) : (x1 - x0)) * 0.5f, rh = half * 2 * rng.r(0.26f, 0.4f), drop = rh * ov / half;
        vec3 tc = rng.f() < 0.6f ? vec3(0.15f, 0.06f, 0.042f) * rng.r(0.7f, 1.2f) : vec3(0.07f, 0.075f, 0.085f) * rng.r(0.8f, 1.3f);
        uint32_t rc = packC(tc), rm = matp(M_ROOF, 0, 0, uint32_t(st.level * 200)), gm = matp(M_CONCRETE, st.seedI);
        auto P = [&](float along, float across, float yy) { return alongX ? vec3(along, yy, across) : vec3(across, yy, along); };
        float a0 = (alongX ? x0 : z0) - ov, a1 = (alongX ? x1 : z1) + ov, c0 = alongX ? z0 : x0, c1 = alongX ? z1 : x1, cm = (c0 + c1) * 0.5f;
        vec3 n0 = alongX ? vec3(0, half, -rh) : vec3(-rh, half, 0), n1 = alongX ? vec3(0, half, rh) : vec3(rh, half, 0);
        quad(m, P(a0, c0 - ov, y - drop), P(a1, c0 - ov, y - drop), P(a1, cm, y + rh), P(a0, cm, y + rh), normalize(n0), rc, rm);
        quad(m, P(a1, c1 + ov, y - drop), P(a0, c1 + ov, y - drop), P(a0, cm, y + rh), P(a1, cm, y + rh), normalize(n1), rc, rm);
        float g0 = alongX ? x0 : z0, g1 = alongX ? x1 : z1;
        tri(m, P(g0, c0, y), P(g0, c1, y), P(g0, cm, y + rh), alongX ? vec3(-1, 0, 0) : vec3(0, 0, -1), st.col, gm);
        tri(m, P(g1, c1, y), P(g1, c0, y), P(g1, cm, y + rh), alongX ? vec3(1, 0, 0) : vec3(0, 0, 1), st.col, gm);
        // Schornstein
        float ca = mixf(g0, g1, rng.r(0.2f, 0.8f)), cc = cm + half * rng.r(-0.5f, 0.5f);
        vec3 q0 = P(ca - 0.45f, cc - 0.35f, y), q1 = P(ca + 0.45f, cc + 0.35f, y + rh + 0.9f);
        box(m, vec3(std::min(q0.x, q1.x), y, std::min(q0.z, q1.z)), vec3(std::max(q0.x, q1.x), y + rh + 0.9f, std::max(q0.z, q1.z)), packC(st.wc * 0.8f), gm, gm, packC(vec3(0.03f)));
        vec3 b0 = P(g0, c0, y), b1 = P(g1, c1, y + rh * 0.45f), b2 = P(g0, cm - half * 0.45f, y), b3 = P(g1, cm + half * 0.45f, y + rh * 0.9f);
        w.boxes.push_back({vec3(std::min(b0.x, b1.x), y, std::min(b0.z, b1.z)), vec3(std::max(b0.x, b1.x), b1.y, std::max(b0.z, b1.z))});
        w.boxes.push_back({vec3(std::min(b2.x, b3.x), y, std::min(b2.z, b3.z)), vec3(std::max(b2.x, b3.x), b3.y, std::max(b2.z, b3.z))});
    }
    // Flachdach mit Aufbauten: Wassertank, Klimageraete, Treppenhaus, Technik-Krone bei Glastuermen
    void roof2(MeshB& m, float x0, float z0, float x1, float z1, float y, const Style& st, bool topRoof, float h) {
        roof(m, x0, z0, x1, z1, y, st.level, topRoof);
        if (!topRoof) return;
        float wx = x1 - x0, wz = z1 - z0;
        if (st.type != 2 && wx > 9 && wz > 9 && rng.f() < 0.5f) { // Wassertank auf Stelzen
            float r = rng.r(1.1f, 1.6f), cx = rng.r(x0 + 3, x1 - 3), cz = rng.r(z0 + 3, z1 - 3), hl = rng.r(1.4f, 2.2f), ht = rng.r(2.2f, 3.2f);
            uint32_t ic = packC(vec3(0.06f, 0.05f, 0.045f)), im = matp(M_METAL);
            for (int i = 0; i < 4; i++) { float lx = cx + ((i & 1) ? 0.75f : -0.75f) * r, lz = cz + ((i & 2) ? 0.75f : -0.75f) * r; box(m, vec3(lx - 0.07f, y, lz - 0.07f), vec3(lx + 0.07f, y + hl, lz + 0.07f), ic, im, im, ic); }
            frustum(m, vec3(cx, y + hl, cz), vec3(cx, y + hl + ht, cz), r, r * 0.94f, 12, packC(vec3(0.2f, 0.13f, 0.08f) * rng.r(0.7f, 1.2f)), matp(M_BARK));
            frustum(m, vec3(cx, y + hl + ht, cz), vec3(cx, y + hl + ht + r * 0.5f, cz), r * 1.04f, 0.05f, 12, packC(vec3(0.16f, 0.15f, 0.14f)), matp(M_METAL));
            w.boxes.push_back({vec3(cx - r, y, cz - r), vec3(cx + r, y + hl + ht + r * 0.5f, cz + r)});
        }
        if (wx > 8 && wz > 8 && rng.f() < 0.7f) { // Reihe Klimageraete
            int nA = rng.i(2, 6); bool ax = rng.f() < 0.5f; float cx = rng.r(x0 + 2, x1 - 2), cz = rng.r(z0 + 2, z1 - 2);
            uint32_t c = packC(vec3(0.34f, 0.35f, 0.34f) * rng.r(0.7f, 1.1f)), mt = matp(M_METAL);
            for (int i = 0; i < nA; i++) { float px = cx + (ax ? i * 1.5f : 0.f), pz = cz + (ax ? 0.f : i * 1.5f); if (px + 0.6f > x1 - 1 || pz + 0.6f > z1 - 1) break;
                box(m, vec3(px - 0.5f, y + 0.25f, pz - 0.45f), vec3(px + 0.5f, y + 1.15f, pz + 0.45f), c, mt, mt, c, true);
                box(m, vec3(px - 0.4f, y, pz - 0.35f), vec3(px + 0.4f, y + 0.25f, pz + 0.35f), packC(vec3(0.05f)), mt, mt, packC(vec3(0.05f))); }
        }
        if (st.type != 2 && wx > 10 && wz > 10 && rng.f() < 0.6f) { // Treppenhaus-Aufbau
            float cx = rng.r(x0 + 3.5f, x1 - 3.5f), cz = rng.r(z0 + 3.5f, z1 - 3.5f);
            box(m, vec3(cx - 1.6f, y, cz - 2.2f), vec3(cx + 1.6f, y + 2.7f, cz + 2.2f), packC(st.wc * 0.9f), matp(M_CONCRETE, st.seedI), matp(M_ROOF, 0, 0, uint32_t(st.level * 255)), packC(vec3(0.1f)));
            box(m, vec3(cx - 0.5f, y, cz + 2.2f), vec3(cx + 0.5f, y + 2.05f, cz + 2.26f), packC(vec3(0.07f, 0.08f, 0.09f)), matp(M_METAL), matp(M_METAL), packC(vec3(0.07f)));
            w.boxes.push_back({vec3(cx - 1.6f, y, cz - 2.2f), vec3(cx + 1.6f, y + 2.7f, cz + 2.2f)});
        }
        if (st.type == 2 && wx > 12 && wz > 12) { // Technik-Krone: zurueckgesetzter Lamellen-Schirm
            float in = rng.r(1.2f, 2.4f), hc = rng.r(2.5f, 5.5f), t = 0.18f; uint32_t ac = packC(st.trim * 0.8f), am = matp(M_METAL, 2);
            float a0 = x0 + in, a1 = x1 - in, b0 = z0 + in, b1 = z1 - in;
            Box bs[4] = {{vec3(a0, y, b0), vec3(a1, y + hc, b0 + t)}, {vec3(a0, y, b1 - t), vec3(a1, y + hc, b1)}, {vec3(a0, y, b0), vec3(a0 + t, y + hc, b1)}, {vec3(a1 - t, y, b0), vec3(a1, y + hc, b1)}};
            for (auto& b : bs) { box(m, b.mn, b.mx, ac, am, am, ac); w.boxes.push_back(b); }
            if (h > 80 && rng.f() < 0.55f) { float cx = (x0 + x1) / 2, cz = (z0 + z1) / 2, hs = rng.r(14, 34);
                frustum(m, vec3(cx, y, cz), vec3(cx, y + hs * 0.4f, cz), 0.55f, 0.3f, 6, ac, am); frustum(m, vec3(cx, y + hs * 0.4f, cz), vec3(cx, y + hs, cz), 0.22f, 0.04f, 5, ac, am); }
        }
    }
    // Ein Baukoerper: gestaffelte Geschosse (Setbacks) mit Relief und Dach
    void mass(MeshB& m, float x0, float z0, float x1, float z1, int nf, const Style& st, int wofs, bool tiers) {
        float h = nf * st.fh;
        int nt = (tiers && h > 45 && rng.f() < 0.65f) ? rng.i(2, 3) : 1;
        float ya = st.base, X0 = x0, Z0 = z0, X1 = x1, Z1 = z1;
        for (int t = 0; t < nt; t++) {
            bool last = (t == nt - 1);
            int fl = last ? nf : std::max(1, (int)(nf * (rng.r(0.45f, 0.62f) + 0.25f * t)));
            float yb = st.base + fl * st.fh; if (yb <= ya + st.fh) yb = ya + st.fh;
            float in = rng.r(2.f, 5.f);
            bool can = !last && (X1 - X0 - 2 * in > 9) && (Z1 - Z0 - 2 * in > 9), top = last || !can;
            tier2(m, X0, Z0, X1, Z1, ya, yb, st, wofs + t * 4, top, t == 0);
            bool pitched = top && st.type == 1 && (yb - st.base) < 20 && std::min(X1 - X0, Z1 - Z0) < 26 && rng.f() < 0.6f;
            if (pitched) gable(m, X0, Z0, X1, Z1, yb, st); else roof2(m, X0, Z0, X1, Z1, yb, st, top, h);
            if (top) break;
            X0 += in * rng.r(0.3f, 1.f); X1 -= in * rng.r(0.3f, 1.f); Z0 += in * rng.r(0.3f, 1.f); Z1 -= in * rng.r(0.3f, 1.f);
            ya = yb;
        }
    }

    void building(float x0, float z0, float x1, float z1, float h, int type, bool ruin) {
        float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
        MeshB& m = chunkAt(cx, cz);
        uint32_t seedI = rng.i(0, 255), pB = rng.i(0, 15);
        float seed = float(seedI);
        float level = clampf(0.2f + overgrow(cx, cz) * 0.45f + rng.r(0, 0.35f), 0, 1);
        float bay = type == 2 ? 1.5f + 0.3f * (pB & 3) : 2.4f + 0.5f * (pB & 3);
        float fh = 3.0f + 0.3f * ((pB >> 2) & 3);
        int nf = std::max(2, (int)std::lround(h / fh)); h = nf * fh;
        vec3 wc;
        if (type == 0) { float g = rng.r(0.26f, 0.5f); wc = vec3(g, g * rng.r(0.95f, 1.0f), g * rng.r(0.86f, 0.98f)); }
        else if (type == 1) { wc = rng.f() < 0.7f ? vec3(0.30f, 0.13f, 0.09f) * rng.r(0.7f, 1.3f) : vec3(0.42f, 0.34f, 0.2f) * rng.r(0.7f, 1.1f); }
        else { wc = mix(vec3(0.10f, 0.16f, 0.19f), vec3(0.16f, 0.17f, 0.15f), rng.f()) * rng.r(0.7f, 1.3f); }
        uint32_t col = packC(wc);
        uint32_t fmat = matp(type == 0 ? M_FAC_CONCRETE : (type == 1 ? M_FAC_BRICK : M_FAC_GLASS), seedI, pB, uint32_t(level * 255));
        float base = GROUND;
        mark(x0, z0, x1, z1, 2);
        if (!ruin) {
            {   // Den Zufallsstrom exakt wie in Version 1 weiterdrehen (Ergebnis wird verworfen), damit Grundriss,
                // Hoehen und die gesamte Vegetation der Stadt unveraendert bleiben. Die neuen Gebaeude-Details
                // benutzen danach einen eigenen, pro Gebaeude abgeleiteten Generator.
                MeshB scratch; size_t nb0 = w.boxes.size(), ni0 = w.insts.size();
            int nt = (h > 45 && rng.f() < 0.65f) ? rng.i(2, 3) : 1;
                float ya = base, X0 = x0, Z0 = z0, X1 = x1, Z1 = z1;
                for (int t = 0; t < nt; t++) {
                    bool last = (t == nt - 1);
                    int fl = last ? nf : std::max(1, (int)(nf * (rng.r(0.45f, 0.62f) + 0.25f * t)));
                    float yb = base + fl * fh; if (yb <= ya + fh) yb = ya + fh;
                    tier(scratch, X0, Z0, X1, Z1, ya, yb, base, col, fmat, bay, seed, level, t * 4);
                    float in = rng.r(2.f, 5.f);
                    bool can = !last && (X1 - X0 - 2 * in > 9) && (Z1 - Z0 - 2 * in > 9);
                    roof(scratch, X0, Z0, X1, Z1, yb, level, last || !can);
                    if (!can) { if (h > 90 && rng.f() < 0.5f) frustum(scratch, vec3(cx + 2, yb, cz + 2), vec3(cx + 2, yb + rng.r(12, 28), cz + 2), 0.25f, 0.06f, 5, packC(vec3(0.3f)), matp(M_METAL)); break; }
                    X0 += in * rng.r(0.3f, 1.f); X1 -= in * rng.r(0.3f, 1.f); Z0 += in * rng.r(0.3f, 1.f); Z1 -= in * rng.r(0.3f, 1.f);
                    ya = yb;
                }
                w.boxes.resize(nb0); w.insts.resize(ni0);
            }
            RNG saved = rng; rng = RNG(seedI * 7919u + uint32_t(int(cx * 13.f)) * 31u + uint32_t(int(cz * 7.f)));
            Style st{}; st.type = type; st.seedI = seedI; st.pB = pB; st.seed = seed; st.level = level; st.bay = bay; st.fh = fh; st.base = base; st.wc = wc; st.col = col; st.fmat = fmat;
            st.rdepth = type == 2 ? rng.r(0.12f, 0.4f) : rng.r(0.22f, 0.42f);
            if (type == 0) { st.relief = rng.i(0, 3); st.balcony = (st.relief == 3 && rng.f() < 0.65f) || (st.relief == 1 && rng.f() < 0.3f); st.trim = wc; }
            else if (type == 1) { st.relief = 0; st.balcony = rng.f() < 0.3f; st.fireEsc = !st.balcony && nf >= 3 && nf <= 11 && rng.f() < 0.55f; st.fireWall = rng.i(0, 3); st.trim = vec3(0.34f, 0.32f, 0.28f) * rng.r(0.75f, 1.1f); }
            else {
                const vec3 pal[5] = {{0.09f, 0.15f, 0.2f}, {0.08f, 0.16f, 0.14f}, {0.2f, 0.14f, 0.08f}, {0.14f, 0.15f, 0.16f}, {0.06f, 0.1f, 0.2f}};
                st.wc = pal[rng.i(0, 4)] * rng.r(0.8f, 1.25f); st.col = packC(st.wc); st.relief = rng.i(0, 2);
                st.trim = rng.f() < 0.5f ? vec3(0.45f, 0.46f, 0.47f) : vec3(0.07f, 0.075f, 0.08f);
            }
            float wx = x1 - x0, wz = z1 - z0;
            if (std::min(wx, wz) > 18 && std::max(wx, wz) > 26 && rng.f() < 0.45f) { // L-foermiger Baukoerper: Haupttrakt + niedrigerer, schmalerer Fluegel
                bool sx = wx >= wz, flip = rng.f() < 0.5f, side = rng.f() < 0.5f; float t = rng.r(0.42f, 0.62f), cut = rng.r(0.25f, 0.45f);
                int nf2 = std::max(2, (int)(nf * rng.r(0.35f, 0.75f)));
                if (sx) { float xm = x0 + wx * (flip ? 1 - t : t), zc0 = side ? z0 : z0 + wz * cut, zc1 = side ? z1 - wz * cut : z1;
                    mass(m, flip ? xm : x0, z0, flip ? x1 : xm, z1, nf, st, 0, true); mass(m, flip ? x0 : xm, zc0, flip ? xm : x1, zc1, nf2, st, 12, false); }
                else { float zm = z0 + wz * (flip ? 1 - t : t), xc0 = side ? x0 : x0 + wx * cut, xc1 = side ? x1 - wx * cut : x1;
                    mass(m, x0, flip ? zm : z0, x1, flip ? z1 : zm, nf, st, 0, true); mass(m, xc0, flip ? z0 : zm, xc1, flip ? zm : z1, nf2, st, 12, false); }
            } else mass(m, x0, z0, x1, z1, nf, st, 0, true);
            rng = saved;
        } else {
            // Ruine: Zellenraster mit zum Einsturz-Eck hin abfallender Hoehe
            int nx = std::max(2, int((x1 - x0) / 5.f)), nz = std::max(2, int((z1 - z0) / 5.f));
            float cw = (x1 - x0) / nx, cd = (z1 - z0) / nz;
            int corner = rng.i(0, 3); float ccx = (corner & 1) ? x1 : x0, ccz = (corner & 2) ? z1 : z0;
            float diag = std::sqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
            float sev = rng.r(0.5f, 1.2f);
            std::vector<float> hh(size_t(nx) * nz);
            for (int j = 0; j < nz; j++) for (int i = 0; i < nx; i++) {
                float px = x0 + (i + 0.5f) * cw, pz = z0 + (j + 0.5f) * cd;
                float d = std::sqrt((px - ccx) * (px - ccx) + (pz - ccz) * (pz - ccz)) / diag;
                float f = clampf(0.08f + d * 1.25f / sev + (vnoise(px * 0.13f + seed, pz * 0.13f) - 0.5f) * 0.5f, 0.06f, 1.f);
                hh[size_t(j) * nx + i] = std::max(1.f, std::floor(nf * f)) * fh;
            }
            auto H = [&](int i, int j) { return (i < 0 || j < 0 || i >= nx || j >= nz) ? 0.f : hh[size_t(j) * nx + i]; };
            uint32_t ic = packC(wc * 0.55f + vec3(0.05f)), imat = matp(M_CONCRETE, seedI, 1);
            for (int j = 0; j < nz; j++) for (int i = 0; i < nx; i++) {
                float a0 = x0 + i * cw, a1 = a0 + cw, b0 = z0 + j * cd, b1 = b0 + cd, hc = H(i, j);
                quad(m, vec3(a0, base + hc, b0), vec3(a0, base + hc, b1), vec3(a1, base + hc, b1), vec3(a1, base + hc, b0), vec3(0, 1, 0), ic, matp(M_ROOF, 0, 0, uint32_t(level * 255)), b0, a0, b1, a1);
                struct S { int di, dj; vec3 a, dir, n; float L; int widx; float uofs; };
                S sides[4] = {
                    {0, -1, vec3(a1, 0, b0), vec3(-1, 0, 0), vec3(0, 0, -1), cw, 0, x1 - a1},
                    {0, 1, vec3(a0, 0, b1), vec3(1, 0, 0), vec3(0, 0, 1), cw, 1, a0 - x0},
                    {-1, 0, vec3(a0, 0, b0), vec3(0, 0, 1), vec3(-1, 0, 0), cd, 2, b0 - z0},
                    {1, 0, vec3(a1, 0, b1), vec3(0, 0, -1), vec3(1, 0, 0), cd, 3, z1 - b1}};
                for (auto& s : sides) {
                    float hn = H(i + s.di, j + s.dj); if (hc <= hn) continue;
                    bool ext = (i + s.di < 0 || j + s.dj < 0 || i + s.di >= nx || j + s.dj >= nz);
                    vec3 A = s.a + vec3(0, base + hn, 0), B = A + s.dir * s.L, up(0, hc - hn, 0);
                    if (ext) {
                        float Lw = (s.widx < 2) ? (x1 - x0) : (z1 - z0); int nb = std::max(1, (int)std::lround(Lw / bay));
                        float us = nb * bay / Lw, u0 = bay * 64.f * s.widx + s.uofs * us;
                        quad(m, A, B, B + up, A + up, s.n, col, fmat, u0, hn, u0 + s.L * us, hc);
                    } else quad(m, A, B, B + up, A + up, s.n, ic, imat, s.uofs, hn, s.uofs + s.L, hc);
                }
                w.boxes.push_back({vec3(a0, base, b0), vec3(a1, base + hc, b1)});
                if (rng.f() < 0.25f * level * std::min(density, 1.f)) inst(rng.f() < 0.3f ? decid[rng.i(0, (int)decid.size() - 1)] : bush[rng.i(0, (int)bush.size() - 1)], vec3(rng.r(a0 + 1, a1 - 1), base + hc, rng.r(b0 + 1, b1 - 1)), rng.r(0, 6.28f), rng.r(0.3f, 0.6f));
            }
            // Schutt
            int nr = rng.i(18, 40);
            for (int i = 0; i < nr; i++) {
                float rr = rng.r(0, 14), an = rng.r(0, 2 * PI);
                vec3 c(ccx + std::cos(an) * rr, base + rng.r(0.1f, 1.6f) * (1 - rr / 16), ccz + std::sin(an) * rr);
                mat3 r = rotY(rng.r(0, 6.28f)) * rotX(rng.r(-0.6f, 0.6f)) * rotZ(rng.r(-0.6f, 0.6f));
                boxR(chunkAt(c.x, c.z), c, vec3(rng.r(0.4f, 2.4f), rng.r(0.15f, 0.7f), rng.r(0.4f, 2.0f)), r, packC(wc * rng.r(0.5f, 0.9f)), matp(M_CONCRETE, seedI));
            }
        }
    }

    void subdivide(float x0, float z0, float x1, float z1, int depth, std::vector<Lot>& out) {
        float wx = x1 - x0, wz = z1 - z0; float maxL = rng.r(26, 48);
        if ((wx <= maxL && wz <= maxL) || depth > 5 || (wx < 30 && wz < 30)) { out.push_back({x0, z0, x1, z1}); return; }
        float t = rng.r(0.38f, 0.62f);
        if (wx > wz) { float xm = x0 + wx * t; subdivide(x0, z0, xm, z1, depth + 1, out); subdivide(xm, z0, x1, z1, depth + 1, out); }
        else { float zm = z0 + wz * t; subdivide(x0, z0, x1, zm, depth + 1, out); subdivide(x0, zm, x1, z1, depth + 1, out); }
    }

    void city() {
        std::vector<float> xs, zs, xw, zw;
        for (float x = -530; x < 500;) { xs.push_back(x); x += rng.r(84, 122); }
        for (float z = -492; z < 465;) { zs.push_back(z); z += rng.r(80, 118); }
        { float e = xs.back() + 530; for (auto& x : xs) x = -530 + (x + 530) * 1060.f / e; }
        { float e = zs.back() + 492; for (auto& z : zs) z = -492 + (z + 492) * 984.f / e; }
        for (size_t i = 0; i < xs.size(); i++) xw.push_back((i % 4 == 2) ? 20.f : 13.f);
        for (size_t i = 0; i < zs.size(); i++) zw.push_back((i % 3 == 1) ? 20.f : 13.f);
        int bi = 0; for (size_t i = 0; i < xs.size(); i++) if (std::fabs(xs[i] - 60) < std::fabs(xs[bi] - 60)) bi = (int)i;
        xw[bi] = 18; w.bridgeX = xs[bi];
        float zA = zs.front() - zw.front() / 2, zB = zs.back() + zw.back() / 2;
        w.cityX0 = xs.front() - xw.front() / 2; w.cityX1 = xs.back() + xw.back() / 2; w.cityZ0 = zA; w.cityZ1 = zB;
        const float ry = 0.04f; uint32_t rc = packC(vec3(0.07f));
        for (size_t i = 0; i < xs.size(); i++) {
            float hw = xw[i] / 2;
            for (float z = zA; z < zB; z += CH) { // in Chunk-Stuecke teilen
                float z2 = std::min(z + CH, zB);
                quad(chunkAt(xs[i], (z + z2) / 2), vec3(xs[i] - hw, ry, z), vec3(xs[i] + hw, ry, z), vec3(xs[i] + hw, ry, z2), vec3(xs[i] - hw, ry, z2), vec3(0, 1, 0), rc, matp(M_ROAD, 0, uint32_t(hw * 4)), -hw, z, hw, z2);
            }
            streets.push_back({true, xs[i], xw[i], zA, zB});
        }
        for (size_t j = 0; j < zs.size(); j++) {
            float hw = zw[j] / 2;
            for (size_t i = 0; i + 1 < xs.size(); i++) {
                float a = xs[i] + xw[i] / 2, b = xs[i + 1] - xw[i + 1] / 2;
                quad(chunkAt((a + b) / 2, zs[j]), vec3(a, ry, zs[j] + hw), vec3(a, ry, zs[j] - hw), vec3(b, ry, zs[j] - hw), vec3(b, ry, zs[j] + hw), vec3(0, 1, 0), rc, matp(M_ROAD, 0, uint32_t(hw * 4)), -hw, a, hw, b);
                streets.push_back({false, zs[j], zw[j], a, b});
            }
        }
        // Bloecke
        for (size_t j = 0; j + 1 < zs.size(); j++) for (size_t i = 0; i + 1 < xs.size(); i++) {
            float x0 = xs[i] + xw[i] / 2, x1 = xs[i + 1] - xw[i + 1] / 2, z0 = zs[j] + zw[j] / 2, z1 = zs[j + 1] - zw[j + 1] / 2;
            uint32_t sc = packC(vec3(0.33f, 0.32f, 0.3f));
            box(chunkAt((x0 + x1) / 2, (z0 + z1) / 2), vec3(x0, -0.2f, z0), vec3(x1, GROUND, z1), sc, matp(M_CONCRETE), matp(M_SIDEWALK), sc);
            mark(x0, z0, x1 - 0.01f, z1 - 0.01f, 1);
            std::vector<Lot> lots; subdivide(x0 + 3, z0 + 3, x1 - 3, z1 - 3, 0, lots);
            for (auto& l : lots) {
                float cx = (l.x0 + l.x1) / 2, cz = (l.z0 + l.z1) / 2;
                float westF = smoothstepf(-250, -520, cx);
                if (rng.f() < 0.10f + 0.3f * westF) { emptyLots.push_back(l); continue; }
                float sb = rng.r(0.4f, 2.4f);
                float bx0 = l.x0 + sb, bz0 = l.z0 + sb, bx1 = l.x1 - sb, bz1 = l.z1 - sb;
                if (bx1 - bx0 < 8 || bz1 - bz0 < 8) { emptyLots.push_back(l); continue; }
                float d = std::sqrt((cx - 40) * (cx - 40) + (cz + 30) * (cz + 30)) / 470.f;
                float hmax = mixf(125.f, 13.f, smoothstepf(0.f, 1.f, d));
                float h = hmax * rng.r(0.4f, 1.15f); if (d < 0.3f && rng.f() < 0.25f) h = rng.r(130, 185);
                h = std::max(h, 7.f);
                int type = h > 55 ? (rng.f() < 0.7f ? 2 : 0) : (h > 22 ? (rng.f() < 0.55f ? 0 : (rng.f() < 0.5f ? 1 : 2)) : (rng.f() < 0.65f ? 1 : 0));
                bool ruin = rng.f() < 0.15f + 0.1f * westF;
                building(bx0, bz0, bx1, bz1, h, type, ruin);
            }
        }
    }

    void terrain() {
        const float cs = 4.f; const int N = int(CH / cs);
        auto tcol = [&](float x, float z, float h) {
            float f = forestMask(x, z);
            vec3 c = mix(vec3(0.085f, 0.115f, 0.04f), vec3(0.06f, 0.062f, 0.03f), f);
            float r = std::sqrt(x * x + z * z); c = mix(c, vec3(0.09f, 0.12f, 0.06f), smoothstepf(1500, 3000, r));
            c = mix(c, vec3(0.2f, 0.2f, 0.19f), smoothstepf(260, 420, h)); // Fels in der Hoehe
            return c;
        };
        auto nrm = [&](float x, float z, float e) { return normalize(vec3(w.height(x - e, z) - w.height(x + e, z), 2 * e, w.height(x, z - e) - w.height(x, z + e))); };
        for (int cz = 0; cz < CNZ; cz++) for (int cx = 0; cx < CNX; cx++) {
            MeshB& m = w.chunks[cz * CNX + cx]; float ox = FX0 + cx * CH, oz = FZ0 + cz * CH;
            std::vector<int> vi(size_t(N + 1) * (N + 1), -1);
            auto V = [&](int i, int j) {
                int& k = vi[size_t(j) * (N + 1) + i];
                if (k < 0) { float x = ox + i * cs, z = oz + j * cs, h = w.height(x, z); k = (int)addV(m, vec3(x, h, z), nrm(x, z, 2.f), x, z, packC(tcol(x, z, h)), matp(M_TERRAIN)); }
                return (uint32_t)k;
            };
            for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
                float x = ox + i * cs, z = oz + j * cs;
                if (x > w.cityX0 + 1 && x + cs < w.cityX1 - 1 && z > w.cityZ0 + 1 && z + cs < w.cityZ1 - 1) continue; // unter der Stadt
                uint32_t a = V(i, j), b = V(i + 1, j), c = V(i + 1, j + 1), d = V(i, j + 1);
                m.io.insert(m.io.end(), {a, d, c, a, c, b});
            }
        }
        // grobes Fernterrain (4 Quadranten)
        const float cc = 64.f; const int R = 80;
        size_t fb = w.chunks.size(); w.chunks.resize(fb + 4);
        for (int j = -R; j < R; j++) for (int i = -R; i < R; i++) {
            float x = i * cc, z = j * cc;
            if (x >= FX0 && x + cc <= FX1 && z >= FZ0 && z + cc <= FZ1) continue;
            MeshB& m = w.chunks[fb + (i < 0 ? 0 : 1) + (j < 0 ? 0 : 2)];
            uint32_t k[4]; int q = 0;
            for (int dj = 0; dj < 2; dj++) for (int di = 0; di < 2; di++) { float px = x + di * cc, pz = z + dj * cc, h = w.height(px, pz);
                // an der Naht zum feinen Gitter leicht absenken, damit keine Luecken sichtbar werden
                bool seam = (px >= FX0 && px <= FX1 && pz >= FZ0 && pz <= FZ1);
                k[q++] = addV(m, vec3(px, h - (seam ? 1.5f : 0.f), pz), nrm(px, pz, 16.f), px, pz, packC(tcol(px, pz, h)), matp(M_TERRAIN)); }
            m.io.insert(m.io.end(), {k[0], k[2], k[3], k[0], k[3], k[1]});
        }
        // Wasser
        for (float x = -5120; x < 5120; x += CH) {
            float zc = w.riverZ(x + CH / 2); float y = w.waterLevel;
            quad(chunkAt(x + CH / 2, zc), vec3(x, y, zc - 190), vec3(x, y, zc + 190), vec3(x + CH, y, zc + 190), vec3(x + CH, y, zc - 190), vec3(0, 1, 0), packC(vec3(0.02f, 0.05f, 0.04f)), matp(M_WATER));
        }
    }

    void bridge() {
        float bx = w.bridgeX, hw = 9; float z0 = 0; for (auto& s : streets) if (s.vertical && s.c == bx) z0 = s.b;
        uint32_t cc = packC(vec3(0.3f, 0.29f, 0.28f)), rc = packC(vec3(0.07f)); uint32_t rm = matp(M_ROAD, 0, uint32_t(hw * 4)), cm = matp(M_CONCRETE);
        float top = 0.45f; float zEnd = 930;
        for (float z = z0; z < zEnd; z += 20) {
            float zm = z + 10; MeshB& m = chunkAt(bx, zm);
            float rz = w.riverZ(bx); bool gap = std::fabs(zm - (rz + 8)) < 22;
            bool tiltA = std::fabs(zm - (rz - 24)) < 10, tiltB = std::fabs(zm - (rz + 40)) < 10;
            if (gap) continue;
            float d0 = 0, d1 = 0; if (tiltA) d1 = 5.5f; if (tiltB) d0 = 5.5f;
            vec3 p[8]; for (int i = 0; i < 8; i++) { float zz = (i & 4) ? z + 20 : z; float drop = (i & 4) ? d1 : d0; p[i] = vec3((i & 1) ? bx + hw : bx - hw, ((i & 2) ? top : top - 1.1f) - drop, zz); }
            hexa(m, p, cc, cm, rm, rc, true);
            if (!tiltA && !tiltB) w.boxes.push_back({vec3(bx - hw, top - 1.1f, z), vec3(bx + hw, top, z + 20)});
            for (int s = -1; s <= 1; s += 2) { vec3 q[8]; for (int i = 0; i < 8; i++) { float zz = (i & 4) ? z + 20 : z; float drop = (i & 4) ? d1 : d0; q[i] = vec3(bx + s * (hw - 0.15f) + ((i & 1) ? 0.15f : -0.15f), ((i & 2) ? top + 1.0f : top) - drop, zz); } hexa(m, q, cc, cm, cm, cc); }
            if (int(z / 20) % 2 == 0 && w.height(bx, zm) < -0.5f) box(m, vec3(bx - 2.2f, -7, zm - 1.5f), vec3(bx + 2.2f, top - 1.1f - std::max(d0, d1) * 0.5f, zm + 1.5f), cc, cm, cm, cc);
        }
    }

    bool free(float x, float z) const { int a = at(x, z); return a != 2; }
    bool inCity(float x, float z) const { return x > w.cityX0 && x < w.cityX1 && z > w.cityZ0 && z < w.cityZ1; }

    void vegetation() {
        auto D = [&](std::vector<int>& v) { return v[rng.i(0, (int)v.size() - 1)]; };
        float dn = std::min(density, 1.f);
        // Wald + Haine
        for (float z = FZ0 + 4; z < FZ1 - 4; z += 7.5f) for (float x = FX0 + 4; x < FX1 - 4; x += 7.5f) {
            float px = x + rng.r(-3.2f, 3.2f), pz = z + rng.r(-3.2f, 3.2f);
            if (inCity(px, pz)) continue;
            float f = forestMask(px, pz); if (rng.f() > f * 0.82f * density) continue;
            float h = w.height(px, pz); if (h < w.waterLevel + 0.5f) continue;
            float cn = vnoise(px * 0.004f + 11, pz * 0.004f + 5); float r = rng.f();
            int mesh = r < 0.04f ? D(dead) : ((rng.f() < (cn > 0.5f ? 0.8f : 0.15f)) ? D(conif) : D(decid));
            inst(mesh, vec3(px, h, pz), rng.r(0, 6.28f), rng.r(0.75f, 1.5f), rng.r(-0.05f, 0.05f), rng.r(-0.05f, 0.05f));
        }
        // Unterholz, Wiesen
        for (float z = FZ0 + 4; z < FZ1 - 4; z += 5.f) for (float x = FX0 + 4; x < FX1 - 4; x += 5.f) {
            float px = x + rng.r(-2.4f, 2.4f), pz = z + rng.r(-2.4f, 2.4f);
            if (inCity(px, pz)) continue;
            float r2 = px * px + pz * pz; if (r2 > 1500.f * 1500.f) continue;
            float h = w.height(px, pz); if (h < w.waterLevel + 0.15f) continue;
            float f = forestMask(px, pz), r = rng.f();
            if (f > 0.5f) { if (r < 0.10f * dn) inst(D(bush), vec3(px, h, pz), rng.r(0, 6.28f), rng.r(0.8f, 1.8f)); }
            else { if (r < 0.42f * dn) inst(D(grass), vec3(px, h, pz), rng.r(0, 6.28f), rng.r(1.2f, 2.4f));
                   else if (r < 0.46f * dn) inst(D(bush), vec3(px, h, pz), rng.r(0, 6.28f), rng.r(0.8f, 2.2f)); }
        }
        // Strassenbaeume und Wildwuchs in der Stadt
        for (auto& s : streets) {
            for (float t = s.a + 6; t < s.b - 6; t += rng.r(9, 16)) for (int side = -1; side <= 1; side += 2) {
                if (rng.f() > 0.5f * dn) continue;
                float o = s.c + side * (s.w / 2 + 1.6f);
                float px = s.vertical ? o : t, pz = s.vertical ? t : o;
                if (!free(px, pz) || at(px, pz) != 1) continue;
                inst(rng.f() < 0.12f ? D(conif) : (rng.f() < 0.07f ? D(dead) : D(decid)), vec3(px, GROUND, pz), rng.r(0, 6.28f), rng.r(0.55f, 1.15f), rng.r(-0.06f, 0.06f), rng.r(-0.06f, 0.06f));
            }
            // Autos
            for (float t = s.a + 10; t < s.b - 10; t += rng.r(18, 70)) {
                bool mid = rng.f() < 0.25f; float o = s.c + (mid ? rng.r(-2.5f, 2.5f) : (rng.f() < 0.5f ? -1.f : 1.f) * (s.w / 2 - 1.4f));
                float px = s.vertical ? o : t, pz = s.vertical ? t : o;
                float yaw = (s.vertical ? 0.f : PI / 2) + (rng.f() < 0.5f ? PI : 0.f) + (mid ? rng.r(-0.9f, 0.9f) : rng.r(-0.08f, 0.08f));
                inst(car[rng.f() < 0.2f ? 1 : 0], vec3(px, 0.04f, pz), yaw, 1.f, 0, rng.f() < 0.1f ? rng.r(-0.12f, 0.12f) : 0.f);
            }
            // Laternen
            int k = 0;
            for (float t = s.a + 14; t < s.b - 8; t += 44, k++) {
                float sd = (k & 1) ? 1.f : -1.f; float o = s.c + sd * (s.w / 2 + 0.7f);
                float px = s.vertical ? o : t, pz = s.vertical ? t : o; if (at(px, pz) != 1) continue;
                float yaw = s.vertical ? (sd > 0 ? PI : 0.f) : (sd > 0 ? PI / 2 : -PI / 2);
                bool fallen = rng.f() < 0.08f;
                inst(lamp, vec3(px, GROUND, pz), yaw + rng.r(-0.15f, 0.15f), 1.f, fallen ? 1.45f : rng.r(-0.12f, 0.12f), rng.r(-0.1f, 0.1f));
            }
        }
        for (float z = w.cityZ0 + 1.5f; z < w.cityZ1 - 1.5f; z += 2.4f) for (float x = w.cityX0 + 1.5f; x < w.cityX1 - 1.5f; x += 2.4f) {
            float px = x + rng.r(-1.1f, 1.1f), pz = z + rng.r(-1.1f, 1.1f);
            int a = at(px, pz); if (a == 2 || a < 0 || !inCity(px, pz)) continue;
            float og = overgrow(px, pz); float r = rng.f();
            float y = a == 1 ? GROUND : 0.04f;
            if (r < (og - 0.25f) * 0.75f * dn) inst(D(grass), vec3(px, y, pz), rng.r(0, 6.28f), rng.r(0.7f, 1.7f));
            else if (r > 0.992f - 0.012f * og && og > 0.4f && dn > 0.3f) inst(D(bush), vec3(px, y, pz), rng.r(0, 6.28f), rng.r(0.7f, 1.8f));
            else if (r > 0.9985f - 0.002f * og && og > 0.6f) inst(D(decid), vec3(px, y, pz), rng.r(0, 6.28f), rng.r(0.3f, 0.85f));
        }
        // Brachflaechen: kleine Stadtwaelder
        for (auto& l : emptyLots) {
            int n = int((l.x1 - l.x0) * (l.z1 - l.z0) / 45.f * dn);
            for (int i = 0; i < n; i++) { float px = rng.r(l.x0 + 1, l.x1 - 1), pz = rng.r(l.z0 + 1, l.z1 - 1); float r = rng.f();
                if (r < 0.3f) inst(r < 0.05f ? D(conif) : D(decid), vec3(px, GROUND, pz), rng.r(0, 6.28f), rng.r(0.5f, 1.2f));
                else if (r < 0.55f) inst(D(bush), vec3(px, GROUND, pz), rng.r(0, 6.28f), rng.r(0.8f, 2.f));
                else inst(D(grass), vec3(px, GROUND, pz), rng.r(0, 6.28f), rng.r(1.f, 2.2f)); }
        }
    }

    void run() {
        w.chunks.clear(); w.chunks.resize(CNX * CNZ);
        // instanzierte Meshes
        vec3 greens[4] = {{0.085f, 0.18f, 0.04f}, {0.11f, 0.2f, 0.045f}, {0.06f, 0.14f, 0.045f}, {0.15f, 0.2f, 0.04f}};
        for (int i = 0; i < 4; i++) { decid.push_back((int)w.meshes.size()); w.meshes.push_back(makeDeciduous(rng, rng.r(14, 21), greens[i], true)); }
        for (int i = 0; i < 3; i++) { conif.push_back((int)w.meshes.size()); w.meshes.push_back(makeConifer(rng, rng.r(17, 25))); }
        for (int i = 0; i < 2; i++) { dead.push_back((int)w.meshes.size()); w.meshes.push_back(makeDeciduous(rng, rng.r(11, 15), vec3(0), false)); }
        for (int i = 0; i < 3; i++) { bush.push_back((int)w.meshes.size()); w.meshes.push_back(makeBush(rng, rng.r(1.0f, 1.5f), greens[i] * 0.9f)); }
        grass.push_back((int)w.meshes.size()); w.meshes.push_back(makeGrass(rng, 1.1f, 0.9f, vec3(0.05f, 0.11f, 0.025f), vec3(0.16f, 0.24f, 0.06f)));
        grass.push_back((int)w.meshes.size()); w.meshes.push_back(makeGrass(rng, 1.2f, 1.25f, vec3(0.06f, 0.11f, 0.03f), vec3(0.3f, 0.3f, 0.1f)));
        grass.push_back((int)w.meshes.size()); w.meshes.push_back(makeGrass(rng, 0.9f, 0.6f, vec3(0.04f, 0.1f, 0.02f), vec3(0.12f, 0.22f, 0.05f)));
        car[0] = (int)w.meshes.size(); w.meshes.push_back(makeCar(rng, 0));
        car[1] = (int)w.meshes.size(); w.meshes.push_back(makeCar(rng, 1));
        lamp = (int)w.meshes.size(); w.meshes.push_back(makeLamp());
        makeBird(w);
        city();
        terrain();
        bridge();
        vegetation();
        w.spawn = vec3(w.bridgeX, 38, 600);
    }
};
} // namespace

void World::generate(float density, uint32_t seed) {
    chunks.clear(); meshes.clear(); insts.clear(); boxes.clear();
    // bridgeX wird in city() gesetzt, height() haengt davon ab -> Stadt zuerst (in run()).
    Gen g(*this, density, seed);
    g.run();
    buildGrid();
}

// ------------------------------------------------------------------ Kollision
void World::buildGrid() {
    gx0 = -700; gz0 = -700; gcell = 32; gnx = 45; gnz = 54; // deckt Stadt + Bruecke ab
    grid.assign(size_t(gnx) * gnz, {});
    for (uint32_t i = 0; i < boxes.size(); i++) {
        const Box& b = boxes[i];
        int a = std::max(0, int((b.mn.x - gx0) / gcell)), c = std::min(gnx - 1, int((b.mx.x - gx0) / gcell));
        int d = std::max(0, int((b.mn.z - gz0) / gcell)), e = std::min(gnz - 1, int((b.mx.z - gz0) / gcell));
        for (int z = d; z <= e; z++) for (int x = a; x <= c; x++) grid[size_t(z) * gnx + x].push_back(i);
    }
}
bool World::insideBox(const vec3& p, float margin) const {
    int cx = int((p.x - gx0) / gcell), cz = int((p.z - gz0) / gcell);
    if (cx < 0 || cz < 0 || cx >= gnx || cz >= gnz) return false;
    for (uint32_t i : grid[size_t(cz) * gnx + cx]) { const Box& b = boxes[i];
        if (p.x > b.mn.x - margin && p.x < b.mx.x + margin && p.y > b.mn.y - margin && p.y < b.mx.y + margin && p.z > b.mn.z - margin && p.z < b.mx.z + margin) return true; }
    return false;
}
bool World::collide(vec3& p, float r, vec3& nOut) const {
    bool hit = false; nOut = vec3(0, 0, 0);
    float gy = std::max(height(p.x, p.z), waterLevel) + r;
    if (p.x > cityX0 && p.x < cityX1 && p.z > cityZ0 && p.z < cityZ1) gy = std::max(gy, 0.2f + r);
    if (p.y < gy) { p.y = gy; nOut = vec3(0, 1, 0); hit = true; }
    for (int pass = 0; pass < 2; pass++) {
        int cx = int((p.x - gx0) / gcell), cz = int((p.z - gz0) / gcell);
        if (cx < 0 || cz < 0 || cx >= gnx || cz >= gnz) break;
        bool any = false;
        for (uint32_t i : grid[size_t(cz) * gnx + cx]) {
            const Box& b = boxes[i];
            vec3 q(clampf(p.x, b.mn.x, b.mx.x), clampf(p.y, b.mn.y, b.mx.y), clampf(p.z, b.mn.z, b.mx.z));
            vec3 d = p - q; float l2 = dot(d, d);
            if (l2 >= r * r) continue;
            vec3 n;
            if (l2 > 1e-8f) { float l = std::sqrt(l2); n = d / l; p = q + n * r; }
            else { // Mittelpunkt im Quader: ueber die naechste Flaeche hinaus
                float dist[6] = {p.x - b.mn.x, b.mx.x - p.x, p.y - b.mn.y, b.mx.y - p.y, p.z - b.mn.z, b.mx.z - p.z};
                int k = 0; for (int j = 1; j < 6; j++) if (dist[j] < dist[k]) k = j;
                n = vec3(k == 0 ? -1.f : (k == 1 ? 1.f : 0.f), k == 2 ? -1.f : (k == 3 ? 1.f : 0.f), k == 4 ? -1.f : (k == 5 ? 1.f : 0.f));
                p += n * (dist[k] + r);
            }
            nOut = n; hit = true; any = true;
        }
        if (!any) break;
    }
    return hit;
}
