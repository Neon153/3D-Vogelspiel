// Kollisionstest: Zufallsfluege durch die Stadt, der Vogel darf nie in einem Quader landen.
#include "../src/world.h"
#include <cstdio>
int main() {
    World w; w.generate(0.0f, 20261111u);
    RNG rng(7); long steps = 0, bad = 0, hits = 0;
    for (int run = 0; run < 400; run++) {
        vec3 p(rng.r(-500, 500), rng.r(3, 60), rng.r(-460, 460)); vec3 n;
        w.collide(p, 0.5f, n); if (w.insideBox(p, -0.01f)) continue; // Start im Gebaeude ueberspringen
        float yaw = rng.r(0, 6.28f), pitch = rng.r(-0.3f, 0.2f);
        for (int i = 0; i < 4000; i++) {
            vec3 f(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
            p += f * (30.f / 60.f); // 30 m/s bei 60 FPS
            if (w.collide(p, 0.5f, n)) { hits++; float dn = dot(f, n); if (dn < 0) { vec3 f2 = normalize(f - n * (dn * 1.08f)); yaw = std::atan2(f2.x, f2.z); pitch = std::asin(clampf(f2.y, -1, 1)); } }
            if (w.insideBox(p, -0.02f)) bad++;
            if (rng.f() < 0.01f) { yaw += rng.r(-1, 1); pitch = rng.r(-0.4f, 0.3f); }
            steps++;
        }
    }
    printf("Schritte %ld, Kollisionen %ld, im Quader %ld, Quader %zu\n", steps, hits, bad, w.boxes.size());
    return bad ? 1 : 0;
}
