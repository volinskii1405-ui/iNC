// «Горизонт событий» — инкрементальная игра про чёрную дыру.
// Графика и интерфейс на raylib. Логика — в Game.cpp.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "Audio.hpp"
#include "EmbeddedFonts.hpp"
#include "Game.hpp"
#include "Shaders.hpp"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

using namespace bh;

// GLFW встроен в raylib. Свой обработчик кнопок мыши нужен, чтобы не терять
// короткие клики (тап по тачпаду), когда нажатие и отпускание попадают в один кадр.
extern "C" {
typedef struct GLFWwindow GLFWwindow;
typedef void (*GLFWmousebuttonfun)(GLFWwindow *, int, int, int);
GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow *, GLFWmousebuttonfun);
}

namespace {

GLFWmousebuttonfun gPrevMouseCb = nullptr;
int gPendingClicks = 0;  // нажатия ЛКМ с прошлого кадра
int gClicks = 0;         // нажатия ЛКМ в текущем кадре
std::vector<int> gKeys;  // клавиши, нажатые в этом кадре (из очереди raylib — не теряются)

bool keyHit(int key) { return std::find(gKeys.begin(), gKeys.end(), key) != gKeys.end(); }

void onMouseButton(GLFWwindow *w, int button, int action, int mods)
{
    if (button == 0 && action == 1) gPendingClicks++;
    if (gPrevMouseCb) gPrevMouseCb(w, button, action, mods);
}

constexpr float VW = 1280, VH = 720;
constexpr Vector2 kCenter = {640, 405};
constexpr Rectangle kLeftPanel = {16, 96, 330, 608};
constexpr Rectangle kRightPanel = {934, 96, 330, 608};
constexpr Rectangle kTreeButton = {950, 108, 298, 56};
constexpr Rectangle kBuyModeButton = {kLeftPanel.x + 214, kLeftPanel.y + 8, 104, 26};
constexpr Rectangle kCollapseButton = {470, 548, 340, 58};
constexpr float kCardH = 52, kCardGap = 4;

// ---------- палитра ----------
const Color kBg = {6, 5, 16, 255};
const Color kPanel = {16, 14, 36, 205};
const Color kPanelEdge = {90, 80, 170, 120};
const Color kText = {232, 228, 255, 255};
const Color kDim = {150, 145, 190, 255};
const Color kAccent = {255, 170, 70, 255};
const Color kGood = {120, 240, 150, 255};
const Color kBad = {255, 100, 110, 255};
const Color kGravity = {150, 125, 255, 255};
const Color kAccretion = {255, 150, 60, 255};
const Color kCosmos = {70, 210, 255, 255};
const Color kDark = {235, 90, 210, 255};
const Color kGold = {255, 215, 90, 255};
const Color kRival = {255, 60, 70, 255};

Color branchColor(Branch b)
{
    switch (b) {
    case Branch::Gravity: return kGravity;
    case Branch::Accretion: return kAccretion;
    case Branch::Cosmos: return kCosmos;
    case Branch::Dark: return kDark;
    default: return kText;
    }
}

float frand(float a = 0, float b = 1) { return a + (b - a) * (float)GetRandomValue(0, 100000) / 100000.0f; }

Color lerpColor(Color a, Color b, float t)
{
    t = Clamp(t, 0, 1);
    return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
            (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}

// ---------- шрифты и текст ----------
Font gReg, gBold;

enum Align { LEFT, CENTER, RIGHT };

Vector2 measure(const std::string &s, float size, bool bold = false)
{
    return MeasureTextEx(bold ? gBold : gReg, s.c_str(), size, 0);
}

void text(const std::string &s, float x, float y, float size, Color c, Align al = LEFT, bool bold = false)
{
    Font &f = bold ? gBold : gReg;
    Vector2 m = MeasureTextEx(f, s.c_str(), size, 0);
    if (al == CENTER) x -= m.x / 2;
    if (al == RIGHT) x -= m.x;
    DrawTextEx(f, s.c_str(), {std::round(x), std::round(y)}, size, 0, c);
}

void textGlow(const std::string &s, float x, float y, float size, Color c, Align al, Color glow)
{
    for (int i = 0; i < 8; i++) {
        float a = i * PI / 4;
        text(s, x + std::cos(a) * 2.5f, y + std::sin(a) * 2.5f, size, Fade(glow, 0.25f * c.a / 255.0f), al, true);
    }
    text(s, x, y, size, c, al, true);
}

Font loadFont(const unsigned char *data, int size)
{
    std::vector<int> cps;
    for (int c = 32; c < 127; c++) cps.push_back(c);
    for (int c = 0x400; c < 0x460; c++) cps.push_back(c);
    for (int c : {0xAB, 0xBB, 0xD7, 0xB0, 0x2014, 0x2013, 0x2026, 0x2022, 0x2192, 0x2605, 0x221E, 0x2191, 0x2713, 0x25C6})
        cps.push_back(c);
    Font f = LoadFontFromMemory(".ttf", data, size, 64, cps.data(), (int)cps.size());
    GenTextureMipmaps(&f.texture);
    SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
    return f;
}

// Делит длинное название на две строки по ближайшему к середине пробелу.
std::vector<std::string> splitTwo(const std::string &s)
{
    if (s.size() < 22) return {s};
    size_t mid = s.size() / 2, best = std::string::npos;
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] == ' ' && (best == std::string::npos || (i > mid ? i - mid : mid - i) < (best > mid ? best - mid : mid - best)))
            best = i;
    if (best == std::string::npos) return {s};
    return {s.substr(0, best), s.substr(best + 1)};
}

// ---------- эффекты ----------
struct Particle {
    Vector2 p, v;
    float life, maxLife, size;
    Color c;
    bool attract;
};

struct FloatText {
    Vector2 p;
    float vy, life, maxLife, size;
    std::string s;
    Color c;
};

struct Infaller {
    int type;
    float r, ang, size, rot, spin;
};

struct Shock {
    Vector2 c;
    float r, speed, life, maxLife, thick;
    Color col;
};

struct Toast {
    std::string title, name, sub;
    Color col;
    float t;
};

struct Star {
    Vector2 p;
    float b, tw, speed, size;
};

struct Meteor {
    Vector2 p, v;
    float size, rot;
};

enum class Screen { Intro, Play, Collapse, End };

struct App {
    Game game{1};
    Audio audio;
    Shader hole{};
    bool shaderOk = false;
    int locTime = -1, locPulse = -1, locFrenzy = -1, locHeat = -1;
    Texture2D white{};

    Screen screen = Screen::Intro;
    bool treeOpen = false;
    int buyMode = 0;        // 0: ×1, 1: ×10, 2: макс
    float t = 0;            // реальное время
    float shake = 0;
    float pulse = 0;        // вспышка дыры от клика
    float horizonShown = 0; // плавный рост дыры
    float collapseT = 0;
    float endT = 0;
    float flash = 0;
    float clickTokens = 20;
    float infallAcc = 0;
    float meteorAcc = 0;
    float rivalHitFlash = 0;
    float bannerT = 0;
    Color bannerColor = kGold;
    std::string bannerTitle, bannerText;
    float cardShake[kGenCount] = {};
    float cardFlash[kGenCount] = {};
    float nodeFlash[N_COUNT] = {};
    float abilityFlash[AB_COUNT] = {};
    Vector2 cometFrom{}, cometCtrl{}, cometTo{};
    Vector2 camShake{};

    std::vector<Particle> parts;
    std::vector<FloatText> floats;
    std::vector<Infaller> infallers;
    std::vector<Shock> shocks;
    std::vector<Toast> toasts;
    std::vector<Star> stars;
    std::vector<Meteor> meteors;
};

App app;

float holeRadius()
{
    float r = 36 + 50 * app.horizonShown;
    if (app.screen == Screen::Collapse) r *= 1 + 12 * std::pow(app.collapseT / 4.5f, 5);
    return r * (1 + 0.05f * app.pulse);
}

Vector2 rivalPos() { return {400 + app.game.rivalX * 480, 180 + app.game.rivalY * 280}; }

void addShake(float s) { app.shake = std::max(app.shake, s); }

void burst(Vector2 p, int n, Color c, float speed, float size, float life)
{
    for (int i = 0; i < n; i++) {
        float a = frand(0, 2 * PI), s = frand(0.3f, 1) * speed;
        app.parts.push_back({p, {std::cos(a) * s, std::sin(a) * s}, life * frand(0.6f, 1), life, size * frand(0.5f, 1), c, false});
    }
}

void floatText(Vector2 p, const std::string &s, Color c, float size, float life = 1.2f)
{
    app.floats.push_back({p, -60, life, life, size, s, c});
}

void shock(Vector2 c, float speed, Color col, float life, float thick)
{
    app.shocks.push_back({c, 0, speed, life, life, thick, col});
}

void banner(const std::string &title, const std::string &body, Color c)
{
    app.bannerTitle = title;
    app.bannerText = body;
    app.bannerColor = c;
    app.bannerT = 3.5f;
}

void toast(const std::string &title, const std::string &name, const std::string &sub, Color c)
{
    app.toasts.push_back({title, name, sub, c, 0});
}

void spawnInfaller(int type)
{
    static const float sizes[kGenCount] = {7, 10, 12, 15, 18, 19, 10, 20, 24, 28};
    float a = frand(0, 2 * PI);
    app.infallers.push_back({type, frand(620, 700), a, sizes[type] * frand(0.85f, 1.15f), frand(0, 360), frand(-90, 90)});
}

Vector2 infallerPos(const Infaller &f)
{
    return {kCenter.x + std::cos(f.ang) * f.r, kCenter.y + std::sin(f.ang) * f.r * 0.62f};
}

void suckParticles(int n, Color c)
{
    float R = holeRadius();
    for (int i = 0; i < n; i++) {
        float a = frand(0, 2 * PI), r = R * frand(3.0f, 4.5f);
        Vector2 p = {kCenter.x + std::cos(a) * r, kCenter.y + std::sin(a) * r * 0.6f};
        Vector2 tang = {-std::sin(a), std::cos(a) * 0.6f};
        app.parts.push_back({p, Vector2Scale(tang, frand(80, 160)), 3, 3, frand(1.5f, 3), c, true});
    }
}

const Color kGenColors[kGenCount] = {{190, 170, 150, 255}, {160, 140, 120, 255}, {220, 220, 230, 255}, {90, 170, 240, 255},
                                     {230, 170, 110, 255}, {255, 220, 120, 255}, {170, 210, 255, 255}, {255, 240, 200, 255},
                                     {200, 140, 255, 255}, {120, 200, 255, 255}};

// ---------- рисование объектов ----------

void drawGenIcon(int type, float size, float t, float alpha)
{
    // Рисует объект в начале координат (трансформация уже задана снаружи).
    auto A = [&](Color c) { return Fade(c, alpha * c.a / 255.0f); };
    switch (type) {
    case 0: {  // пыль
        static const Vector2 off[] = {{0, 0}, {-0.6f, -0.3f}, {0.5f, -0.5f}, {0.7f, 0.4f}, {-0.4f, 0.6f}, {0.1f, -0.9f}, {-0.9f, 0.2f}};
        for (int i = 0; i < 7; i++)
            DrawCircleV(Vector2Scale(off[i], size), size * (i == 0 ? 0.35f : 0.22f), A({190, 170, 150, 220}));
        break;
    }
    case 1: {  // астероид
        Vector2 pts[11];
        pts[0] = {0, 0};
        static const float rr[] = {1.0f, 0.8f, 0.95f, 0.7f, 0.9f, 1.05f, 0.75f, 0.92f, 0.85f};
        for (int i = 0; i < 9; i++) {
            float a = -i * 2 * PI / 9;
            pts[i + 1] = {std::cos(a) * size * rr[i], std::sin(a) * size * rr[i]};
        }
        pts[10] = pts[1];
        DrawTriangleFan(pts, 11, A({135, 120, 108, 255}));
        DrawCircleV({size * 0.25f, -size * 0.2f}, size * 0.2f, A({95, 85, 78, 255}));
        DrawCircleV({-size * 0.35f, size * 0.25f}, size * 0.14f, A({95, 85, 78, 255}));
        break;
    }
    case 2:  // луна
        DrawCircleV({0, 0}, size, A({200, 200, 210, 255}));
        DrawCircleV({size * 0.3f, -size * 0.25f}, size * 0.25f, A({160, 160, 172, 255}));
        DrawCircleV({-size * 0.35f, size * 0.3f}, size * 0.18f, A({160, 160, 172, 255}));
        DrawCircleV({-size * 0.2f, -size * 0.45f}, size * 0.12f, A({160, 160, 172, 255}));
        break;
    case 3:  // планета-земля
        DrawCircleV({0, 0}, size * 0.85f, A({70, 140, 220, 255}));
        DrawCircleSector({0, 0}, size * 0.85f, 200, 340, 16, A({80, 190, 120, 255}));
        DrawCircleSector({0, 0}, size * 0.85f, 20, 70, 10, A({80, 190, 120, 255}));
        break;
    case 4:  // газовый гигант с кольцом
        DrawCircleV({0, 0}, size * 0.8f, A({220, 160, 100, 255}));
        DrawRectangleV({-size * 0.75f, -size * 0.2f}, {size * 1.5f, size * 0.14f}, A({180, 110, 70, 255}));
        DrawRectangleV({-size * 0.7f, size * 0.15f}, {size * 1.4f, size * 0.12f}, A({240, 200, 150, 255}));
        DrawEllipseLines(0, 0, size * 1.35f, size * 0.32f, A({230, 210, 170, 255}));
        DrawEllipseLines(0, 0, size * 1.25f, size * 0.27f, A({230, 210, 170, 200}));
        break;
    case 5:  // звезда
        DrawCircleGradient(0, 0, size * 1.4f, A({255, 230, 120, 140}), A({255, 120, 30, 0}));
        DrawCircleV({0, 0}, size * 0.7f, A({255, 240, 170, 255}));
        DrawCircleV({0, 0}, size * 0.45f, A({255, 255, 240, 255}));
        break;
    case 6: {  // нейтронная звезда с лучами
        float a = t * 4;
        for (int s = -1; s <= 1; s += 2) {
            Vector2 d = {std::cos(a) * s, std::sin(a) * s};
            Vector2 n = {-d.y, d.x};
            Vector2 tip = Vector2Scale(d, size * 2.6f);
            DrawTriangle(Vector2Scale(n, size * 0.25f), Vector2Scale(n, -size * 0.25f), tip, A({150, 200, 255, 150}));
            DrawTriangle(Vector2Scale(n, -size * 0.25f), Vector2Scale(n, size * 0.25f), tip, A({150, 200, 255, 150}));
        }
        DrawCircleGradient(0, 0, size * 1.2f, A({170, 210, 255, 200}), A({80, 120, 255, 0}));
        DrawCircleV({0, 0}, size * 0.45f, A({240, 248, 255, 255}));
        break;
    }
    case 7: {  // звёздное скопление
        DrawCircleGradient(0, 0, size * 1.1f, A({255, 230, 200, 70}), A({255, 200, 150, 0}));
        for (int i = 0; i < 18; i++) {
            float a = i * 2.39996f, r = std::sqrt(i / 18.0f) * size;
            DrawCircleV({std::cos(a) * r, std::sin(a) * r}, size * (0.09f + 0.04f * (i % 3)),
                        A(i % 4 == 0 ? Color{180, 210, 255, 255} : Color{255, 240, 200, 255}));
        }
        break;
    }
    case 8:  // галактика
        DrawCircleGradient(0, 0, size * 1.1f, A({190, 120, 255, 90}), A({60, 20, 120, 0}));
        for (int arm = 0; arm < 2; arm++)
            for (int i = 0; i < 22; i++) {
                float k = i / 22.0f;
                float a = arm * PI + k * 4.2f + t * 0.5f;
                Vector2 p = {std::cos(a) * k * size, std::sin(a) * k * size * 0.75f};
                DrawCircleV(p, size * 0.07f * (1.2f - k), A(lerpColor({255, 230, 255, 255}, {150, 100, 255, 255}, k)));
            }
        DrawCircleV({0, 0}, size * 0.18f, A({255, 245, 230, 255}));
        break;
    case 9:  // сверхскопление — паутина
        for (int i = 0; i < 7; i++) {
            float a1 = i * 0.9f, a2 = (i + 3) * 0.9f;
            Vector2 p1 = {std::cos(a1) * size * 0.9f, std::sin(a1) * size * 0.7f};
            Vector2 p2 = {std::cos(a2) * size * 0.5f, std::sin(a2) * size * 0.8f};
            DrawLineEx(p1, p2, size * 0.06f, A({120, 180, 255, 140}));
            DrawCircleV(p1, size * 0.12f, A({170, 210, 255, 255}));
            DrawCircleV(p2, size * 0.09f, A({220, 170, 255, 255}));
        }
        break;
    }
}

void drawIconAt(int type, Vector2 p, float size, float t, float rot = 0, float stretch = 1, float alpha = 1)
{
    rlPushMatrix();
    rlTranslatef(p.x, p.y, 0);
    rlRotatef(rot, 0, 0, 1);
    rlScalef(stretch, 1 / std::sqrt(stretch), 1);
    drawGenIcon(type, size, t, alpha);
    rlPopMatrix();
}

void drawDarkIcon(Vector2 p, float r, Color c)
{
    DrawPoly(p, 4, r, 0, c);
    DrawPoly(p, 4, r * 0.5f, 0, Fade(WHITE, 0.6f));
}

// ---------- сцена ----------

void initStars()
{
    app.stars.clear();
    for (int i = 0; i < 420; i++)
        app.stars.push_back({{frand(0, VW), frand(0, VH)}, frand(0.25f, 1), frand(0, 10), frand(2, 9), frand(0.6f, 1.8f)});
}

void drawBackground()
{
    Game &g = app.game;
    ClearBackground(kBg);
    float t = app.t;
    DrawCircleGradient(260 + 30 * std::sin(t * 0.05f), 220, 420, {70, 30, 120, 70}, {0, 0, 0, 0});
    DrawCircleGradient(1050, 520 + 20 * std::cos(t * 0.04f), 460, {20, 60, 120, 70}, {0, 0, 0, 0});
    DrawCircleGradient(700, 120, 300, {120, 40, 80, 45}, {0, 0, 0, 0});
    if (g.frenzyLeft > 0)
        DrawCircleGradient(640, 400, 700, Fade({255, 60, 200, 255}, 0.18f + 0.08f * std::sin(t * 10)), {0, 0, 0, 0});
    if (g.abilityLeft[AB_WARP] > 0)
        DrawCircleGradient(640, 400, 760, Fade({60, 160, 255, 255}, 0.16f + 0.06f * std::sin(t * 6)), {0, 0, 0, 0});
    if (g.rivalActive)
        DrawCircleGradient((int)rivalPos().x, (int)rivalPos().y, 260, Fade(kRival, 0.12f), {0, 0, 0, 0});

    // Звёзды с гравитационным линзированием вокруг дыры.
    float R = holeRadius();
    float E = R * 1.55f;
    for (auto &s : app.stars) {
        Vector2 d = Vector2Subtract(s.p, kCenter);
        float r = Vector2Length(d) + 0.001f;
        float rl = (r + std::sqrt(r * r + 4 * E * E)) / 2;
        Vector2 p = Vector2Add(kCenter, Vector2Scale(d, rl / r));
        float tw = 0.65f + 0.35f * std::sin(app.t * 2 + s.tw);
        float stretch = Clamp(rl / r, 1, 3);
        Color c = Fade(lerpColor({170, 190, 255, 255}, {255, 240, 220, 255}, s.tw / 10), s.b * tw);
        if (g.abilityLeft[AB_WARP] > 0) {
            // искривление времени — звёзды вытягиваются в полосы
            DrawLineEx(p, {p.x + s.speed * 3, p.y}, s.size * 0.7f, c);
        } else if (stretch > 1.3f) {
            Vector2 tang = Vector2Normalize({-d.y, d.x});
            Vector2 a = Vector2Add(p, Vector2Scale(tang, s.size * stretch));
            Vector2 b = Vector2Subtract(p, Vector2Scale(tang, s.size * stretch));
            DrawLineEx(a, b, s.size * 0.8f, c);
        } else {
            DrawCircleV(p, s.size * 0.6f, c);
        }
    }
}

void drawHole()
{
    Game &g = app.game;
    float R = holeRadius();
    DrawCircleV(kCenter, R, BLACK);

    // Падающие объекты
    for (auto &f : app.infallers) {
        float k = Clamp(R / f.r, 0, 1);
        float stretch = 1 + 3.5f * k * k;
        Vector2 p = infallerPos(f);
        float size = f.size * (0.45f + 0.55f * (1 - k * k));
        float rot = f.ang * RAD2DEG;
        drawIconAt(f.type, p, size, app.t, stretch > 1.15f ? rot : f.rot, stretch, Clamp((f.r - R) / (R * 0.4f), 0, 1));
    }

    float quad = R * 4.4f;
    BeginBlendMode(BLEND_ADDITIVE);
    if (app.shaderOk) {
        float tt = app.t * (g.abilityLeft[AB_WARP] > 0 ? 3 : 1), pu = app.pulse, fr = g.frenzyLeft > 0 ? 1.0f : 0.0f,
              heat = app.horizonShown;
        SetShaderValue(app.hole, app.locTime, &tt, SHADER_UNIFORM_FLOAT);
        SetShaderValue(app.hole, app.locPulse, &pu, SHADER_UNIFORM_FLOAT);
        SetShaderValue(app.hole, app.locFrenzy, &fr, SHADER_UNIFORM_FLOAT);
        SetShaderValue(app.hole, app.locHeat, &heat, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(app.hole);
        DrawTexturePro(app.white, {0, 0, 1, 1}, {kCenter.x - quad, kCenter.y - quad, quad * 2, quad * 2}, {0, 0}, 0, WHITE);
        EndShaderMode();
    } else {
        DrawRing(kCenter, R * 1.0f, R * 1.08f, 0, 360, 64, {255, 200, 140, 255});
        for (int i = 0; i < 40; i++) {
            float a = i * 2 * PI / 40 + app.t * 0.8f;
            float rr = R * (1.7f + 2.2f * ((i * 37) % 40) / 40.0f);
            Vector2 p = {kCenter.x + std::cos(a) * rr, kCenter.y + std::sin(a) * rr * 0.2f};
            DrawCircleV(p, 3, Fade(kAccent, 0.6f));
        }
    }

    // Кольцо резонанса: сжимается к горизонту, кликать — когда оно золотое.
    if (g.hasNode(G_RESONANCE) && app.screen == Screen::Play) {
        float ph = (float)g.resonancePhase();
        float rr = R * (1.08f + 1.7f * (1 - ph));
        bool win = g.resonanceWindow();
        Color c = win ? kGold : Fade(kGravity, 0.25f + 0.45f * ph);
        DrawRing(kCenter, rr - (win ? 3.5f : 1.5f), rr, 0, 360, 96, c);
    }
    EndBlendMode();
}

void drawRival()
{
    Game &g = app.game;
    if (!g.rivalActive) return;
    Vector2 p = rivalPos();
    float r = 22 + 3 * std::sin(app.t * 7);
    // Поток массы от нашей дыры к сопернику
    if (GetRandomValue(0, 2) == 0) {
        Vector2 from = Vector2Add(kCenter, Vector2Scale(Vector2Normalize(Vector2Subtract(p, kCenter)), holeRadius() * 1.3f));
        Vector2 v = Vector2Scale(Vector2Normalize(Vector2Subtract(p, from)), frand(180, 260));
        app.parts.push_back({from, v, Vector2Distance(from, p) / Vector2Length(v), 2, frand(1.5f, 3), kRival, false});
    }
    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient((int)p.x, (int)p.y, r * 3, Fade(kRival, 0.35f), {0, 0, 0, 0});
    EndBlendMode();
    DrawCircleV(p, r, BLACK);
    for (int i = 0; i < 3; i++) DrawRing(p, r + 2 + i * 5, r + 4 + i * 5, app.t * 200 + i * 60, app.t * 200 + i * 60 + 220, 32, Fade(kRival, 0.8f - i * 0.2f));
    if (app.rivalHitFlash > 0) DrawCircleV(p, r, Fade(WHITE, app.rivalHitFlash * 0.6f));
    // Полоска здоровья
    float k = (float)(g.rivalHp / g.rivalMaxHp);
    Rectangle bar = {p.x - 44, p.y - r - 32, 88, 9};
    DrawRectangleRounded(bar, 1, 6, {40, 10, 16, 230});
    DrawRectangleRounded({bar.x, bar.y, bar.width * k, bar.height}, 1, 6, kRival);
    char buf[128];
    std::snprintf(buf, sizeof buf, "СОПЕРНИК  %.0f с", g.rivalLeft);
    text(buf, p.x, bar.y - 18, 13, kRival, CENTER, true);
    text("крадёт " + fmtNum(g.rivalStolen), p.x, p.y + r + 14, 13, Fade(kRival, 0.9f), CENTER);
}

void drawMeteors()
{
    for (auto &m : app.meteors) {
        Vector2 tail = Vector2Subtract(m.p, Vector2Scale(m.v, 0.12f));
        BeginBlendMode(BLEND_ADDITIVE);
        DrawLineEx(tail, m.p, m.size * 0.9f, Fade({255, 140, 60, 255}, 0.6f));
        DrawCircleGradient((int)m.p.x, (int)m.p.y, m.size * 2.2f, Fade({255, 160, 80, 255}, 0.5f), {0, 0, 0, 0});
        EndBlendMode();
        drawIconAt(1, m.p, m.size, app.t, m.rot);
    }
}

void drawEffects()
{
    BeginBlendMode(BLEND_ADDITIVE);
    for (auto &s : app.shocks) {
        float k = s.life / s.maxLife;
        DrawRing(s.c, s.r, s.r + s.thick * k, 0, 360, 96, Fade(s.col, k * 0.8f));
    }
    for (auto &p : app.parts) {
        float k = p.life / p.maxLife;
        DrawCircleV(p.p, p.size * (0.4f + 0.6f * k), Fade(p.c, std::min(1.0f, k * 1.5f)));
    }
    EndBlendMode();
}

Vector2 cometPos()
{
    float k = 1 - (float)(app.game.cometLeft / app.game.cometTotalTime);
    return GetSplinePointBezierQuad(app.cometFrom, app.cometCtrl, app.cometTo, k);
}

void drawComet()
{
    if (app.game.cometLeft <= 0) return;
    Vector2 p = cometPos();
    float pulse = 0.8f + 0.2f * std::sin(app.t * 12);
    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient((int)p.x, (int)p.y, 46 * pulse, Fade(kGold, 0.5f), {0, 0, 0, 0});
    DrawCircleV(p, 9, {255, 250, 220, 255});
    EndBlendMode();
    if (GetRandomValue(0, 1)) app.parts.push_back({p, {frand(-30, 30), frand(-30, 30)}, 1.2f, 1.2f, frand(2, 5), kGold, false});
    if (std::fmod(app.t, 0.6f) < 0.4f) text("КЛИКНИ!", p.x, p.y - 44, 16, kGold, CENTER, true);
}

// ---------- интерфейс ----------

bool hover(Rectangle r, Vector2 m) { return CheckCollisionPointRec(m, r); }

void panel(Rectangle r, Color edge = kPanelEdge)
{
    DrawRectangleRounded(r, 0.06f, 8, kPanel);
    DrawRectangleRoundedLinesEx(r, 0.06f, 8, 1.5f, edge);
}

Rectangle genCard(int i) { return {kLeftPanel.x + 8, kLeftPanel.y + 40 + i * (kCardH + kCardGap), kLeftPanel.width - 16, kCardH}; }

int buyAmount(int i)
{
    const Game &g = app.game;
    if (app.buyMode == 0) return 1;
    if (app.buyMode == 1) return 10;
    return std::max(1, g.genAffordable(i));
}

void drawTooltip(Vector2 m, const std::string &title, const std::string &body, const std::string &foot, Color tc)
{
    float w = std::max({measure(title, 19, true).x, measure(body, 16).x, measure(foot, 16, true).x}) + 28;
    float h = foot.empty() ? 66 : 92;
    float x = std::min(m.x + 18, VW - w - 8), y = std::min(m.y + 18, VH - h - 8);
    DrawRectangleRounded({x, y, w, h}, 0.15f, 6, {12, 10, 30, 248});
    DrawRectangleRoundedLinesEx({x, y, w, h}, 0.15f, 6, 1.5f, Fade(tc, 0.8f));
    text(title, x + 14, y + 10, 19, tc, LEFT, true);
    text(body, x + 14, y + 38, 16, kText);
    if (!foot.empty()) text(foot, x + 14, y + 64, 16, kDim, LEFT, true);
}

struct Tip {
    std::string title, body, foot;
    Color col = kText;
};

void drawLeftPanel(Vector2 m, Tip &tip)
{
    Game &g = app.game;
    panel(kLeftPanel);
    text("ОБЪЕКТЫ", kLeftPanel.x + 14, kLeftPanel.y + 11, 19, kText, LEFT, true);
    text("1–9, 0", kLeftPanel.x + 112, kLeftPanel.y + 14, 13, kDim);

    // Переключатель количества покупки
    static const char *modes[] = {"×1", "×10", "МАКС"};
    bool bh = hover(kBuyModeButton, m);
    DrawRectangleRounded(kBuyModeButton, 0.5f, 6, bh ? Color{70, 60, 130, 255} : Color{45, 38, 90, 255});
    text(std::string("купить ") + modes[app.buyMode], kBuyModeButton.x + kBuyModeButton.width / 2, kBuyModeButton.y + 5, 14, kText, CENTER, true);
    if (bh) tip = {"Сколько покупать за клик", "Переключает ×1 → ×10 → МАКС", "клавиша B", kText};

    for (int i = 0; i < kGenCount; i++) {
        Rectangle r = genCard(i);
        r.x += std::sin(app.cardShake[i] * 60) * app.cardShake[i] * 20;
        bool vis = g.genVisible(i);
        int amount = buyAmount(i);
        double cost = g.genCostN(i, amount);
        bool can = vis && g.mass >= cost;
        bool hov = vis && hover(r, m);
        bool flare = g.flareGen == i && g.flareLeft > 0;
        Color bg = can ? Color{40, 34, 80, 220} : Color{24, 22, 48, 200};
        if (hov) bg = can ? Color{60, 50, 120, 240} : Color{34, 30, 64, 230};
        DrawRectangleRounded(r, 0.2f, 6, bg);
        if (app.cardFlash[i] > 0) DrawRectangleRounded(r, 0.2f, 6, Fade(kGood, app.cardFlash[i] * 0.5f));
        Color edge = can ? Fade(kGood, 0.5f + 0.25f * std::sin(app.t * 4)) : Fade(kPanelEdge, 0.6f);
        if (flare) edge = Fade(kAccent, 0.7f + 0.3f * std::sin(app.t * 8));
        DrawRectangleRoundedLinesEx(r, 0.2f, 6, flare ? 2.5f : 1.2f, edge);

        Vector2 ic = {r.x + 26, r.y + r.height / 2};
        if (!vis) {
            DrawCircleV(ic, 15, {40, 36, 70, 255});
            text("?", ic.x, ic.y - 11, 20, kDim, CENTER, true);
            text("???", r.x + 52, r.y + 8, 16, kDim, LEFT, true);
            text("Поглоти больше массы", r.x + 52, r.y + 29, 13, Fade(kDim, 0.7f));
            continue;
        }
        drawIconAt(i, ic, 14, app.t, hov ? app.t * 40 : 0);
        text(kGens[i].name, r.x + 52, r.y + 6, 16, kText, LEFT, true);
        text("+" + fmtNum(g.genIncome(i)) + "/с", r.x + 52, r.y + 29, 13, flare ? kAccent : Fade(kGood, 0.85f), LEFT, flare);
        // Вехи: 10/25/50/100
        int ml = g.milestoneLevel(i);
        for (int k = 0; k < 4; k++) {
            Vector2 pp = {r.x + 158 + k * 11.0f, r.y + 37};
            DrawCircleV(pp, 3.5f, k < ml ? kGold : Fade(kDim, 0.35f));
        }
        if (flare) text("×5", r.x + 206, r.y + 28, 14, kAccent, LEFT, true);
        text(std::to_string(g.gens[i]), r.x + r.width - 10, r.y + 4, 20, kText, RIGHT, true);
        std::string cs = (amount > 1 ? "×" + std::to_string(amount) + " " : "") + fmtNum(cost);
        text(cs, r.x + r.width - 10, r.y + 30, 14, can ? kGood : kBad, RIGHT, true);

        if (hov) {
            int next = ml < 4 ? kMilestones[ml] : 0;
            std::string foot = next ? "Веха " + std::to_string(next) + " шт.: доход этого типа ×" +
                                          std::string(g.hasNode(C_ATTRACTOR) ? "2" : "1.5")
                                    : "Все вехи собраны!";
            tip = {kGens[i].name, std::string(kGens[i].desc) + "  •  база +" + fmtNum(kGens[i].rate) + "/с за шт.", foot, kText};
        }
    }
}

int affordableNodes()
{
    int n = 0;
    for (int i = 1; i < N_COUNT; i++)
        if (app.game.canAffordNode((NodeId)i)) n++;
    return n;
}

Vector2 achPos(int a) { return {kRightPanel.x + 32 + (a % 7) * 44.0f, kRightPanel.y + 330 + (a / 7) * 40.0f}; }

void drawRightPanel(Vector2 m, Tip &tip)
{
    Game &g = app.game;
    panel(kRightPanel);

    // Кнопка дерева
    int aff = affordableNodes();
    bool hov = hover(kTreeButton, m);
    DrawRectangleRounded(kTreeButton, 0.25f, 8, hov ? Color{90, 60, 170, 255} : Color{60, 40, 120, 255});
    if (aff > 0) DrawRectangleRoundedLinesEx(kTreeButton, 0.25f, 8, 2.5f, Fade(kGood, 0.6f + 0.4f * std::sin(app.t * 5)));
    else DrawRectangleRoundedLinesEx(kTreeButton, 0.25f, 8, 1.5f, Fade(kGravity, 0.8f));
    text("ДЕРЕВО ПРОКАЧКИ", kTreeButton.x + kTreeButton.width / 2, kTreeButton.y + 7, 21, kText, CENTER, true);
    text("узлов " + std::to_string(g.nodesOwned()) + "/" + std::to_string(N_COUNT - 1) + "   •   клавиша T",
         kTreeButton.x + kTreeButton.width / 2, kTreeButton.y + 34, 13, kDim, CENTER);
    if (aff > 0) {
        Vector2 bp = {kTreeButton.x + kTreeButton.width - 6, kTreeButton.y + 6};
        DrawCircleV(bp, 14 + 2 * std::sin(app.t * 6), kGood);
        text(std::to_string(aff), bp.x, bp.y - 10, 18, {10, 30, 10, 255}, CENTER, true);
    }

    float x = kRightPanel.x + 18, y = kRightPanel.y + 84;
    char buf[256];
    text("СИЛА КЛИКА", x, y, 15, kDim, LEFT, true);
    text("+" + fmtNum(g.baseClick() * g.comboMult()), kRightPanel.x + kRightPanel.width - 18, y - 3, 20, kText, RIGHT, true);
    y += 24;
    if (g.critChance() > 0) std::snprintf(buf, sizeof buf, "Крит: %.0f%% шанс, ×%.0f", g.critChance() * 100, g.critMult());
    else std::snprintf(buf, sizeof buf, "Криты закрыты — ищи в дереве");
    text(buf, x, y, 14, g.critChance() > 0 ? kText : kDim);
    y += 22;
    float cm = (float)g.comboMult(), cmax = (float)g.comboMax();
    std::snprintf(buf, sizeof buf, "Комбо ×%.2f  (макс ×%.1f)", cm, cmax);
    text(buf, x, y, 14, cm > 1.01f ? kAccent : kDim);
    Rectangle bar = {x, y + 20, kRightPanel.width - 36, 8};
    DrawRectangleRounded(bar, 1, 6, {30, 26, 60, 255});
    float k = (cm - 1) / 2.5f;
    if (k > 0) DrawRectangleRounded({bar.x, bar.y, bar.width * k, bar.height}, 1, 6, lerpColor(kAccent, kBad, k));
    DrawRectangle((int)(bar.x + bar.width * (cmax - 1) / 2.5f) - 1, (int)bar.y - 3, 2, 14, Fade(kText, 0.6f));
    y += 40;

    // Тёмная материя
    drawDarkIcon({x + 7, y + 9}, 8, kDark);
    std::snprintf(buf, sizeof buf, "Тёмная материя: %.0f", g.dark);
    text(buf, x + 22, y, 16, kDark, LEFT, true);
    if (hover({x, y - 2, 290, 22}, m))
        tip = {"Тёмная материя", "Кометы, достижения, ранги, соперники, метеоры, резонанс", "Тратится в ветке «Тёмная материя»", kDark};
    y += 28;

    // Активные эффекты
    text("ЭФФЕКТЫ", x, y, 15, kDim, LEFT, true);
    y += 22;
    int shown = 0;
    auto effect = [&](const std::string &s, Color c) {
        if (shown >= 3) return;
        text(s, x, y + shown * 20, 14, c, LEFT, true);
        shown++;
    };
    if (g.frenzyLeft > 0) { std::snprintf(buf, sizeof buf, "БЕЗУМИЕ ×7  •  %.0f с", g.frenzyLeft); effect(buf, {255, 140, 230, 255}); }
    if (g.flareLeft > 0 && g.flareGen >= 0) { std::snprintf(buf, sizeof buf, "Вспышка: %s ×5  •  %.0f с", kGens[g.flareGen].name, g.flareLeft); effect(buf, kAccent); }
    if (g.abilityLeft[AB_WARP] > 0) { std::snprintf(buf, sizeof buf, "Время ×3  •  %.0f с", g.abilityLeft[AB_WARP]); effect(buf, kCosmos); }
    if (g.abilityLeft[AB_RUSH] > 0) { std::snprintf(buf, sizeof buf, "Рывок: клики ×3  •  %.0f с", g.abilityLeft[AB_RUSH]); effect(buf, kGravity); }
    if (g.rivalActive) effect("Соперник крадёт массу!", kRival);
    if (g.meteorLeft > 0) effect("Метеоритный дождь!", {255, 160, 80, 255});
    if (shown == 0) text("нет — лови кометы и события", x, y, 14, Fade(kDim, 0.7f));

    // Достижения
    y = kRightPanel.y + 300;
    text("ДОСТИЖЕНИЯ", x, y - 4, 15, kDim, LEFT, true);
    std::snprintf(buf, sizeof buf, "%d/%d  •  +%d%% дохода", g.achCount(), ACH_COUNT, g.achCount() * 2);
    text(buf, kRightPanel.x + kRightPanel.width - 18, y - 3, 13, kDim, RIGHT);
    for (int a = 0; a < ACH_COUNT; a++) {
        Vector2 p = achPos(a);
        bool has = g.hasAch(a);
        if (has) {
            DrawCircleGradient((int)p.x, (int)p.y, 19, Fade(kGold, 0.3f), {0, 0, 0, 0});
            DrawCircleV(p, 14, {90, 70, 20, 255});
            DrawRing(p, 12, 15, 0, 360, 24, kGold);
            DrawPoly(p, 5, 6.5f, -90 + app.t * 20, kGold);
        } else {
            DrawCircleV(p, 14, {28, 26, 50, 255});
            DrawRing(p, 13, 14.5f, 0, 360, 24, Fade(kDim, 0.4f));
            text("?", p.x, p.y - 9, 16, Fade(kDim, 0.6f), CENTER, true);
        }
        if (CheckCollisionPointCircle(m, p, 15)) tip = {kAchs[a].name, kAchs[a].desc, has ? "✓ Получено" : "", has ? kGold : kDim};
    }

    // Цель
    y = kRightPanel.y + 452;
    text("ПОГЛОЩЕНИЕ ВСЕЛЕННОЙ", x, y, 15, kDim, LEFT, true);
    double prog = g.mass / kGoal;
    double logProg = std::clamp(std::log10(g.mass + 1) / std::log10(kGoal), 0.0, 1.0);
    bar = {x, y + 24, kRightPanel.width - 36, 18};
    DrawRectangleRounded(bar, 1, 8, {30, 26, 60, 255});
    if (logProg > 0.01) DrawRectangleRounded({bar.x, bar.y, (float)(bar.width * logProg), bar.height}, 1, 8, lerpColor(kGravity, kBad, (float)logProg));
    text(fmtNum(g.mass) + " / " + fmtNum(kGoal), bar.x + bar.width / 2, bar.y + 1, 15, kText, CENTER, true);
    text(prog >= 1 ? "Готово! Поглощай!" : "шкала по порядкам величины", x, y + 48, 13, prog >= 1 ? kGood : Fade(kDim, 0.7f));
    int sec = (int)g.time;
    std::snprintf(buf, sizeof buf, "%02d:%02d", sec / 60, sec % 60);
    text(buf, kRightPanel.x + kRightPanel.width - 18, y + 48, 14, kDim, RIGHT, true);

    y = kRightPanel.y + kRightPanel.height - 60;
    Color hc = Fade(kDim, 0.8f);
    text("Пробел — клик   B — сколько покупать", x, y, 13, hc);
    text("Q W E — способности   T — дерево", x, y + 18, 13, hc);
    text(std::string("M — звук: ") + (app.audio.muted() ? "выкл" : "вкл") + "   F — полный экран", x, y + 36, 13, hc);
}

Rectangle abilityRect(int a) { return {640 - 165 + a * 112.0f, 640, 104, 56}; }

void drawAbilities(Vector2 m, Tip &tip)
{
    Game &g = app.game;
    static const char *keys[] = {"Q", "W", "E"};
    static const Color cols[] = {kCosmos, kGravity, kGold};
    for (int a = 0; a < AB_COUNT; a++) {
        Rectangle r = abilityRect(a);
        bool un = g.abilityUnlocked((Ability)a);
        bool hov = hover(r, m);
        Color c = cols[a];
        DrawRectangleRounded(r, 0.3f, 8, un ? Color{30, 26, 64, 235} : Color{18, 16, 36, 220});
        if (!un) {
            DrawRectangleRoundedLinesEx(r, 0.3f, 8, 1.2f, Fade(kDim, 0.3f));
            text(keys[a], r.x + 14, r.y + 6, 18, Fade(kDim, 0.5f), LEFT, true);
            text("?", r.x + r.width / 2 + 10, r.y + 14, 24, Fade(kDim, 0.5f), CENTER, true);
            if (hov) tip = {kAbilities[a].name, kAbilities[a].desc, "Открой в ветке «Тёмная материя»", kDim};
            continue;
        }
        double cd = g.abilityCd[a], full = g.abilityCooldown((Ability)a);
        bool ready = cd <= 0;
        if (g.abilityLeft[a] > 0) {
            float k = (float)(g.abilityLeft[a] / kAbilities[a].duration);
            DrawRectangleRounded({r.x, r.y, r.width * k, r.height}, 0.3f, 8, Fade(c, 0.35f));
        } else if (!ready) {
            float k = (float)(cd / full);
            DrawRectangleRounded({r.x, r.y, r.width * k, r.height}, 0.3f, 8, Fade(BLACK, 0.45f));
        }
        if (app.abilityFlash[a] > 0) DrawRectangleRounded(r, 0.3f, 8, Fade(WHITE, app.abilityFlash[a] * 0.5f));
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, ready ? 2.2f : 1.2f, ready ? Fade(c, 0.7f + 0.3f * std::sin(app.t * 4)) : Fade(c, 0.4f));
        text(keys[a], r.x + 12, r.y + 5, 18, ready ? c : Fade(c, 0.5f), LEFT, true);
        // Иконка
        Vector2 ic = {r.x + r.width - 24, r.y + 20};
        if (a == AB_WARP) { DrawRing(ic, 7, 10, app.t * 300, app.t * 300 + 270, 16, c); DrawCircleV(ic, 3, c); }
        if (a == AB_RUSH) { DrawTriangle({ic.x - 8, ic.y - 9}, {ic.x - 8, ic.y + 9}, {ic.x + 9, ic.y}, c); }
        if (a == AB_PORTAL) { DrawCircleV(ic, 9, WHITE); DrawRing(ic, 9, 12, 0, 360, 24, c); }
        std::string st = g.abilityLeft[a] > 0 ? "АКТИВНО" : ready ? "готово" : std::to_string((int)std::ceil(cd)) + " с";
        text(st, r.x + r.width / 2, r.y + 34, 14, ready || g.abilityLeft[a] > 0 ? kText : kDim, CENTER, true);
        if (hov) {
            char buf[128];
            std::snprintf(buf, sizeof buf, "Перезарядка %.0f с  •  клавиша %s", full, keys[a]);
            tip = {kAbilities[a].name, kAbilities[a].desc, buf, c};
        }
    }
}

void drawTop()
{
    Game &g = app.game;
    textGlow(fmtNum(g.mass), 640, 12, 54, kText, CENTER, kGravity);
    text("массы   •   +" + fmtNum(g.income()) + " в секунду", 640, 70, 18, kDim, CENTER);
    Color rc = lerpColor(kAccent, kGold, 0.5f + 0.5f * std::sin(app.t * 2));
    text(g.rankName(), 640, 96, 17, rc, CENTER, true);
    if (g.combo >= 3) {
        char buf[128];
        std::snprintf(buf, sizeof buf, "КОМБО ×%.2f", g.comboMult());
        float s = 20 + 6 * (float)((g.comboMult() - 1) / 2.5);
        textGlow(buf, 640, 122, s, kAccent, CENTER, kBad);
    }
}

void drawBottom(Vector2 m)
{
    Game &g = app.game;
    if (g.mass >= kGoal) {
        bool hov = hover(kCollapseButton, m);
        float p = 0.5f + 0.5f * std::sin(app.t * 5);
        DrawRectangleRounded(kCollapseButton, 0.4f, 10, lerpColor({120, 20, 60, 255}, {200, 40, 90, 255}, hov ? 1 : p));
        DrawRectangleRoundedLinesEx(kCollapseButton, 0.4f, 10, 3, Fade(kGold, 0.6f + 0.4f * p));
        text("ПОГЛОТИТЬ ВСЕЛЕННУЮ", 640, kCollapseButton.y + 8, 24, kText, CENTER, true);
        text("клик или Enter", 640, kCollapseButton.y + 36, 14, Fade(kText, 0.8f), CENTER);
        return;
    }
    std::string hint;
    if (g.clicks < 5) hint = "Кликай по чёрной дыре, чтобы затягивать материю";
    else if (g.gens[0] == 0) hint = "Купи космическую пыль на панели слева — она даёт массу сама";
    else if (g.nodesOwned() == 0 && affordableNodes() > 0) hint = "Открой ДЕРЕВО ПРОКАЧКИ справа (или нажми T)";
    else if (g.rivalActive) hint = "Соперник крадёт массу! Кликай по нему, пока не сбежал";
    else if (g.cometLeft > 0) hint = "Комета! Кликни по ней, пока не улетела";
    else if (g.meteorLeft > 0) hint = "Метеоритный дождь — лови метеоры кликами";
    else if (g.dark >= 3 && !g.hasNode(D_WARP)) hint = "Хватает тёмной материи на способность — загляни в дерево";
    else if (g.hasNode(G_RESONANCE) && g.perfects < 5) hint = "Кликай, когда кольцо вокруг дыры станет золотым";
    else if (g.hasNode(C_INTERCEPT) && g.intercepts < 3) hint = "Кликай по падающим объектам — перехват даёт бонус";
    if (!hint.empty()) text(hint, 640, 612, 16, Fade(kText, 0.6f + 0.3f * std::sin(app.t * 3)), CENTER);
}

void drawToasts()
{
    if (app.toasts.empty()) return;
    Toast &ts = app.toasts.front();
    float in = std::min(1.0f, ts.t * 4), out = std::min(1.0f, (3.2f - ts.t) * 3);
    float k = Clamp(std::min(in, out), 0, 1);
    float x = kLeftPanel.x - (1 - in) * 360;
    Rectangle r = {x, 14, kLeftPanel.width, 72};
    DrawRectangleRounded(r, 0.25f, 8, Fade(lerpColor({20, 16, 30, 255}, ts.col, 0.18f), 0.95f * k));
    DrawRectangleRoundedLinesEx(r, 0.25f, 8, 2, Fade(ts.col, k));
    DrawPoly({r.x + 34, r.y + 36}, 5, 16, -90 + app.t * 60, Fade(ts.col, k));
    text(ts.title, r.x + 62, r.y + 8, 14, Fade(ts.col, k), LEFT, true);
    text(ts.name, r.x + 62, r.y + 25, 19, Fade(kText, k), LEFT, true);
    text(ts.sub, r.x + 62, r.y + 48, 13, Fade(kDim, k));
    if (app.toasts.size() > 1) text("+" + std::to_string(app.toasts.size() - 1), r.x + r.width - 12, r.y + 8, 14, Fade(ts.col, k), RIGHT, true);
}

void drawBanner()
{
    if (app.bannerT <= 0) return;
    float k = Clamp(std::min(app.bannerT, 3.5f - app.bannerT) * 3, 0, 1);
    float s = 1 + 0.15f * (1 - k);
    textGlow(app.bannerTitle, 640, 160, 21 * s, Fade(app.bannerColor, k), CENTER, Fade(kAccent, k));
    textGlow(app.bannerText, 640, 186, 32 * s, Fade(kText, k), CENTER, Fade(app.bannerColor, k));
}

// ---------- дерево прокачки ----------

float branchX(Branch b)
{
    switch (b) {
    case Branch::Gravity: return 170;
    case Branch::Accretion: return 485;
    case Branch::Cosmos: return 795;
    case Branch::Dark: return 1110;
    default: return 640;
    }
}

Vector2 nodePos(int i)
{
    const NodeDef &n = kNodes[i];
    if (n.branch == Branch::Root) return {640, 676};
    return {branchX(n.branch) + n.col * 80.0f, 676 - n.row * 96.0f};
}

void drawNodeIcon(Branch b, Vector2 p, float r, Color c, float t)
{
    switch (b) {
    case Branch::Gravity:
        DrawRing(p, r * 0.25f, r * 0.38f, 0, 360, 24, c);
        DrawRing(p, r * 0.55f, r * 0.62f, t * 90, t * 90 + 270, 24, c);
        break;
    case Branch::Accretion:
        DrawCircleV(p, r * 0.25f, c);
        rlPushMatrix();
        rlTranslatef(p.x, p.y, 0);
        rlRotatef(-20, 0, 0, 1);
        DrawEllipseLines(0, 0, r * 0.7f, r * 0.25f, c);
        DrawEllipseLines(0, 0, r * 0.62f, r * 0.2f, c);
        rlPopMatrix();
        break;
    case Branch::Cosmos:
        DrawPoly(p, 4, r * 0.5f, t * 30, c);
        DrawPoly(p, 4, r * 0.32f, t * 30 + 45, c);
        break;
    case Branch::Dark:
        DrawPoly(p, 4, r * 0.55f, 0, c);
        DrawPolyLines(p, 4, r * 0.75f, t * 40, c);
        break;
    case Branch::Root:
        DrawCircleV(p, r * 0.45f, BLACK);
        DrawRing(p, r * 0.45f, r * 0.55f, 0, 360, 24, kAccent);
        break;
    }
}

std::string nodeCostStr(int i)
{
    return kNodes[i].branch == Branch::Dark ? "◆ " + fmtNum(kNodes[i].cost) : fmtNum(kNodes[i].cost);
}

int drawTree(Vector2 m, bool clicked)
{
    Game &g = app.game;
    DrawRectangle(0, 0, (int)VW, (int)VH, {6, 4, 18, 244});
    textGlow("ДЕРЕВО ПРОКАЧКИ", 640, 14, 32, kText, CENTER, kGravity);
    char buf[256];
    if (g.cometLeft > 0 && std::fmod(app.t, 0.8f) < 0.5f)
        text("★ Пролетает комета! Закрой дерево (T), чтобы поймать", 640, 54, 16, kGold, CENTER, true);
    else if (g.rivalActive && std::fmod(app.t, 0.8f) < 0.5f)
        text("Соперник крадёт массу! Закрой дерево (T)", 640, 54, 16, kRival, CENTER, true);
    else {
        std::snprintf(buf, sizeof buf, "Масса: %s   •   ◆ Тёмная материя: %.0f   •   T или Esc — закрыть", fmtNum(g.mass).c_str(), g.dark);
        text(buf, 640, 54, 16, kDim, CENTER);
    }
    text("ГРАВИТАЦИЯ • клик", branchX(Branch::Gravity), 84, 15, kGravity, CENTER, true);
    text("АККРЕЦИЯ • доход", branchX(Branch::Accretion), 84, 15, kAccretion, CENTER, true);
    text("КОСМОС • события", branchX(Branch::Cosmos), 84, 15, kCosmos, CENTER, true);
    text("ТЁМНАЯ МАТЕРИЯ • способности", branchX(Branch::Dark), 84, 15, kDark, CENTER, true);

    for (int i = 1; i < N_COUNT; i++) {
        Vector2 a = nodePos(kNodes[i].parent), b = nodePos(i);
        Color c = branchColor(kNodes[i].branch);
        if (g.hasNode((NodeId)i)) {
            DrawLineEx(a, b, 4, Fade(c, 0.9f));
            float k = std::fmod(app.t * 0.7f + i * 0.13f, 1.0f);
            BeginBlendMode(BLEND_ADDITIVE);
            DrawCircleV(Vector2Lerp(a, b, k), 3.5f, WHITE);
            EndBlendMode();
        } else if (g.nodeAvailable((NodeId)i)) {
            for (int s = 0; s < 14; s += 2)
                DrawLineEx(Vector2Lerp(a, b, s / 14.0f), Vector2Lerp(a, b, (s + 1) / 14.0f), 2.5f, Fade(c, 0.6f));
        } else {
            DrawLineEx(a, b, 1.5f, Fade(c, 0.15f));
        }
    }

    int hovered = -1;
    for (int i = 0; i < N_COUNT; i++) {
        NodeId id = (NodeId)i;
        Vector2 p = nodePos(i);
        Color c = branchColor(kNodes[i].branch);
        bool owned = g.hasNode(id), avail = g.nodeAvailable(id), can = g.canAffordNode(id);
        float r = i == 0 ? 30 : 23;
        bool hov = CheckCollisionPointCircle(m, p, r + 4);
        if (hov) { hovered = i; r += 3; }
        if (owned) {
            BeginBlendMode(BLEND_ADDITIVE);
            DrawCircleGradient((int)p.x, (int)p.y, r * 2, Fade(c, 0.35f), {0, 0, 0, 0});
            EndBlendMode();
            DrawCircleV(p, r, lerpColor(c, {20, 15, 40, 255}, 0.55f));
            DrawRing(p, r - 3, r, 0, 360, 36, c);
            drawNodeIcon(kNodes[i].branch, p, r, WHITE, app.t);
        } else if (avail) {
            DrawCircleV(p, r, {24, 20, 50, 255});
            float pr = can ? 0.6f + 0.4f * std::sin(app.t * 5) : 0.5f;
            DrawRing(p, r - 2.5f, r, 0, 360, 36, Fade(can ? kGood : c, pr));
            drawNodeIcon(kNodes[i].branch, p, r, Fade(c, 0.85f), app.t);
            text(nodeCostStr(i), p.x, p.y + r + 3, 13, can ? kGood : kBad, CENTER, true);
        } else {
            DrawCircleV(p, r, {16, 14, 32, 255});
            DrawRing(p, r - 1.5f, r, 0, 360, 36, Fade(c, 0.25f));
            drawNodeIcon(kNodes[i].branch, p, r, Fade(c, 0.2f), 0);
        }
        if (app.nodeFlash[i] > 0) {
            BeginBlendMode(BLEND_ADDITIVE);
            DrawCircleV(p, r * (1 + 1.5f * (1 - app.nodeFlash[i])), Fade(WHITE, app.nodeFlash[i] * 0.6f));
            EndBlendMode();
        }
        if (owned || avail) {
            auto lines = splitTwo(kNodes[i].name);
            float ly = p.y - r - 4 - lines.size() * 15.0f;
            for (auto &ln : lines) {
                text(ln, p.x, ly, 13, owned ? Fade(kText, 0.85f) : kText, CENTER, true);
                ly += 15;
            }
        }
    }

    if (hovered >= 0) {
        const NodeDef &n = kNodes[hovered];
        std::string foot;
        if (hovered == 0) foot = "Корень дерева";
        else if (g.hasNode(n.id)) foot = "✓ Куплено";
        else if (!g.nodeAvailable(n.id)) foot = std::string("Сначала: ") + kNodes[n.parent].name;
        else foot = "Цена: " + nodeCostStr(hovered) + (g.canAffordNode(n.id) ? "  — кликни, чтобы купить" : "  — не хватает");
        drawTooltip(m, n.name, n.desc, foot, branchColor(n.branch));
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    }
    return clicked ? hovered : -1;
}

// ---------- обработка событий игры ----------

void handleEvents()
{
    Game &g = app.game;
    for (const Event &e : g.events) {
        switch (e.type) {
        case EvType::Click: {
            app.pulse = 1;
            float pitch = 0.85f + 0.3f * (float)std::min(1.0, (g.comboMult() - 1) / 2.5) + frand(-0.05f, 0.05f);
            float fy = kCenter.y - holeRadius() - 24;
            if (e.crit) {
                app.audio.play(SFX_CRIT, frand(0.9f, 1.1f));
                addShake(10);
                shock(kCenter, 500, kAccent, 0.5f, 14);
                floatText({kCenter.x + frand(-60, 60), fy - 10}, std::string(e.perfect ? "ИДЕАЛЬНЫЙ " : "") + "КРИТ! +" + fmtNum(e.value), kAccent, 34, 1.4f);
                suckParticles(24, kAccent);
            } else if (e.perfect) {
                app.audio.play(SFX_BUY, 1.5f, 0.6f);
                app.audio.play(SFX_CLICK, pitch, 0.7f);
                shock(kCenter, 300, kGold, 0.4f, 6);
                floatText({kCenter.x + frand(-60, 60), fy}, "ИДЕАЛЬНО! +" + fmtNum(e.value), kGold, 26, 1.1f);
                suckParticles(12, kGold);
            } else {
                app.audio.play(SFX_CLICK, pitch, 0.7f);
                floatText({kCenter.x + frand(-70, 70), fy + frand(-10, 10)}, "+" + fmtNum(e.value), kText, 21, 1.0f);
                suckParticles(6, lerpColor({200, 180, 255, 255}, kAccent, frand()));
            }
            break;
        }
        case EvType::Buy: {
            app.audio.play(SFX_BUY, 0.9f + 0.04f * e.index);
            app.cardFlash[e.index] = 1;
            int n = std::min(6, 1 + (int)e.value / 3);
            for (int k = 0; k < n; k++) spawnInfaller(e.index);
            Rectangle r = genCard(e.index);
            burst({r.x + 26, r.y + r.height / 2}, 12, kGood, 150, 3, 0.6f);
            break;
        }
        case EvType::Foam: {
            Rectangle r = genCard(e.index);
            floatText({r.x + r.width / 2, r.y}, "Квантовая пена: +" + fmtNum(e.value) + " бесплатно!", kDark, 16, 1.6f);
            spawnInfaller(e.index);
            break;
        }
        case EvType::Node: {
            app.audio.play(SFX_NODE);
            Vector2 p = nodePos(e.index);
            Color c = branchColor(kNodes[e.index].branch);
            burst(p, 40, c, 260, 4, 0.9f);
            burst(p, 20, WHITE, 140, 2.5f, 0.6f);
            shock(p, 220, c, 0.6f, 10);
            app.nodeFlash[e.index] = 1;
            addShake(5);
            break;
        }
        case EvType::Achievement:
            app.audio.play(SFX_ACH);
            toast("ДОСТИЖЕНИЕ!", kAchs[e.index].name, "+2% к доходу  •  +1 ◆", kGold);
            break;
        case EvType::Milestone:
            app.audio.play(SFX_NODE, 1.3f, 0.6f);
            toast("ВЕХА!", std::string(kGens[e.index].name) + " ×" + std::to_string((int)e.value),
                  std::string("доход этого типа ×") + (g.hasNode(C_ATTRACTOR) ? "2" : "1.5"), kGood);
            for (int k = 0; k < 4; k++) spawnInfaller(e.index);
            break;
        case EvType::Dark:
            floatText({640, 150}, "+" + fmtNum(e.value) + " ◆ тёмной материи", kDark, 18, 1.6f);
            break;
        case EvType::Comet: {
            Vector2 p = cometPos();
            app.audio.play(SFX_COMET);
            burst(p, 60, kGold, 380, 4, 1.1f);
            shock(p, 300, kGold, 0.6f, 8);
            floatText(p, "★ +" + fmtNum(e.value), kGold, 34, 1.8f);
            addShake(6);
            break;
        }
        case EvType::Frenzy: {
            Vector2 p = cometPos();
            app.audio.play(SFX_FRENZY);
            burst(p, 80, {255, 100, 220, 255}, 420, 5, 1.2f);
            shock(kCenter, 700, {255, 80, 220, 255}, 1.0f, 20);
            banner("СВЕРХНОВАЯ", "БЕЗУМИЕ: доход ×7 на 12 секунд!", {255, 120, 230, 255});
            addShake(12);
            break;
        }
        case EvType::Wave:
            app.audio.play(SFX_WAVE);
            shock(kCenter, 900, kGravity, 1.3f, 30);
            shock(kCenter, 650, WHITE, 1.0f, 8);
            floatText({kCenter.x, kCenter.y - holeRadius() - 70}, "ГРАВИТАЦИОННАЯ ВОЛНА +" + fmtNum(e.value), kGravity, 28, 2.0f);
            addShake(16);
            break;
        case EvType::Rank:
            app.audio.play(SFX_RANK);
            banner("НОВЫЙ РАНГ", g.rankName(), kGold);
            shock(kCenter, 600, kGold, 1.2f, 16);
            burst(kCenter, 60, kGold, 300, 3, 1.4f);
            break;
        case EvType::CometSpawn: {
            bool fromLeft = GetRandomValue(0, 1);
            app.cometFrom = {fromLeft ? -60.0f : VW + 60, frand(160, 330)};
            app.cometTo = {fromLeft ? VW + 60 : -60.0f, frand(220, 560)};
            app.cometCtrl = {640 + frand(-150, 150), frand(40, 200)};
            break;
        }
        case EvType::Flare:
            app.audio.play(SFX_FRENZY, 1.4f, 0.6f);
            banner("ЗВЁЗДНАЯ ВСПЫШКА", std::string(kGens[e.index].name) + ": доход ×5 на 20 с", kAccent);
            app.cardFlash[e.index] = 1;
            shock(kCenter, 500, kAccent, 0.8f, 12);
            break;
        case EvType::RivalSpawn:
            app.audio.play(SFX_DENY, 0.6f);
            app.audio.play(SFX_WAVE, 1.6f, 0.7f);
            banner("ЧЁРНАЯ ДЫРА-СОПЕРНИК", "Кликай по ней, пока она крадёт твою массу!", kRival);
            shock(rivalPos(), 400, kRival, 0.8f, 12);
            addShake(8);
            break;
        case EvType::RivalHit:
            app.audio.play(SFX_CLICK, frand(1.4f, 1.7f), 0.7f);
            app.rivalHitFlash = 1;
            burst(rivalPos(), 8, kRival, 200, 3, 0.5f);
            break;
        case EvType::RivalKilled:
            app.audio.play(SFX_CRIT, 0.7f);
            app.audio.play(SFX_ACH, 1.2f, 0.7f);
            burst(rivalPos(), 100, kRival, 450, 5, 1.2f);
            burst(rivalPos(), 50, kGold, 300, 3, 1.0f);
            shock(rivalPos(), 600, kRival, 1.0f, 20);
            floatText(rivalPos(), "СОПЕРНИК ПОГЛОЩЁН! +" + fmtNum(e.value), kGold, 28, 2.2f);
            addShake(14);
            break;
        case EvType::RivalLeft:
            app.audio.play(SFX_DENY, 0.5f);
            floatText(rivalPos(), "Соперник сбежал с " + fmtNum(e.value), kRival, 22, 2.0f);
            burst(rivalPos(), 30, kRival, 200, 3, 0.8f);
            break;
        case EvType::MeteorStart:
            app.audio.play(SFX_COMET, 0.6f);
            banner("МЕТЕОРИТНЫЙ ДОЖДЬ", "Лови метеоры кликами — 10 секунд!", {255, 160, 80, 255});
            break;
        case EvType::Meteor:
            app.audio.play(SFX_COMET, frand(1.2f, 1.5f), 0.5f);
            break;
        case EvType::Intercept:
            app.audio.play(SFX_BUY, 1.6f, 0.5f);
            break;
        case EvType::Ability: {
            static const Color cols[] = {kCosmos, kGravity, kGold};
            app.abilityFlash[e.index] = 1;
            app.audio.play(e.index == AB_PORTAL ? SFX_ACH : SFX_FRENZY, e.index == AB_WARP ? 0.7f : 1.1f);
            shock(kCenter, 800, cols[e.index], 1.0f, 22);
            if (e.index == AB_PORTAL) {
                floatText({kCenter.x, kCenter.y - holeRadius() - 60}, "БЕЛАЯ ДЫРА +" + fmtNum(e.value), WHITE, 30, 2.0f);
                burst(kCenter, 120, WHITE, 500, 4, 1.2f);
            }
            if (e.index == AB_WARP) banner("ИСКРИВЛЕНИЕ ВРЕМЕНИ", "Время течёт втрое быстрее", kCosmos);
            if (e.index == AB_RUSH) banner("ГРАВИТАЦИОННЫЙ РЫВОК", "Клики ×3 — жми!", kGravity);
            addShake(8);
            break;
        }
        case EvType::Collapse:
            app.audio.play(SFX_COLLAPSE);
            app.screen = Screen::Collapse;
            app.collapseT = 0;
            app.treeOpen = false;
            g.cometLeft = 0;
            g.rivalActive = false;
            app.meteors.clear();
            break;
        }
    }
    g.events.clear();
}

// ---------- обновление ----------

void updateEffects(float dt)
{
    Game &g = app.game;
    float R = holeRadius();
    float sdt = dt * (float)g.timeScale();
    for (auto &p : app.parts) {
        if (p.attract) {
            Vector2 d = Vector2Subtract(kCenter, p.p);
            float dist = Vector2Length(d);
            p.v = Vector2Add(p.v, Vector2Scale(Vector2Normalize(d), 900 * dt));
            if (dist < R * 0.95f) p.life = 0;
        } else {
            p.v = Vector2Scale(p.v, 1 - 1.8f * dt);
        }
        p.p = Vector2Add(p.p, Vector2Scale(p.v, dt));
        p.life -= dt;
    }
    app.parts.erase(std::remove_if(app.parts.begin(), app.parts.end(), [](const Particle &p) { return p.life <= 0; }), app.parts.end());
    if (app.parts.size() > 2500) app.parts.erase(app.parts.begin(), app.parts.begin() + (app.parts.size() - 2500));

    for (auto &f : app.floats) {
        f.p.y += f.vy * dt;
        f.vy *= 1 - 1.5f * dt;
        f.life -= dt;
    }
    app.floats.erase(std::remove_if(app.floats.begin(), app.floats.end(), [](const FloatText &f) { return f.life <= 0; }), app.floats.end());
    if (app.floats.size() > 60) app.floats.erase(app.floats.begin(), app.floats.begin() + (app.floats.size() - 60));

    for (auto &s : app.shocks) {
        s.r += s.speed * dt;
        s.life -= dt;
    }
    app.shocks.erase(std::remove_if(app.shocks.begin(), app.shocks.end(), [](const Shock &s) { return s.life <= 0; }), app.shocks.end());

    for (auto &f : app.infallers) {
        float speed = 35 + 26000 / std::max(f.r, 20.0f);
        f.r -= speed * sdt;
        f.ang += sdt * 260 / std::pow(std::max(f.r, 20.0f), 1.05f);
        f.rot += f.spin * sdt;
        if (f.r < R) {
            Vector2 p = {kCenter.x + std::cos(f.ang) * R, kCenter.y + std::sin(f.ang) * R * 0.62f};
            burst(p, 4 + f.type, kGenColors[f.type], 90, 2.5f, 0.5f);
        }
    }
    app.infallers.erase(std::remove_if(app.infallers.begin(), app.infallers.end(), [R](const Infaller &f) { return f.r < R; }), app.infallers.end());
    if (app.infallers.size() > 160) app.infallers.erase(app.infallers.begin(), app.infallers.begin() + (app.infallers.size() - 160));

    // Метеоры
    if (g.meteorLeft > 0 && app.screen == Screen::Play) {
        app.meteorAcc += sdt * 1.6f;
        while (app.meteorAcc >= 1) {
            app.meteorAcc -= 1;
            Vector2 p = {frand(450, 1150), -30};
            Vector2 v = {frand(-260, -160), frand(170, 240)};
            app.meteors.push_back({p, v, frand(9, 13), frand(0, 360)});
        }
    }
    for (auto &m : app.meteors) {
        m.p = Vector2Add(m.p, Vector2Scale(m.v, sdt));
        m.rot += 120 * sdt;
    }
    app.meteors.erase(std::remove_if(app.meteors.begin(), app.meteors.end(), [](const Meteor &m) { return m.p.y > VH + 40 || m.p.x < -40; }), app.meteors.end());

    if (!app.toasts.empty()) {
        app.toasts.front().t += dt * (app.toasts.size() > 2 ? 1.8f : 1.0f);
        if (app.toasts.front().t > 3.2f) app.toasts.erase(app.toasts.begin());
    }

    float starK = g.abilityLeft[AB_WARP] > 0 ? 6 : 1;
    for (auto &s : app.stars) {
        s.p.x -= s.speed * dt * starK;
        if (s.p.x < -10) s.p = {VW + 10, frand(0, VH)};
    }

    for (int i = 0; i < kGenCount; i++) {
        app.cardShake[i] = std::max(0.0f, app.cardShake[i] - dt);
        app.cardFlash[i] = std::max(0.0f, app.cardFlash[i] - dt * 2.5f);
    }
    for (float &f : app.nodeFlash) f = std::max(0.0f, f - dt * 1.5f);
    for (float &f : app.abilityFlash) f = std::max(0.0f, f - dt * 2);

    app.rivalHitFlash = std::max(0.0f, app.rivalHitFlash - dt * 6);
    app.pulse = std::max(0.0f, app.pulse - dt * 5);
    app.bannerT = std::max(0.0f, app.bannerT - dt);
    app.shake = std::max(0.0f, app.shake - dt * 30);
    app.camShake = {frand(-1, 1) * app.shake, frand(-1, 1) * app.shake};
    app.horizonShown += ((float)g.horizon() - app.horizonShown) * std::min(1.0f, dt * 2);
}

void spawnPassiveInfallers(float dt)
{
    Game &g = app.game;
    int total = 0;
    for (int c : g.gens) total += c;
    if (total == 0) return;
    app.infallAcc += dt * (float)g.timeScale() * std::min(3.5f, 0.4f + total * 0.02f);
    while (app.infallAcc >= 1) {
        app.infallAcc -= 1;
        double w = 0;
        for (int i = 0; i < kGenCount; i++) w += g.gens[i] * (1 + i);
        double pick = frand() * w;
        for (int i = 0; i < kGenCount; i++) {
            pick -= g.gens[i] * (1 + i);
            if (pick <= 0) { spawnInfaller(i); break; }
        }
    }
}

void doClick()
{
    if (app.clickTokens < 1) return;
    app.clickTokens -= 1;
    app.game.click();
}

void tryBuy(int i)
{
    if (app.game.buyGen(i, buyAmount(i)) == 0) {
        app.cardShake[i] = 0.3f;
        app.audio.play(SFX_DENY);
    }
}

void tryAbility(Ability a)
{
    if (!app.game.useAbility(a)) app.audio.play(SFX_DENY, 1.2f, 0.5f);
}

void startGame()
{
    app.game.reset((uint32_t)std::time(nullptr));
    app.parts.clear();
    app.floats.clear();
    app.infallers.clear();
    app.shocks.clear();
    app.toasts.clear();
    app.meteors.clear();
    app.horizonShown = 0;
    app.treeOpen = false;
    app.bannerT = 0;
    app.screen = Screen::Play;
}

// Что находится под курсором — в порядке приоритета для клика.
enum class Target { None, Comet, Rival, Meteor, Infaller, Hole, Collapse, Tree, BuyMode, Card, Ability };

Target pickTarget(Vector2 m, int &index)
{
    Game &g = app.game;
    index = -1;
    if (g.mass >= kGoal && hover(kCollapseButton, m)) return Target::Collapse;
    if (hover(kTreeButton, m)) return Target::Tree;
    if (hover(kBuyModeButton, m)) return Target::BuyMode;
    for (int i = 0; i < kGenCount; i++)
        if (g.genVisible(i) && hover(genCard(i), m)) { index = i; return Target::Card; }
    for (int a = 0; a < AB_COUNT; a++)
        if (g.abilityUnlocked((Ability)a) && hover(abilityRect(a), m)) { index = a; return Target::Ability; }
    if (hover(kLeftPanel, m) || hover(kRightPanel, m)) return Target::None;
    if (g.cometLeft > 0 && Vector2Distance(m, cometPos()) < 46) return Target::Comet;
    if (g.rivalActive && Vector2Distance(m, rivalPos()) < 42) return Target::Rival;
    for (int k = 0; k < (int)app.meteors.size(); k++)
        if (Vector2Distance(m, app.meteors[k].p) < 30) { index = k; return Target::Meteor; }
    float R = holeRadius();
    if (g.hasNode(C_INTERCEPT))
        for (int k = (int)app.infallers.size() - 1; k >= 0; k--) {
            const Infaller &f = app.infallers[k];
            if (f.r > R * 1.9f && Vector2Distance(m, infallerPos(f)) < f.size + 10) { index = k; return Target::Infaller; }
        }
    if (CheckCollisionPointCircle(m, kCenter, R * 1.7f)) return Target::Hole;
    return Target::None;
}

void updatePlay(float dt, Vector2 m)
{
    Game &g = app.game;
    if (keyHit(KEY_T)) app.treeOpen = !app.treeOpen;
    if (keyHit(KEY_ESCAPE)) app.treeOpen = false;
    if (keyHit(KEY_B)) app.buyMode = (app.buyMode + 1) % 3;
    if (keyHit(KEY_Q)) tryAbility(AB_WARP);
    if (keyHit(KEY_W)) tryAbility(AB_RUSH);
    if (keyHit(KEY_E)) tryAbility(AB_PORTAL);

    if (!app.treeOpen) {
        if (keyHit(KEY_SPACE)) {
            if (g.rivalActive) g.hitRival();
            else doClick();
        }
        for (int i = 0; i < kGenCount; i++)
            if (keyHit(i == 9 ? KEY_ZERO : KEY_ONE + i)) tryBuy(i);
        if ((keyHit(KEY_ENTER) || keyHit(KEY_KP_ENTER)) && g.mass >= kGoal) g.collapse();

        int idx;
        Target tg = pickTarget(m, idx);
        if (tg != Target::None) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        for (int c = 0; c < gClicks; c++) {
            switch (tg) {
            case Target::Comet: g.catchComet(); break;
            case Target::Rival: g.hitRival(); break;
            case Target::Meteor:
                if (idx >= 0 && idx < (int)app.meteors.size() && g.catchMeteor()) {
                    Vector2 p = app.meteors[idx].p;
                    burst(p, 24, {255, 160, 80, 255}, 260, 3.5f, 0.7f);
                    floatText(p, "+" + fmtNum(g.events.back().value), {255, 180, 100, 255}, 20, 1.0f);
                    app.meteors.erase(app.meteors.begin() + idx);
                    idx = -1;
                }
                break;
            case Target::Infaller:
                if (idx >= 0 && idx < (int)app.infallers.size()) {
                    Infaller f = app.infallers[idx];
                    if (g.intercept(f.type)) {
                        Vector2 p = infallerPos(f);
                        burst(p, 18, kGenColors[f.type], 220, 3, 0.6f);
                        floatText(p, "Перехват +" + fmtNum(g.events.back().value), kCosmos, 18, 1.1f);
                        app.infallers.erase(app.infallers.begin() + idx);
                        idx = -1;
                    }
                }
                break;
            case Target::Hole: doClick(); break;
            case Target::Collapse: g.collapse(); break;
            case Target::Tree: app.treeOpen = true; app.audio.play(SFX_BUY, 1.4f, 0.5f); break;
            case Target::BuyMode: app.buyMode = (app.buyMode + 1) % 3; app.audio.play(SFX_BUY, 1.8f, 0.4f); break;
            case Target::Card: tryBuy(idx); break;
            case Target::Ability: tryAbility((Ability)idx); break;
            case Target::None: break;
            }
            if (tg == Target::Tree || tg == Target::BuyMode || tg == Target::Collapse) break;
        }
    }

    g.update(dt);
    spawnPassiveInfallers(dt);
}

// ---------- кадры ----------

void drawFloats()
{
    for (auto &f : app.floats) {
        float k = f.life / f.maxLife;
        float s = f.size * (k > 0.85f ? 1 + (k - 0.85f) * 3 : 1);
        text(f.s, f.p.x + 2, f.p.y + 2, s, Fade(BLACK, k * 0.6f), CENTER, true);
        text(f.s, f.p.x, f.p.y, s, Fade(f.c, std::min(1.0f, k * 2)), CENTER, true);
    }
}

void drawIntro()
{
    DrawRectangle(0, 0, (int)VW, (int)VH, Fade(kBg, 0.7f));
    textGlow("ГОРИЗОНТ СОБЫТИЙ", 640, 110, 64, kText, CENTER, kGravity);
    text("инкрементальная игра про чёрную дыру", 640, 186, 22, kDim, CENTER);
    text("Ты — крошечная чёрная дыра. Поглощай пыль, планеты, звёзды и галактики, прокачивай", 640, 548, 18, kText, CENTER);
    text("дерево из 31 умения, лови кометы и метеоры, побеждай соперников и копи тёмную материю.", 640, 572, 18, kText, CENTER);
    text("Цель — проглотить всю Вселенную. Примерно 9 минут.", 640, 596, 18, kText, CENTER);
    if (std::fmod(app.t, 1.0f) < 0.7f) textGlow("Кликни, чтобы начать", 640, 640, 28, kGold, CENTER, kAccent);
}

void drawEnd()
{
    Game &g = app.game;
    ClearBackground(BLACK);
    if (GetRandomValue(0, 1)) {
        Color c = ColorFromHSV(frand(0, 360), 0.6f, 1);
        float a = frand(0, 2 * PI), s = frand(60, 500);
        app.parts.push_back({{640, 360}, {std::cos(a) * s, std::sin(a) * s}, 3, 3, frand(1, 4), c, false});
    }
    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient(640, 360, 200 + 30 * std::sin(app.t * 2), Fade({255, 230, 200, 255}, 0.35f), {0, 0, 0, 0});
    EndBlendMode();
    drawEffects();

    float k = Clamp(app.endT / 1.5f, 0, 1);
    Rectangle r = {340, 100, 600, 520};
    DrawRectangleRounded(r, 0.08f, 8, Fade({10, 8, 24, 255}, 0.85f * k));
    DrawRectangleRoundedLinesEx(r, 0.08f, 8, 2, Fade(kGold, k));
    textGlow("ВСЕЛЕННАЯ ПОГЛОЩЕНА", 640, 120, 40, Fade(kText, k), CENTER, Fade(kGravity, k));
    text("...и из сингулярности рождается новый Большой взрыв", 640, 170, 18, Fade(kDim, k), CENTER);
    int sec = (int)g.time;
    char buf[128];
    std::snprintf(buf, sizeof buf, "%02d:%02d", sec / 60, sec % 60);
    text("Время прохождения", 640, 206, 18, Fade(kDim, k), CENTER);
    textGlow(buf, 640, 230, 54, Fade(kGold, k), CENTER, Fade(kAccent, k));
    float y = 308;
    auto row = [&](const char *name, const std::string &val) {
        text(name, 400, y, 18, Fade(kText, k));
        text(val, 880, y, 18, Fade(kText, k), RIGHT, true);
        y += 27;
    };
    row("Поглощено массы", fmtNum(g.total));
    row("Кликов / идеальных", std::to_string(g.clicks) + " / " + std::to_string(g.perfects));
    row("Критов", std::to_string(g.crits));
    row("Поймано комет / метеоров", std::to_string(g.cometsCaught) + " / " + std::to_string(g.meteorsCaught));
    row("Побеждено соперников", std::to_string(g.rivalsKilled));
    row("Перехвачено объектов", std::to_string(g.intercepts));
    row("Добыто тёмной материи", fmtNum(g.darkTotal));
    row("Узлов дерева", std::to_string(g.nodesOwned()) + " / " + std::to_string(N_COUNT - 1));
    row("Достижений", std::to_string(g.achCount()) + " / " + std::to_string(ACH_COUNT));
    if (std::fmod(app.t, 1.0f) < 0.7f) text("R — сыграть снова     Esc — выход", 640, 646, 20, Fade(kText, k), CENTER, true);
}

void loadShader()
{
    app.hole = LoadShaderFromMemory(nullptr, kHoleFragment);
    app.shaderOk = app.hole.id != 0 && app.hole.id != rlGetShaderIdDefault();
    if (app.shaderOk) {
        app.locTime = GetShaderLocation(app.hole, "time");
        app.locPulse = GetShaderLocation(app.hole, "pulse");
        app.locFrenzy = GetShaderLocation(app.hole, "frenzy");
        app.locHeat = GetShaderLocation(app.hole, "heat");
    }
    Image img = GenImageColor(1, 1, WHITE);
    app.white = LoadTextureFromImage(img);
    UnloadImage(img);
}

// Отладочные сцены для скриншотов: BH_SCENE=intro|play|mid|late|tree|collapse|end.
void devScene(const std::string &scene)
{
    startGame();
    Game &g = app.game;
    auto give = [&](double m) { g.mass += m; g.total += m; };
    if (scene == "intro") { app.screen = Screen::Intro; return; }
    bool early = scene == "play";
    int n = early ? 3 : scene == "mid" ? 7 : kGenCount;
    for (int i = 0; i < n; i++) {
        int cnt = early ? 12 - i * 4 : scene == "mid" ? 30 - i * 3 : 40 - i * 2;
        for (int k = 0; k < cnt; k++) { give(g.genCost(i)); g.buyGen(i, 1); }
    }
    for (int i = 1; i < N_COUNT; i++) {
        bool take = early ? kNodes[i].cost < 400 && kNodes[i].branch != Branch::Dark
                          : scene == "mid" ? (kNodes[i].branch == Branch::Dark ? kNodes[i].cost <= 6 : kNodes[i].cost < 1e7) : true;
        if (!take) continue;
        if (kNodes[i].branch == Branch::Dark) g.dark += kNodes[i].cost;
        else give(kNodes[i].cost);
        g.buyNode((NodeId)i);
    }
    g.dark += 5;
    if (scene == "late" || scene == "collapse") give(kGoal * 1.2);
    g.events.clear();
    for (int i = 0; i < 25; i++) g.click();
    g.events.clear();
    g.update(0.01);
    g.events.clear();
    app.toasts.clear();
    g.cometLeft = 5;
    app.cometFrom = {-60, 200}; app.cometCtrl = {600, 80}; app.cometTo = {1340, 400};
    if (scene == "mid") {
        g.rivalActive = true; g.rivalMaxHp = 25; g.rivalHp = 16; g.rivalLeft = 18; g.rivalStolen = g.mass * 0.2;
        g.rivalX = 0.75f; g.rivalY = 0.15f;
        g.meteorLeft = 8;
        g.flareGen = 4; g.flareLeft = 12;
        g.abilityCd[AB_RUSH] = 30; g.abilityLeft[AB_WARP] = 6; g.abilityCd[AB_WARP] = 80;
    }
    app.horizonShown = (float)g.horizon();
    if (scene == "tree") app.treeOpen = true;
    if (scene == "end") { g.collapse(); g.events.clear(); app.screen = Screen::End; app.endT = 3; }
    if (scene == "collapse") { g.events.clear(); g.collapse(); }
}

}  // namespace

int main()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow((int)VW, (int)VH, "Горизонт событий");
    SetWindowMinSize(640, 360);
    SetExitKey(KEY_NULL);
    gPrevMouseCb = glfwSetMouseButtonCallback((GLFWwindow *)GetWindowHandle(), onMouseButton);
    SetTargetFPS(60);

    gReg = loadFont(kFontRegular, (int)kFontRegularSize);
    gBold = loadFont(kFontBold, (int)kFontBoldSize);
    loadShader();
    app.audio.init();
    initStars();
    app.game.reset((uint32_t)std::time(nullptr));
    if (const char *scene = std::getenv("BH_SCENE")) devScene(scene);

    const char *shotEnv = std::getenv("BH_SCREENSHOT");  // для автотестов: снимок и выход
    const int shotFrame = std::getenv("BH_FRAME") ? std::atoi(std::getenv("BH_FRAME")) : 90;
    int frame = 0;

    while (!WindowShouldClose()) {
        float dt = std::min(GetFrameTime(), 0.1f);
        gClicks = gPendingClicks;
        gPendingClicks = 0;
        gKeys.clear();
        for (int k = GetKeyPressed(); k != 0; k = GetKeyPressed()) gKeys.push_back(k);
        app.t += dt;
        frame++;

        float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
        float scale = std::min(sw / VW, sh / VH);
        Camera2D ui{};
        ui.offset = {(sw - VW * scale) / 2, (sh - VH * scale) / 2};
        ui.zoom = scale;
        Vector2 m = GetScreenToWorld2D(GetMousePosition(), ui);
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);

        if (keyHit(KEY_F) || keyHit(KEY_F11)) ToggleBorderlessWindowed();
        if (keyHit(KEY_M)) app.audio.toggleMute();
        app.audio.update();
        app.clickTokens = std::min(20.0f, app.clickTokens + dt * 20);

        int treeClick = -1;
        bool quit = false;
        switch (app.screen) {
        case Screen::Intro:
            if (gClicks > 0 || keyHit(KEY_SPACE) || keyHit(KEY_ENTER)) startGame();
            break;
        case Screen::Play:
            updatePlay(dt, m);
            break;
        case Screen::Collapse:
            app.collapseT += dt;
            addShake(4 + app.collapseT * 4);
            for (auto &s : app.stars) s.p = Vector2Lerp(s.p, kCenter, dt * app.collapseT * 0.4f);
            if (app.collapseT > 4.5f) {
                app.screen = Screen::End;
                app.endT = 0;
                app.flash = 1;
                app.parts.clear();
                for (int i = 0; i < 400; i++) {
                    Color c = ColorFromHSV(frand(0, 360), 0.5f, 1);
                    float a = frand(0, 2 * PI), s = frand(100, 900);
                    app.parts.push_back({{640, 360}, {std::cos(a) * s, std::sin(a) * s}, 3, 3, frand(2, 6), c, false});
                }
            }
            break;
        case Screen::End:
            app.endT += dt;
            if (keyHit(KEY_R)) startGame();
            if (keyHit(KEY_ESCAPE)) quit = true;
            break;
        }
        if (quit) break;
        handleEvents();
        updateEffects(dt);
        app.flash = std::max(0.0f, app.flash - dt * 0.8f);

        BeginDrawing();
        ClearBackground(BLACK);
        Camera2D scene = ui;
        scene.offset = Vector2Add(ui.offset, Vector2Scale(app.camShake, scale));
        BeginScissorMode((int)ui.offset.x, (int)ui.offset.y, (int)(VW * scale), (int)(VH * scale));

        if (app.screen == Screen::End) {
            BeginMode2D(ui);
            drawEnd();
            EndMode2D();
        } else {
            BeginMode2D(scene);
            drawBackground();
            drawHole();
            drawRival();
            drawMeteors();
            drawEffects();
            drawComet();
            EndMode2D();

            BeginMode2D(ui);
            if (app.screen == Screen::Intro) {
                drawIntro();
            } else {
                float uiAlpha = app.screen == Screen::Collapse ? std::max(0.0f, 1 - app.collapseT) : 1;
                if (uiAlpha > 0) {
                    Tip tip;
                    drawFloats();
                    drawTop();
                    drawBottom(m);
                    drawAbilities(m, tip);
                    drawLeftPanel(m, tip);
                    drawRightPanel(m, tip);
                    drawBanner();
                    drawToasts();
                    if (!app.treeOpen && !tip.title.empty()) drawTooltip(m, tip.title, tip.body, tip.foot, tip.col);
                    if (app.treeOpen) treeClick = drawTree(m, gClicks > 0);
                    if (uiAlpha < 1) DrawRectangle(0, 0, (int)VW, (int)VH, Fade(BLACK, (1 - uiAlpha) * 0.3f));
                }
                if (app.screen == Screen::Collapse) {
                    float k = Clamp((app.collapseT - 3.5f) / 1.0f, 0, 1);
                    DrawRectangle(0, 0, (int)VW, (int)VH, Fade(WHITE, k));
                }
            }
            EndMode2D();
        }
        if (app.flash > 0) {
            BeginMode2D(ui);
            DrawRectangle(0, 0, (int)VW, (int)VH, Fade(WHITE, app.flash));
            EndMode2D();
        }
        EndScissorMode();
        EndDrawing();

        if (treeClick > 0) {
            if (!app.game.buyNode((NodeId)treeClick) && !app.game.hasNode((NodeId)treeClick)) {
                app.audio.play(SFX_DENY);
                addShake(3);
            }
        }

        if (shotEnv && frame == shotFrame) {
            TakeScreenshot(shotEnv);
            break;
        }
    }
    app.audio.shutdown();
    UnloadShader(app.hole);
    UnloadFont(gReg);
    UnloadFont(gBold);
    CloseWindow();
    return 0;
}
