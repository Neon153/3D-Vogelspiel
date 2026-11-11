// common.glsl - gemeinsame Uniforms, Hash/Noise (bitgenau wie src/mathx.h)
#define PI 3.14159265359

layout(set = 0, binding = 0, std140) uniform FrameUBO {
    vec4 camPos;      // xyz, w = Zeit (s)
    vec4 camRight;    // * tan(fovx/2)
    vec4 camUp;       // * tan(fovy/2)
    vec4 camFwd;
    vec4 pCamPos;     // Kamera des letzten Frames
    vec4 pCamRight;   // Einheitsvektor / tan(fovx/2)
    vec4 pCamUp;      // Einheitsvektor / tan(fovy/2)
    vec4 pCamFwd;
    vec4 sunDir;
    vec4 sunColor;    // E/pi am Boden (inkl. atmosphaerischer Transmission)
    vec4 moonDir;
    vec4 moonColor;
    vec4 lightDir;    // aktive Richtungslichtquelle (Sonne oder Mond), w = Kegelradius (rad)
    vec4 lightColor;  // rgb = E/pi, w = 1 wenn aktiv
    vec4 sky;         // x Sonnenintensitaet, y Mondintensitaet, z Sternsichtbarkeit, w Bewoelkung
    vec4 fog;         // x Dichte, y Hoehenabfall, z Schritte, w Nahbereich (m)
    vec4 res;         // Renderaufloesung w,h,1/w,1/h
    vec4 outRes;      // Fensteraufloesung
    vec4 jitter;      // xy Subpixel-Jitter, z Adaptionsfaktor der Belichtung
    vec4 birdDelta;   // xyz Bewegung des Vogels in diesem Frame, w Basis-Belichtung
    vec4 misc;        // x Frame, y Belichtungs-Zielwert (Key), z GI-Samples, w max. History
    vec4 misc2;       // x Wasserspiegel, y HUD-Skalierung, z Nachtfaktor, w Wolkenzeit
    vec4 misc3;       // x sRGB-Encode noetig, y HUD sichtbar, z Schaerfe, w Spiegelungen an
} F;

uint hashu(uint x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
float hash1(uint x) { return float(hashu(x)) * (1.0 / 4294967296.0); }
float hash2i(ivec2 p) { return hash1(uint(p.x) * 0x9E3779B1U ^ hashu(uint(p.y) + 0x7F4A7C15U)); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    ivec2 ip = ivec2(i);
    float a = hash2i(ip), b = hash2i(ip + ivec2(1, 0)), c = hash2i(ip + ivec2(0, 1)), d = hash2i(ip + ivec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float fbm2(vec2 p, int oct) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < oct; i++) { s += a * vnoise(p); p = p * 2.03 + vec2(17.3, 9.1); a *= 0.5; }
    return s;
}
float overgrow(vec2 p) {
    float n = fbm2(p * 0.03, 3);
    float w = clamp((200.0 - p.x) / 900.0, 0.0, 1.0);
    return smoothstep(0.38, 0.60, n + w * 0.22);
}
float ivyMask(float u, float v, float seed, float level) {
    float n = fbm2(vec2(u * 0.07 + seed * 1.37, v * 0.07 + seed * 0.73), 3);
    float g = n + 0.30 * exp(-v * 0.05) + (level - 0.5) * 0.45;
    return smoothstep(0.58, 0.70, g);
}
vec3 octDec(uint e) {
    vec2 f = vec2(float(e & 0xFFFFu), float(e >> 16)) * (2.0 / 65535.0) - 1.0;
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t; n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
vec2 octEnc(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 o = n.xy;
    if (n.z < 0.0) o = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return o;
}
vec3 octDecF(vec2 f) {
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t; n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
vec2 skyUV(vec3 d) { return vec2(atan(d.z, d.x) * (0.5 / PI) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI); }
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
// Projektion eines Weltpunkts in die UV-Koordinaten des letzten Frames
vec2 projPrev(vec3 P) {
    vec3 v = P - F.pCamPos.xyz;
    float z = max(dot(v, F.pCamFwd.xyz), 1e-4);
    return vec2(dot(v, F.pCamRight.xyz), -dot(v, F.pCamUp.xyz)) / z * 0.5 + 0.5;
}
