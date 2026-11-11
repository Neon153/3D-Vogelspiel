#version 460
// final.frag - Hochskalieren, Schaerfen, Belichtung, ACES-Tonemapping, HUD
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set = 0, binding = 1) uniform sampler2D tColor;
layout(set = 0, binding = 2) uniform sampler2D tHud;
layout(set = 0, binding = 3) uniform sampler2D tBloom;
layout(set = 0, binding = 4) uniform sampler2D tExp;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

vec3 aces(vec3 x) { // ACES-Fit (Stephen Hill)
    const mat3 mi = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 mo = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    vec3 v = mi * x; vec3 a = v * (v + 0.0245786) - 0.000090537, b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return clamp(mo * (a / b), 0.0, 1.0);
}
vec3 toSRGB(vec3 c) { return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c)); }

void main() {
    vec2 px = F.res.zw;
    vec3 c = textureLod(tColor, vUV, 0.0).rgb;
    vec3 nb = textureLod(tColor, vUV + vec2(px.x, 0.0), 0.0).rgb + textureLod(tColor, vUV - vec2(px.x, 0.0), 0.0).rgb
            + textureLod(tColor, vUV + vec2(0.0, px.y), 0.0).rgb + textureLod(tColor, vUV - vec2(0.0, px.y), 0.0).rgb;
    float sh = F.misc3.z;
    vec3 shp = c + (c - nb * 0.25) * sh;
    c = max(mix(c, shp, 1.0 / (1.0 + 0.3 * luma(abs(c - nb * 0.25)) / (luma(c) + 1e-3))), 0.0);
    c = mix(c, textureLod(tBloom, vUV, 0.0).rgb, 0.055);
    c *= texelFetch(tExp, ivec2(0), 0).r;
    // Nacht: leichte Entsaettigung + Blaustich (Purkinje-Effekt)
    float night = F.misc2.z; float l = luma(c);
    c = mix(c, vec3(l) * vec3(0.72, 0.86, 1.18), night * 0.45);
    c = aces(c);
    vec2 d = vUV - 0.5; c *= 1.0 - 0.28 * dot(d, d) * 1.6;
    if (F.misc3.x > 0.5) c = toSRGB(c);
    float dn = hash1(uint(gl_FragCoord.x) + uint(gl_FragCoord.y) * 4099u + uint(F.misc.x) * 131u);
    c += (dn - 0.5) / 255.0;
    if (F.misc3.y > 0.5) {
        vec2 hp = gl_FragCoord.xy / F.misc2.y; ivec2 hs = textureSize(tHud, 0);
        if (hp.x < float(hs.x) && hp.y < float(hs.y)) {
            vec2 hv = textureLod(tHud, hp / vec2(hs), 0.0).rg;
            c = mix(c, vec3(0.02, 0.025, 0.03), hv.g * 0.62);
            c = mix(c, vec3(0.96, 0.97, 0.94), hv.r);
        }
    }
    oColor = vec4(c, 1.0);
}
