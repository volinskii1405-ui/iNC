// Фрагментный шейдер чёрной дыры: аккреционный диск с доплеровской асимметрией,
// линзированная задняя часть диска над и под тенью, фотонное кольцо и свечение.
// Рисуется аддитивно поверх уже нарисованной чёрной тени.
#pragma once

namespace bh {

inline const char *kHoleFragment = R"(
#version 330
in vec2 fragTexCoord;
out vec4 finalColor;

uniform float time;
uniform float pulse;    // вспышка от клика, 0..1
uniform float frenzy;   // безумие сверхновой, 0..1
uniform float heat;     // рост дыры, 0..1 — диск горячее и ярче

const float R = 0.227;  // радиус тени в координатах квада (-1..1)

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x),
               mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) { v += a * noise(p); p *= 2.03; a *= 0.5; }
    return v;
}

vec3 palette(float t) {
    vec3 hot  = mix(vec3(1.0, 0.92, 0.80), vec3(0.85, 0.92, 1.0), heat);
    vec3 mid  = mix(vec3(1.0, 0.55, 0.15), vec3(1.0, 0.70, 0.30), heat);
    vec3 cool = vec3(0.55, 0.08, 0.30);
    vec3 c = t < 0.45 ? mix(hot, mid, t / 0.45) : mix(mid, cool, (t - 0.45) / 0.55);
    return mix(c, c * vec3(1.3, 0.6, 1.4), frenzy);
}

// Яркость диска в точке плоскости диска q (единицы квада).
vec3 disk(vec2 q, float doppler) {
    float r = length(q) / R;
    float inner = 1.55, outer = 4.2;
    if (r < inner || r > outer) return vec3(0.0);
    float t = (r - inner) / (outer - inner);
    float ang = atan(q.y, q.x);
    float a = ang - time * (2.2 / pow(r, 1.5)) * (1.0 + 2.0 * frenzy);
    vec2 d = vec2(cos(a), sin(a));
    float streak = fbm(d * r * 1.7 + vec2(r * 3.0, 0.0));
    float fine = noise(d * r * 9.0);
    float dens = smoothstep(0.0, 0.08, t) * (1.0 - smoothstep(0.55, 1.0, t));
    float b = dens * (0.25 + 1.25 * streak * streak + 0.25 * fine);
    b *= 1.0 + 0.55 * doppler;
    return palette(t) * b * (1.0 + 0.6 * heat);
}

void main() {
    vec2 p = (fragTexCoord - 0.5) * 2.0;
    float r = length(p);
    float rr = r / R;
    vec3 col = vec3(0.0);
    float tilt = 0.20;

    // Лучи к наблюдателю ярче: левая сторона диска движется к нам.
    float dop = clamp(-p.x / max(r, 1e-3), -1.0, 1.0);

    // Линзированная задняя часть диска — арка над тенью и слабее под ней.
    if (rr > 1.0) {
        float lr = 1.55 + (rr - 1.03) * 2.4;
        float ang = atan(p.y, p.x);
        vec2 q = vec2(cos(ang), sin(ang)) * lr * R;
        float vert = pow(abs(p.y) / max(r, 1e-3), 1.3);
        float w = (p.y < 0.0 ? 0.95 : 0.45) * vert * (1.0 - smoothstep(2.0, 3.4, rr));
        col += disk(q, dop * 0.5) * w;
    }

    // Передняя часть диска: ближняя половина перекрывает тень.
    vec2 q = vec2(p.x, p.y / tilt);
    bool front = p.y > 0.0;
    vec3 fd = vec3(0.0);
    if (rr > 1.0 || front) fd = disk(q, dop);

    // Фотонное кольцо — позади ближней части диска.
    float ring = exp(-pow((rr - 1.04) / 0.035, 2.0));
    float cover = front ? clamp(dot(fd, vec3(0.6)) * 2.0, 0.0, 1.0) : 0.0;
    col += vec3(1.0, 0.8, 0.55) * ring * (1.3 + 0.8 * pulse) * (1.0 - cover);
    col += fd;

    // Мягкое свечение вокруг.
    if (rr > 1.0) col += palette(0.5) * exp(-(rr - 1.0) * 1.4) * (0.10 + 0.25 * pulse + 0.2 * frenzy);

    // Плавное затухание к краю квада.
    col *= 1.0 - smoothstep(0.85, 1.0, r);
    col = vec3(1.0) - exp(-col * 1.4);  // мягкая тонкомпрессия против пересвета
    finalColor = vec4(col, 1.0);
}
)";

}  // namespace bh
