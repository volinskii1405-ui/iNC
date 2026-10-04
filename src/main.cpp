// «Горизонт событий» — инкрементальная игра про чёрную дыру.
// В заходе дыра летит за курсором и поглощает всё, что меньше неё;
// между заходами — созвездие прокачки. Графика на raylib, логика — в Game.cpp.
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
int gPendingClicks = 0, gPendingRight = 0;
int gClicks = 0, gRight = 0;
std::vector<int> gKeys;  // клавиши, нажатые в этом кадре (из очереди raylib — не теряются)

void onMouseButton(GLFWwindow *w, int button, int action, int mods)
{
    if (action == 1 && button == 0) gPendingClicks++;
    if (action == 1 && button == 1) gPendingRight++;
    if (gPrevMouseCb) gPrevMouseCb(w, button, action, mods);
}

bool keyHit(int key) { return std::find(gKeys.begin(), gKeys.end(), key) != gKeys.end(); }

constexpr float VW = 1280, VH = 720;
constexpr Vector2 kScreenC = {640, 360};
constexpr Rectangle kStartButton = {1010, 642, 252, 62};
constexpr float kRingStep = 128;

// ---------- палитра ----------
const Color kBg = {6, 5, 16, 255};
const Color kPanel = {16, 14, 36, 220};
const Color kPanelEdge = {90, 80, 170, 120};
const Color kText = {232, 228, 255, 255};
const Color kDim = {150, 145, 190, 255};
const Color kAccent = {255, 170, 70, 255};
const Color kGood = {120, 240, 150, 255};
const Color kBad = {255, 100, 110, 255};
const Color kGold = {255, 215, 90, 255};
const Color kDarkC = {235, 90, 210, 255};
const Color kTimeC = {80, 220, 255, 255};
const Color kRival = {255, 60, 70, 255};

Color branchColor(Branch b)
{
    switch (b) {
    case Branch::Gravity: return {150, 125, 255, 255};
    case Branch::Growth: return {110, 230, 130, 255};
    case Branch::Time: return kTimeC;
    case Branch::Wealth: return kGold;
    case Branch::Cosmos: return {255, 140, 70, 255};
    case Branch::Dark: return kDarkC;
    default: return kText;
    }
}

const char *branchName(Branch b)
{
    switch (b) {
    case Branch::Gravity: return "ГРАВИТАЦИЯ";
    case Branch::Growth: return "РОСТ";
    case Branch::Time: return "ВРЕМЯ";
    case Branch::Wealth: return "БОГАТСТВО";
    case Branch::Cosmos: return "КОСМОС";
    case Branch::Dark: return "ТЁМНАЯ МАТЕРИЯ";
    default: return "";
    }
}

float branchAngle(Branch b)
{
    switch (b) {
    case Branch::Gravity: return -90;
    case Branch::Time: return -30;
    case Branch::Wealth: return 30;
    case Branch::Cosmos: return 90;
    case Branch::Dark: return 150;
    case Branch::Growth: return -150;
    default: return 0;
    }
}

bool hover(Rectangle r, Vector2 m) { return CheckCollisionPointRec(m, r); }

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

Vector2 measure(const std::string &s, float size, bool bold = false) { return MeasureTextEx(bold ? gBold : gReg, s.c_str(), size, 0); }

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

void textShadow(const std::string &s, float x, float y, float size, Color c, Align al = CENTER)
{
    text(s, x + 2, y + 2, size, Fade(BLACK, 0.6f * c.a / 255.0f), al, true);
    text(s, x, y, size, c, al, true);
}

Font loadFont(const unsigned char *data, int size)
{
    std::vector<int> cps;
    for (int c = 32; c < 127; c++) cps.push_back(c);
    for (int c = 0x400; c < 0x460; c++) cps.push_back(c);
    for (int c : {0xAB, 0xBB, 0xD7, 0xB0, 0x2014, 0x2013, 0x2026, 0x2022, 0x2192, 0x2605, 0x221E, 0x2191, 0x2713, 0x25C6, 0x25B6, 0x2212})
        cps.push_back(c);
    Font f = LoadFontFromMemory(".ttf", data, size, 64, cps.data(), (int)cps.size());
    GenTextureMipmaps(&f.texture);
    SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
    return f;
}

std::string fmtTime(double s)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%02d", (int)s / 60, (int)s % 60);
    return buf;
}

// ---------- эффекты ----------
struct Particle {
    Vector2 p, v;  // мир
    float life, maxLife, size;  // size — пиксели экрана
    Color c;
};

struct FloatText {
    Vector2 p;  // экран
    float vy, life, maxLife, size;
    std::string s;
    Color c;
};

struct Shock {
    Vector2 c;  // мир
    float r, speed, life, maxLife, thick;  // пиксели экрана
    Color col;
};

struct Star {
    Vector2 p;
    float b, tw, depth, size;
};

enum class Screen { Intro, Run, RunEnd, Tree, Collapse, End };

struct App {
    Game game;
    Audio audio;
    Shader hole{};
    bool shaderOk = false;
    int locTime = -1, locPulse = -1, locFrenzy = -1, locHeat = -1;
    Texture2D white{};

    Screen screen = Screen::Intro;
    float t = 0, realTime = 0;
    float shake = 0, pulse = 0, flash = 0, hurtVignette = 0;
    Vector2 cam{0, 0}, starScroll{0, 0};
    float zoom = 2.5f;
    Vector2 camShake{};
    float collapseT = 0, endT = 0, runEndT = 0;
    double lastInterest = 0, runStartBest = 0;
    float bannerT = 0;
    std::string bannerTitle, bannerText;
    Color bannerColor = kGold;
    float eatSoundCd = 0;

    // созвездие
    Vector2 treeCam{0, 0};
    float treeZoom = 0.85f;
    bool dragging = false, pressed = false;
    Vector2 pressPos{}, pressCam{};
    std::vector<float> nodeFlash;

    std::vector<Particle> parts;
    std::vector<FloatText> floats;
    std::vector<Shock> shocks;
    std::vector<Star> stars;
};

App app;

Vector2 toScreen(float x, float y) { return {(x - app.cam.x) * app.zoom + kScreenC.x, (y - app.cam.y) * app.zoom + kScreenC.y}; }
Vector2 toScreen(Vec p) { return toScreen(p.x, p.y); }
Vec toWorld(Vector2 s) { return {(s.x - kScreenC.x) / app.zoom + app.cam.x, (s.y - kScreenC.y) / app.zoom + app.cam.y}; }

void addShake(float s) { app.shake = std::max(app.shake, s); }

void burstW(Vec p, int n, Color c, float speedPx, float sizePx, float life)
{
    for (int i = 0; i < n; i++) {
        float a = frand(0, 2 * PI), s = frand(0.3f, 1) * speedPx / app.zoom;
        app.parts.push_back({{p.x, p.y}, {std::cos(a) * s, std::sin(a) * s}, life * frand(0.6f, 1), life, sizePx * frand(0.5f, 1), c});
    }
}

void floatText(Vector2 screenP, const std::string &s, Color c, float size, float life = 1.1f)
{
    app.floats.push_back({screenP, -55, life, life, size, s, c});
    if (app.floats.size() > 40) app.floats.erase(app.floats.begin());
}

void shockW(Vec c, float speedPx, Color col, float life, float thick)
{
    app.shocks.push_back({{c.x, c.y}, 0, speedPx, life, life, thick, col});
}

void banner(const std::string &title, const std::string &body, Color c)
{
    app.bannerTitle = title;
    app.bannerText = body;
    app.bannerColor = c;
    app.bannerT = 3.0f;
}

const Color kTierColors[kTierCount] = {{190, 170, 150, 255}, {160, 140, 120, 255}, {170, 230, 255, 255}, {220, 220, 230, 255},
                                       {90, 170, 240, 255},  {230, 170, 110, 255}, {255, 220, 120, 255}, {255, 240, 200, 255},
                                       {200, 140, 255, 255}, {120, 200, 255, 255}, {255, 255, 255, 255}};

// ---------- рисование объектов (в начале координат, size — радиус в пикселях) ----------

void drawTierIcon(int tier, float size, float t, float alpha)
{
    auto A = [&](Color c) { return Fade(c, alpha * c.a / 255.0f); };
    switch (tier) {
    case 0: {
        static const Vector2 off[] = {{0, 0}, {-0.6f, -0.3f}, {0.5f, -0.5f}, {0.7f, 0.4f}, {-0.4f, 0.6f}, {0.1f, -0.9f}, {-0.9f, 0.2f}};
        for (int i = 0; i < 7; i++) DrawCircleV(Vector2Scale(off[i], size), size * (i == 0 ? 0.35f : 0.22f), A({190, 170, 150, 230}));
        break;
    }
    case 1: {
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
    case 2:  // комета
        DrawTriangle({-size * 0.4f, -size * 0.45f}, {-size * 3.2f, 0}, {-size * 0.4f, size * 0.45f}, A({140, 210, 255, 90}));
        DrawCircleGradient(0, 0, size * 1.3f, A({180, 230, 255, 140}), A({80, 160, 255, 0}));
        DrawCircleV({0, 0}, size * 0.6f, A({235, 250, 255, 255}));
        break;
    case 3:
        DrawCircleV({0, 0}, size, A({200, 200, 210, 255}));
        DrawCircleV({size * 0.3f, -size * 0.25f}, size * 0.25f, A({160, 160, 172, 255}));
        DrawCircleV({-size * 0.35f, size * 0.3f}, size * 0.18f, A({160, 160, 172, 255}));
        DrawCircleV({-size * 0.2f, -size * 0.45f}, size * 0.12f, A({160, 160, 172, 255}));
        break;
    case 4:
        DrawCircleV({0, 0}, size, A({70, 140, 220, 255}));
        DrawCircleSector({0, 0}, size, 200, 340, 16, A({80, 190, 120, 255}));
        DrawCircleSector({0, 0}, size, 20, 70, 10, A({80, 190, 120, 255}));
        DrawRing({0, 0}, size * 0.95f, size * 1.1f, 0, 360, 32, A({160, 210, 255, 90}));
        break;
    case 5:
        DrawCircleV({0, 0}, size * 0.85f, A({220, 160, 100, 255}));
        DrawRectangleV({-size * 0.8f, -size * 0.22f}, {size * 1.6f, size * 0.15f}, A({180, 110, 70, 255}));
        DrawRectangleV({-size * 0.75f, size * 0.15f}, {size * 1.5f, size * 0.13f}, A({240, 200, 150, 255}));
        DrawEllipseLines(0, 0, size * 1.45f, size * 0.34f, A({230, 210, 170, 255}));
        DrawEllipseLines(0, 0, size * 1.35f, size * 0.29f, A({230, 210, 170, 200}));
        break;
    case 6:
        DrawCircleGradient(0, 0, size * 1.6f, A({255, 230, 120, 140}), A({255, 120, 30, 0}));
        DrawCircleV({0, 0}, size * 0.8f, A({255, 240, 170, 255}));
        DrawCircleV({0, 0}, size * 0.5f, A({255, 255, 240, 255}));
        break;
    case 7:
        DrawCircleGradient(0, 0, size * 1.1f, A({255, 230, 200, 70}), A({255, 200, 150, 0}));
        for (int i = 0; i < 26; i++) {
            float a = i * 2.39996f, r = std::sqrt(i / 26.0f) * size;
            DrawCircleV({std::cos(a) * r, std::sin(a) * r}, size * (0.07f + 0.03f * (i % 3)),
                        A(i % 4 == 0 ? Color{180, 210, 255, 255} : Color{255, 240, 200, 255}));
        }
        break;
    case 8:
        DrawCircleGradient(0, 0, size * 1.1f, A({190, 120, 255, 90}), A({60, 20, 120, 0}));
        for (int arm = 0; arm < 2; arm++)
            for (int i = 0; i < 26; i++) {
                float k = i / 26.0f;
                float a = arm * PI + k * 4.2f + t * 0.3f;
                Vector2 p = {std::cos(a) * k * size, std::sin(a) * k * size * 0.75f};
                DrawCircleV(p, size * 0.06f * (1.2f - k), A(lerpColor({255, 230, 255, 255}, {150, 100, 255, 255}, k)));
            }
        DrawCircleV({0, 0}, size * 0.16f, A({255, 245, 230, 255}));
        break;
    case 9:
        DrawCircleGradient(0, 0, size * 1.1f, A({90, 150, 255, 50}), A({0, 0, 0, 0}));
        for (int i = 0; i < 9; i++) {
            float a1 = i * 0.9f, a2 = (i + 3) * 0.9f;
            Vector2 p1 = {std::cos(a1) * size * 0.9f, std::sin(a1) * size * 0.7f};
            Vector2 p2 = {std::cos(a2) * size * 0.5f, std::sin(a2) * size * 0.8f};
            DrawLineEx(p1, p2, std::max(1.0f, size * 0.04f), A({120, 180, 255, 140}));
            DrawCircleV(p1, size * 0.1f, A({170, 210, 255, 255}));
            DrawCircleV(p2, size * 0.07f, A({220, 170, 255, 255}));
        }
        break;
    case 10:  // Ядро Вселенной
        DrawCircleGradient(0, 0, size * 1.6f, A({255, 255, 255, 120}), A({200, 120, 255, 0}));
        for (int i = 0; i < 6; i++) {
            float a0 = t * 40 * (i % 2 ? 1 : -1) + i * 30;
            DrawRing({0, 0}, size * (0.3f + i * 0.12f), size * (0.34f + i * 0.12f), a0, a0 + 240, 48,
                     A(ColorFromHSV(std::fmod(t * 40 + i * 50, 360.0f), 0.5f, 1)));
        }
        DrawCircleV({0, 0}, size * 0.25f, A(WHITE));
        break;
    }
}

void pushTransform(Vector2 sp, float rotDeg, float stretch)
{
    rlPushMatrix();
    rlTranslatef(sp.x, sp.y, 0);
    rlRotatef(rotDeg, 0, 0, 1);
    rlScalef(stretch, 1 / std::sqrt(stretch), 1);
}

void drawObj(const Obj &o)
{
    Game &g = app.game;
    Vector2 sp = toScreen(o.p);
    float ss = o.size * app.zoom;
    float margin = ss * (o.kind == K_PULSAR ? 10 : 3) + 20;
    if (sp.x < -margin || sp.x > VW + margin || sp.y < -margin || sp.y > VH + margin) return;
    bool edible = g.canEat(o);
    float dx = g.pos.x - o.p.x, dy = g.pos.y - o.p.y;
    float d = std::sqrt(dx * dx + dy * dy) + 0.01f;
    float stretch = 1, rot = o.rot;
    if (o.pulled && d < g.R * 3.5f) {
        float k = Clamp(g.R / d, 0, 1.2f);
        stretch = 1 + 2.8f * k * k;
        rot = std::atan2(dy, dx) * RAD2DEG;
    }
    float alpha = Clamp((d - g.R * 0.75f) / (g.R * 0.35f), 0, 1);

    switch (o.kind) {
    case K_TIER: {
        if (o.tier == 2 && !o.pulled) rot = std::atan2(o.v.y, o.v.x) * RAD2DEG;  // хвост кометы назад
        pushTransform(sp, rot, stretch);
        drawTierIcon(o.tier, ss, app.t, alpha);
        rlPopMatrix();
        if (!edible && o.tier < 10) {
            // не по зубам — красная пунктирная обводка
            for (int k = 0; k < 12; k++) {
                float a0 = k * 30 + app.t * 15;
                DrawRing(sp, ss * 1.12f, ss * 1.12f + 2, a0, a0 + 16, 4, Fade(kBad, 0.55f));
            }
        }
        break;
    }
    case K_ANTI: {
        BeginBlendMode(BLEND_ADDITIVE);
        DrawCircleGradient((int)sp.x, (int)sp.y, ss * 1.8f, Fade({255, 40, 120, 255}, 0.5f), {0, 0, 0, 0});
        EndBlendMode();
        for (int k = 0; k < 8; k++) {
            float a = (k * 45 + o.rot * 3) * DEG2RAD;
            Vector2 tip = {sp.x + std::cos(a) * ss * 1.3f, sp.y + std::sin(a) * ss * 1.3f};
            Vector2 l = {sp.x + std::cos(a + 0.35f) * ss * 0.6f, sp.y + std::sin(a + 0.35f) * ss * 0.6f};
            Vector2 r = {sp.x + std::cos(a - 0.35f) * ss * 0.6f, sp.y + std::sin(a - 0.35f) * ss * 0.6f};
            DrawTriangle(r, l, tip, {255, 60, 130, 255});
            DrawTriangle(l, r, tip, {255, 60, 130, 255});
        }
        DrawCircleV(sp, ss * 0.65f, {120, 0, 50, 255});
        DrawCircleV(sp, ss * 0.35f, edible ? kGood : Color{255, 120, 180, 255});
        break;
    }
    case K_PULSAR: {
        float bl = o.size * 6 * app.zoom;
        BeginBlendMode(BLEND_ADDITIVE);
        for (float b : {o.beam, o.beam + PI}) {
            Vector2 dir = {std::cos(b), std::sin(b)}, n = {-dir.y, dir.x};
            Vector2 tip = Vector2Add(sp, Vector2Scale(dir, bl));
            Vector2 a1 = Vector2Add(sp, Vector2Scale(n, ss * 0.3f)), a2 = Vector2Subtract(sp, Vector2Scale(n, ss * 0.3f));
            Color bc = edible ? Color{150, 200, 255, 120} : Color{255, 120, 160, 150};
            DrawTriangle(a1, a2, tip, bc);
            DrawTriangle(a2, a1, tip, bc);
        }
        DrawCircleGradient((int)sp.x, (int)sp.y, ss * 1.6f, Fade({170, 210, 255, 255}, 0.6f), {0, 0, 0, 0});
        EndBlendMode();
        DrawCircleV(sp, ss * 0.5f, {240, 248, 255, 255});
        if (!edible)
            for (int k = 0; k < 12; k++) {
                float a0 = k * 30 + app.t * 15;
                DrawRing(sp, ss * 1.2f, ss * 1.2f + 2, a0, a0 + 16, 4, Fade(kBad, 0.55f));
            }
        break;
    }
    case K_RIVAL: {
        Color rc = edible ? kGood : kRival;
        BeginBlendMode(BLEND_ADDITIVE);
        DrawCircleGradient((int)sp.x, (int)sp.y, ss * 2.6f, Fade(rc, 0.3f), {0, 0, 0, 0});
        EndBlendMode();
        DrawCircleV(sp, ss, BLACK);
        for (int i = 0; i < 3; i++)
            DrawRing(sp, ss * (1.05f + i * 0.18f), ss * (1.12f + i * 0.18f), app.t * 180 + i * 70, app.t * 180 + i * 70 + 230, 32,
                     Fade(rc, 0.85f - i * 0.2f));
        textShadow(edible ? "ЕДА" : "СОПЕРНИК", sp.x, sp.y - ss * 1.7f - 16, 13, rc);
        break;
    }
    case K_CLOCK: {
        BeginBlendMode(BLEND_ADDITIVE);
        DrawCircleGradient((int)sp.x, (int)sp.y, ss * 2, Fade(kTimeC, 0.5f), {0, 0, 0, 0});
        EndBlendMode();
        DrawCircleV(sp, ss, {20, 60, 80, 255});
        DrawRing(sp, ss * 0.85f, ss, 0, 360, 24, kTimeC);
        float a = app.t * 4;
        DrawLineEx(sp, {sp.x + std::cos(a) * ss * 0.7f, sp.y + std::sin(a) * ss * 0.7f}, 2, kTimeC);
        DrawLineEx(sp, {sp.x + std::cos(a * 0.1f) * ss * 0.45f, sp.y + std::sin(a * 0.1f) * ss * 0.45f}, 3, kTimeC);
        break;
    }
    case K_DARK: {
        BeginBlendMode(BLEND_ADDITIVE);
        DrawCircleGradient((int)sp.x, (int)sp.y, ss * 2.2f, Fade(kDarkC, 0.55f), {0, 0, 0, 0});
        EndBlendMode();
        DrawPoly(sp, 4, ss, app.t * 60, kDarkC);
        DrawPoly(sp, 4, ss * 0.5f, app.t * 60, Fade(WHITE, 0.7f));
        break;
    }
    case K_GOLD: {
        float a = std::atan2(o.v.y, o.v.x);
        Vector2 back = {sp.x - std::cos(a) * ss * 5, sp.y - std::sin(a) * ss * 5};
        BeginBlendMode(BLEND_ADDITIVE);
        DrawLineEx(back, sp, ss * 0.8f, Fade(kGold, 0.5f));
        DrawCircleGradient((int)sp.x, (int)sp.y, ss * 2.4f, Fade(kGold, 0.6f), {0, 0, 0, 0});
        EndBlendMode();
        DrawCircleV(sp, ss * 0.6f, {255, 250, 220, 255});
        if (std::fmod(app.t, 0.5f) < 0.33f) textShadow("ЗОЛОТАЯ КОМЕТА", sp.x, sp.y - ss * 2 - 14, 13, kGold);
        break;
    }
    }
}

// ---------- сцена ----------

void initStars()
{
    app.stars.clear();
    for (int i = 0; i < 380; i++)
        app.stars.push_back({{frand(0, VW), frand(0, VH)}, frand(0.25f, 1), frand(0, 10), frand(0.03f, 0.35f), frand(0.6f, 1.8f)});
}

float wrapf(float v, float m) { return v - std::floor(v / m) * m; }

void drawBackground(Vector2 holeScreen, float holeRs, bool lens)
{
    ClearBackground(kBg);
    float t = app.t;
    Vector2 sc = app.starScroll;
    DrawCircleGradient((int)wrapf(260 - sc.x * 0.02f, VW + 600) - 300, (int)wrapf(220 - sc.y * 0.02f, VH + 600) - 300, 420, {70, 30, 120, 70}, {0, 0, 0, 0});
    DrawCircleGradient((int)wrapf(1050 - sc.x * 0.03f, VW + 600) - 300, (int)wrapf(520 - sc.y * 0.03f, VH + 600) - 300, 460, {20, 60, 120, 70}, {0, 0, 0, 0});
    DrawCircleGradient((int)wrapf(700 - sc.x * 0.015f, VW + 600) - 300, (int)wrapf(120 - sc.y * 0.015f, VH + 600) - 300, 300, {120, 40, 80, 45}, {0, 0, 0, 0});
    float E = holeRs * 1.55f;
    for (auto &s : app.stars) {
        Vector2 p = {wrapf(s.p.x - sc.x * s.depth, VW), wrapf(s.p.y - sc.y * s.depth, VH)};
        float tw = 0.65f + 0.35f * std::sin(t * 2 + s.tw);
        Color c = Fade(lerpColor({170, 190, 255, 255}, {255, 240, 220, 255}, s.tw / 10), s.b * tw);
        if (lens) {
            Vector2 d = Vector2Subtract(p, holeScreen);
            float r = Vector2Length(d) + 0.001f;
            float rl = (r + std::sqrt(r * r + 4 * E * E)) / 2;
            float stretch = Clamp(rl / r, 1, 3);
            p = Vector2Add(holeScreen, Vector2Scale(d, rl / r));
            if (stretch > 1.3f) {
                Vector2 tang = Vector2Normalize({-d.y, d.x});
                DrawLineEx(Vector2Add(p, Vector2Scale(tang, s.size * stretch)), Vector2Subtract(p, Vector2Scale(tang, s.size * stretch)), s.size * 0.8f, c);
                continue;
            }
        }
        DrawCircleV(p, s.size * 0.6f, c);
    }
}

void drawHoleAt(Vector2 sp, float Rs, float heat, float frenzy)
{
    DrawCircleV(sp, Rs, BLACK);
    float quad = Rs * 4.4f;
    BeginBlendMode(BLEND_ADDITIVE);
    if (app.shaderOk) {
        float tt = app.t, pu = app.pulse;
        SetShaderValue(app.hole, app.locTime, &tt, SHADER_UNIFORM_FLOAT);
        SetShaderValue(app.hole, app.locPulse, &pu, SHADER_UNIFORM_FLOAT);
        SetShaderValue(app.hole, app.locFrenzy, &frenzy, SHADER_UNIFORM_FLOAT);
        SetShaderValue(app.hole, app.locHeat, &heat, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(app.hole);
        DrawTexturePro(app.white, {0, 0, 1, 1}, {sp.x - quad, sp.y - quad, quad * 2, quad * 2}, {0, 0}, 0, WHITE);
        EndShaderMode();
    } else {
        DrawRing(sp, Rs, Rs * 1.08f, 0, 360, 64, {255, 200, 140, 255});
        DrawRing(sp, Rs * 1.6f, Rs * 3.5f, 0, 360, 64, Fade(kAccent, 0.25f));
    }
    EndBlendMode();
}

void drawEffects()
{
    BeginBlendMode(BLEND_ADDITIVE);
    for (auto &s : app.shocks) {
        float k = s.life / s.maxLife;
        DrawRing(toScreen(s.c.x, s.c.y), s.r, s.r + s.thick * k, 0, 360, 96, Fade(s.col, k * 0.8f));
    }
    for (auto &p : app.parts) {
        float k = p.life / p.maxLife;
        DrawCircleV(toScreen(p.p.x, p.p.y), p.size * (0.4f + 0.6f * k), Fade(p.c, std::min(1.0f, k * 1.5f)));
    }
    EndBlendMode();
}

void drawFloats()
{
    for (auto &f : app.floats) {
        float k = f.life / f.maxLife;
        float s = f.size * (k > 0.85f ? 1 + (k - 0.85f) * 3 : 1);
        textShadow(f.s, f.p.x, f.p.y, s, Fade(f.c, std::min(1.0f, k * 2)));
    }
}

void drawBanner()
{
    if (app.bannerT <= 0) return;
    float k = Clamp(std::min(app.bannerT, 3.0f - app.bannerT) * 3, 0, 1);
    textGlow(app.bannerTitle, 640, 96, 20, Fade(app.bannerColor, k), CENTER, Fade(kAccent, k));
    textGlow(app.bannerText, 640, 120, 30, Fade(kText, k), CENTER, Fade(app.bannerColor, k));
}

// ---------- заход ----------

float screenR() { return app.game.R * app.zoom; }

void drawRunWorld()
{
    Game &g = app.game;
    Vector2 hs = toScreen(g.pos);
    float Rs = screenR();
    drawBackground(hs, Rs, true);

    // радиус притяжения
    float pullRs = g.R * 2.4f * (float)g.stats().pull * app.zoom;
    for (int k = 0; k < 48; k++) {
        float a0 = k * 7.5f + app.t * 6;
        DrawRing(hs, pullRs - 1, pullRs, a0, a0 + 3.5f, 2, Fade(kDim, 0.18f));
    }
    if (g.collapseLeft > 0) DrawCircleGradient((int)hs.x, (int)hs.y, 900, Fade({150, 125, 255, 255}, 0.18f), {0, 0, 0, 0});

    for (auto &o : g.objs) drawObj(o);

    float heat = Clamp(std::log10(g.R + 1) / 3.0f, 0, 1);
    drawHoleAt(hs, Rs, heat, g.dashLeft > 0 ? 1.0f : 0.0f);

    // спутники
    const Stats &st = g.stats();
    for (int s = 0; s < st.sats; s++) {
        float a = g.satAngle + s * 2 * PI / st.sats;
        Vector2 p = {hs.x + std::cos(a) * Rs * 2.0f, hs.y + std::sin(a) * Rs * 2.0f};
        float r = Rs * 0.22f * (float)st.satSize;
        DrawCircleV(p, r, BLACK);
        DrawRing(p, r, r * 1.25f, 0, 360, 24, Fade(kAccent, 0.9f));
    }
    drawEffects();
}

void drawCoreArrow()
{
    Game &g = app.game;
    for (auto &o : g.objs) {
        if (o.kind != K_TIER || o.tier != 10) continue;
        Vector2 sp = toScreen(o.p);
        bool on = sp.x > 0 && sp.x < VW && sp.y > 0 && sp.y < VH;
        bool edible = g.canEat(o);
        Color c = edible ? kGood : kGold;
        if (!on) {
            Vector2 d = Vector2Normalize(Vector2Subtract(sp, kScreenC));
            float k = std::min((VW / 2 - 60) / std::max(0.01f, std::fabs(d.x)), (VH / 2 - 60) / std::max(0.01f, std::fabs(d.y)));
            Vector2 p = Vector2Add(kScreenC, Vector2Scale(d, k));
            Vector2 n = {-d.y, d.x};
            Vector2 tip = Vector2Add(p, Vector2Scale(d, 22));
            DrawTriangle(Vector2Add(p, Vector2Scale(n, 12)), Vector2Subtract(p, Vector2Scale(n, 12)), tip, c);
            DrawTriangle(Vector2Subtract(p, Vector2Scale(n, 12)), Vector2Add(p, Vector2Scale(n, 12)), tip, c);
            textShadow("ЯДРО ВСЕЛЕННОЙ", p.x - d.x * 40, p.y - d.y * 40 - 10, 14, c);
        }
        if (!edible) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "Ядро: нужен размер %.0f (сейчас %.0f)", o.size / g.stats().eat, g.R);
            textShadow(buf, 640, 682, 15, kGold);
        }
    }
}

void drawRunHud()
{
    Game &g = app.game;
    const Stats &st = g.stats();
    // таймер испарения
    float maxT = (float)std::max(st.time * 2, g.timeLeft);
    float k = (float)(g.timeLeft / st.time);
    Rectangle bar = {380, 16, 520, 18};
    DrawRectangleRounded(bar, 1, 8, {20, 18, 44, 220});
    Color tc = g.timeLeft < 5 ? lerpColor(kBad, WHITE, 0.5f + 0.5f * std::sin(app.t * 14)) : lerpColor(kBad, kTimeC, Clamp(k * 2, 0, 1));
    float w = bar.width * Clamp((float)g.timeLeft / maxT * 2, 0, 1);
    if (w > 2) DrawRectangleRounded({bar.x, bar.y, w, bar.height}, 1, 8, tc);
    char buf[128];
    std::snprintf(buf, sizeof buf, "%.1f с до испарения", std::max(0.0, g.timeLeft));
    textShadow(buf, 640, bar.y + 22, 16, tc);
    if (g.hurtFlash > 0) DrawRectangleRoundedLinesEx(bar, 1, 8, 3, Fade(kBad, (float)g.hurtFlash));

    // масса
    DrawRectangleGradientH(0, 0, 360, 100, Fade(kBg, 0.85f), {0, 0, 0, 0});
    textGlow("+" + fmtNum(g.runMass), 24, 14, 34, kText, LEFT, {150, 125, 255, 255});
    text("за заход  •  всего " + fmtNum(g.mass), 26, 54, 15, kDim);
    std::snprintf(buf, sizeof buf, "Размер %.0f  •  рекорд %.0f", g.R, g.bestR);
    text(buf, 26, 74, 15, kDim);

    // тёмная материя
    DrawPoly({VW - 34, 30}, 4, 10, app.t * 40, kDarkC);
    text(fmtNum(g.dark), VW - 52, 19, 22, kDarkC, RIGHT, true);
    std::snprintf(buf, sizeof buf, "заход %d", g.runs);
    text(buf, VW - 24, 48, 14, kDim, RIGHT);

    // комбо
    if (g.combo >= 3) {
        double cm = std::min(st.comboMax, 1.0 + g.combo * 0.03);
        std::snprintf(buf, sizeof buf, "ПИР ×%.2f", cm);
        Vector2 hs = toScreen(g.pos);
        textGlow(buf, hs.x, hs.y + screenR() * 1.3f + 18, 18 + 6 * (float)((cm - 1) / 3), kAccent, CENTER, kBad);
    }

    // способности
    float x = 640 - (st.dash && st.collapse ? 110 : 52);
    auto ability = [&](const char *key, const char *name, double cd, double full, Color c, double active) {
        Rectangle r = {x, 638, 104, 54};
        DrawRectangleRounded(r, 0.3f, 8, {24, 20, 52, 220});
        if (active > 0) DrawRectangleRounded(r, 0.3f, 8, Fade(c, 0.35f));
        else if (cd > 0) DrawRectangleRounded({r.x, r.y, r.width * (float)(cd / full), r.height}, 0.3f, 8, Fade(BLACK, 0.5f));
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, cd <= 0 ? 2 : 1, Fade(c, cd <= 0 ? 0.9f : 0.4f));
        text(key, r.x + 52, r.y + 5, 13, Fade(c, 0.9f), CENTER, true);
        text(cd > 0 ? std::to_string((int)std::ceil(cd)) + " с" : name, r.x + 52, r.y + 25, 16, cd > 0 ? kDim : kText, CENTER, true);
        x += 116;
    };
    if (st.dash) ability("ПРОБЕЛ / ПКМ", "Рывок", g.dashCd, st.dashCd, {150, 125, 255, 255}, g.dashLeft);
    if (st.collapse) ability("Q", "Коллапс", g.collapseCd, st.collapseCd, kAccent, g.collapseLeft);

    drawCoreArrow();

    if (g.runs == 1 && g.runTime < 9)
        textShadow("Веди дыру мышкой и поглощай всё, что меньше тебя", 640, 600, 19, Fade(kText, 0.9f));
    else if (g.runs == 1 && g.runTime < 15)
        textShadow("Красная обводка — пока не по зубам. Подрасти!", 640, 600, 19, Fade(kText, 0.9f));
    else if (g.runs == 3 && g.runTime < 6)
        textShadow("Осторожно: антиматерия и крупные дыры-соперники отнимают время", 640, 600, 17, kBad);

    if (app.hurtVignette > 0) {
        DrawRectangleGradientV(0, 0, (int)VW, 120, Fade(kBad, app.hurtVignette * 0.4f), {0, 0, 0, 0});
        DrawRectangleGradientV(0, (int)VH - 120, (int)VW, 120, {0, 0, 0, 0}, Fade(kBad, app.hurtVignette * 0.4f));
    }
}

void drawRunEnd()
{
    Game &g = app.game;
    float k = Clamp(app.runEndT * 3, 0, 1);
    DrawRectangle(0, 0, (int)VW, (int)VH, Fade(BLACK, 0.55f * k));
    Rectangle r = {390, 140 + (1 - k) * 40, 500, 420};
    DrawRectangleRounded(r, 0.08f, 8, Fade({14, 12, 32, 255}, 0.95f * k));
    DrawRectangleRoundedLinesEx(r, 0.08f, 8, 2, Fade(kTimeC, k));
    textGlow("ДЫРА ИСПАРИЛАСЬ", 640, r.y + 22, 32, Fade(kText, k), CENTER, Fade(kTimeC, k));
    text("Излучение Хокинга забрало остатки. Но масса — твоя.", 640, r.y + 66, 15, Fade(kDim, k), CENTER);
    textGlow("+" + fmtNum(g.runMass), 640, r.y + 96, 46, Fade(kGold, k), CENTER, Fade(kAccent, k));
    float y = r.y + 168;
    auto row = [&](const std::string &a, const std::string &b, Color c = kText) {
        text(a, r.x + 50, y, 17, Fade(kText, k));
        text(b, r.x + r.width - 50, y, 17, Fade(c, k), RIGHT, true);
        y += 28;
    };
    row("Поглощено объектов", std::to_string(g.runEaten));
    row("Крупнейшая добыча", g.biggestTier >= 0 ? kTiers[g.biggestTier].name : "—");
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.0f", g.bestR);
    row("Рекорд размера", buf, g.bestR > app.runStartBest ? kGood : kText);
    if (app.lastInterest > 0) row("Сложный процент", "+" + fmtNum(app.lastInterest), kGold);
    row("Всего массы", fmtNum(g.mass));
    int aff = g.affordableCount();
    if (aff > 0) {
        std::snprintf(buf, sizeof buf, "Можно купить улучшений: %d", aff);
        text(buf, 640, r.y + r.height - 72, 17, Fade(kGood, k), CENTER, true);
    }
    if (std::fmod(app.t, 1.0f) < 0.7f) text("Клик или Пробел — в созвездие прокачки", 640, r.y + r.height - 42, 17, Fade(kText, k), CENTER, true);
}

// ---------- созвездие прокачки ----------

Vector2 nodeWorld(int i)
{
    const NodeDef &n = kNodes[i];
    if (i == 0) return {0, 0};
    float a = (branchAngle(n.branch) + n.offset) * DEG2RAD;
    return {std::cos(a) * n.ring * kRingStep, std::sin(a) * n.ring * kRingStep};
}

Vector2 treeToScreen(Vector2 w) { return {(w.x - app.treeCam.x) * app.treeZoom + kScreenC.x, (w.y - app.treeCam.y) * app.treeZoom + kScreenC.y}; }

void drawStatIcon(Stat s, Vector2 p, float r, Color c)
{
    float t = app.t;
    switch (s) {
    case ST_SIZE: case ST_GROWTH:
        DrawCircleV(p, r * (s == ST_SIZE ? 0.42f : 0.25f), BLACK);
        DrawRing(p, r * (s == ST_SIZE ? 0.42f : 0.25f), r * (s == ST_SIZE ? 0.52f : 0.33f), 0, 360, 24, c);
        if (s == ST_GROWTH)
            for (int k = 0; k < 4; k++) {
                float a = k * PI / 2 + PI / 4;
                Vector2 o = {p.x + std::cos(a) * r * 0.62f, p.y + std::sin(a) * r * 0.62f};
                DrawLineEx(Vector2Lerp(p, o, 0.6f), o, 2, c);
            }
        break;
    case ST_SPEED: case ST_DASH: case ST_DASHCD: case ST_PHANTOM:
        for (int k = 0; k < 2; k++) {
            float ox = (k - 0.5f) * r * 0.4f;
            DrawTriangle({p.x + ox - r * 0.25f, p.y - r * 0.4f}, {p.x + ox - r * 0.25f, p.y + r * 0.4f}, {p.x + ox + r * 0.3f, p.y}, c);
        }
        break;
    case ST_PULL: case ST_PULLSTR: case ST_COLLAPSE: case ST_COLLAPSEPOW:
        for (int k = 0; k < 3; k++) DrawRing(p, r * (0.18f + k * 0.18f), r * (0.24f + k * 0.18f), t * 60 * (k + 1), t * 60 * (k + 1) + 260, 24, c);
        break;
    case ST_EAT: case ST_ANTIEAT:
        DrawCircleSector(p, r * 0.55f, 30, 330, 24, c);
        DrawCircleV({p.x + r * 0.12f, p.y - r * 0.22f}, r * 0.08f, BLACK);
        break;
    case ST_TIME: case ST_CLOCK: case ST_CLOCKVAL: case ST_TIMEFEED: case ST_LOOP:
        DrawRing(p, r * 0.45f, r * 0.55f, 0, 360, 24, c);
        DrawLineEx(p, {p.x, p.y - r * 0.38f}, 2, c);
        DrawLineEx(p, {p.x + r * 0.28f, p.y}, 2, c);
        break;
    case ST_ARMOR:
        DrawPoly(p, 6, r * 0.5f, 30, c);
        DrawPoly(p, 6, r * 0.3f, 30, BLACK);
        break;
    case ST_VALUE: case ST_VALUEX: case ST_TIERVAL: case ST_INTEREST:
        DrawPoly(p, 4, r * 0.5f, 45, c);
        DrawPoly(p, 4, r * 0.25f, 45, Fade(WHITE, 0.6f));
        break;
    case ST_CRIT: case ST_CRITMULT: case ST_COMBO: case ST_COMBOWIN:
        for (int k = 0; k < 5; k++) {
            float a = k * 2 * PI / 5 - PI / 2;
            DrawLineEx(p, {p.x + std::cos(a) * r * 0.55f, p.y + std::sin(a) * r * 0.55f}, 3, c);
        }
        break;
    case ST_DENSITY: case ST_RICH: case ST_SWARM:
        for (int k = 0; k < 5; k++) DrawCircleV({p.x + std::cos(k * 1.3f) * r * 0.35f, p.y + std::sin(k * 1.3f) * r * 0.35f}, r * 0.12f, c);
        break;
    case ST_GOLD: case ST_CHAIN: case ST_MAGNET: case ST_RIVAL:
        DrawPoly(p, 4, r * 0.55f, t * 30, c);
        DrawPoly(p, 4, r * 0.32f, t * 30 + 45, Fade(WHITE, 0.7f));
        break;
    case ST_SAT:
        DrawCircleV(p, r * 0.22f, BLACK);
        DrawRing(p, r * 0.22f, r * 0.3f, 0, 360, 16, c);
        DrawCircleV({p.x + std::cos(t * 2) * r * 0.5f, p.y + std::sin(t * 2) * r * 0.5f}, r * 0.11f, c);
        break;
    case ST_SATSIZE:
        DrawCircleV({p.x + std::cos(t * 2) * r * 0.42f, p.y + std::sin(t * 2) * r * 0.42f}, r * 0.2f, c);
        break;
    case ST_DARK:
        DrawPoly(p, 4, r * 0.5f, 0, c);
        break;
    case ST_FINAL:
        drawTierIcon(10, r * 0.5f, t, 1);
        break;
    }
}

std::string costStr(int i)
{
    return (kNodes[i].branch == Branch::Dark ? "◆ " : "") + fmtNum(app.game.nodeCost(i));
}

void drawTree(Vector2 m, int &hovered)
{
    Game &g = app.game;
    drawBackground({-1000, -1000}, 0, false);
    hovered = -1;
    if (app.nodeFlash.size() != kNodes.size()) app.nodeFlash.assign(kNodes.size(), 0);
    float z = app.treeZoom;

    // связи
    for (size_t i = 1; i < kNodes.size(); i++) {
        if (!g.nodeVisible((int)i)) continue;
        Vector2 a = treeToScreen(nodeWorld(kNodes[i].parent)), b = treeToScreen(nodeWorld((int)i));
        Color c = branchColor(kNodes[i].branch);
        if (g.level((int)i) > 0) {
            DrawLineEx(a, b, 3.5f * z, Fade(c, 0.85f));
            float k = std::fmod(app.t * 0.6f + i * 0.17f, 1.0f);
            BeginBlendMode(BLEND_ADDITIVE);
            DrawCircleV(Vector2Lerp(a, b, k), 3 * z, WHITE);
            EndBlendMode();
        } else {
            for (int s = 0; s < 12; s += 2) DrawLineEx(Vector2Lerp(a, b, s / 12.0f), Vector2Lerp(a, b, (s + 1) / 12.0f), 2 * z, Fade(c, 0.45f));
        }
    }
    // подписи веток
    for (Branch b : {Branch::Gravity, Branch::Growth, Branch::Time, Branch::Wealth, Branch::Cosmos, Branch::Dark}) {
        float a = branchAngle(b) * DEG2RAD;
        Vector2 p = treeToScreen({std::cos(a) * kRingStep * 0.55f, std::sin(a) * kRingStep * 0.55f});
        Color c = branchColor(b);
        Vector2 ms = measure(branchName(b), 13 * std::max(0.8f, z), true);
        DrawRectangleRounded({p.x - ms.x / 2 - 8, p.y - ms.y / 2 - 3, ms.x + 16, ms.y + 6}, 0.6f, 6, Fade({10, 8, 24, 255}, 0.85f));
        text(branchName(b), p.x, p.y - ms.y / 2, 13 * std::max(0.8f, z), c, CENTER, true);
    }

    for (size_t i = 0; i < kNodes.size(); i++) {
        if (!g.nodeVisible((int)i)) continue;
        const NodeDef &n = kNodes[i];
        Vector2 p = treeToScreen(nodeWorld((int)i));
        float r = (i == 0 ? 34 : 22) * z;
        if (p.x < -r || p.x > VW + r || p.y < -r || p.y > VH + r) continue;
        Color c = branchColor(n.branch);
        int lv = g.level((int)i);
        bool maxed = lv >= n.maxLevel, can = g.canBuy((int)i);
        bool hov = CheckCollisionPointCircle(m, p, r + 3);
        if (hov) hovered = (int)i;
        if (i == 0) {
            drawHoleAt(p, r * 0.7f, 0.3f, 0);
            continue;
        }
        if (lv > 0) {
            BeginBlendMode(BLEND_ADDITIVE);
            DrawCircleGradient((int)p.x, (int)p.y, r * 2.1f, Fade(c, maxed ? 0.45f : 0.25f), {0, 0, 0, 0});
            EndBlendMode();
        }
        DrawCircleV(p, r, lv > 0 ? lerpColor(c, {16, 12, 34, 255}, maxed ? 0.35f : 0.65f) : Color{20, 16, 42, 255});
        // дуга уровня
        DrawRing(p, r - 3 * z, r, 0, 360, 36, Fade(c, 0.25f));
        if (lv > 0) DrawRing(p, r - 3 * z, r, -90, -90 + 360.0f * lv / n.maxLevel, 36, c);
        if (can) DrawRing(p, r + 2 * z, r + 4.5f * z, 0, 360, 36, Fade(kGood, 0.55f + 0.45f * std::sin(app.t * 5)));
        drawStatIcon(n.stat, p, r, lv > 0 ? WHITE : Fade(c, 0.85f));
        if (app.nodeFlash[i] > 0) {
            BeginBlendMode(BLEND_ADDITIVE);
            DrawCircleV(p, r * (1 + 1.5f * (1 - app.nodeFlash[i])), Fade(WHITE, app.nodeFlash[i] * 0.6f));
            EndBlendMode();
        }
        if (n.maxLevel > 1 || lv == 0) {
            std::string lt = maxed ? "МАКС" : std::to_string(lv) + "/" + std::to_string(n.maxLevel);
            text(lt, p.x, p.y + r + 2, 11 * std::max(0.85f, z), maxed ? c : can ? kGood : kDim, CENTER, true);
        }
    }

    // верхняя панель
    DrawRectangle(0, 0, (int)VW, 64, Fade(kBg, 0.85f));
    DrawLine(0, 64, (int)VW, 64, kPanelEdge);
    textGlow(fmtNum(g.mass), 24, 10, 34, kText, LEFT, {150, 125, 255, 255});
    text("массы", 28 + measure(fmtNum(g.mass), 34, true).x, 26, 15, kDim);
    DrawPoly({330, 31}, 4, 10, app.t * 40, kDarkC);
    text(fmtNum(g.dark) + " тёмной материи", 348, 21, 18, kDarkC, LEFT, true);
    textGlow("СОЗВЕЗДИЕ ПРОКАЧКИ", 640 + 120, 8, 24, kText, CENTER, {150, 125, 255, 255});
    char buf[160];
    std::snprintf(buf, sizeof buf, "заходов: %d   •   рекорд размера: %.0f   •   время: %s", g.runs, g.bestR, fmtTime(app.realTime).c_str());
    text(buf, 640 + 120, 38, 14, kDim, CENTER);

    // статы слева
    const Stats &st = g.stats();
    Rectangle pr = {14, 78, 236, 330};
    DrawRectangleRounded(pr, 0.06f, 8, kPanel);
    DrawRectangleRoundedLinesEx(pr, 0.06f, 8, 1.2f, kPanelEdge);
    text("ТВОЯ ДЫРА", pr.x + 14, pr.y + 10, 15, kDim, LEFT, true);
    float y = pr.y + 36;
    auto row = [&](const char *a, const std::string &b) {
        text(a, pr.x + 14, y, 14, kText);
        text(b, pr.x + pr.width - 14, y, 14, kGood, RIGHT, true);
        y += 21;
    };
    std::snprintf(buf, sizeof buf, "%.0f", st.size); row("Стартовый размер", buf);
    std::snprintf(buf, sizeof buf, "%.0f с", st.time); row("Время захода", buf);
    std::snprintf(buf, sizeof buf, "×%.2f", st.speed); row("Скорость", buf);
    std::snprintf(buf, sizeof buf, "×%.2f", st.pull); row("Притяжение", buf);
    std::snprintf(buf, sizeof buf, "до %.0f%% себя", st.eat * 100); row("Можно съесть", buf);
    std::snprintf(buf, sizeof buf, "×%.2f", st.growth); row("Рост", buf);
    row("Масса от всего", "×" + fmtNum(st.value));
    std::snprintf(buf, sizeof buf, "×%.1f", st.comboMax); row("Предел пира", buf);
    std::snprintf(buf, sizeof buf, "%.0f%% ×%.0f", st.crit * 100, st.critMult); row("Крит", buf);
    std::snprintf(buf, sizeof buf, "%d", st.sats); row("Спутники", buf);
    row("Рывок / коллапс", std::string(st.dash ? "да" : "нет") + " / " + (st.collapse ? "да" : "нет"));
    std::snprintf(buf, sizeof buf, "%.0f%%", st.armor * 100); row("Защита", buf);

    text("Колесо — масштаб, перетаскивай — двигать", 20, VH - 30, 13, Fade(kDim, 0.8f));

    // кнопка старта
    bool sh = hover(kStartButton, m);
    float pp = 0.5f + 0.5f * std::sin(app.t * 4);
    DrawRectangleRounded(kStartButton, 0.35f, 10, sh ? Color{90, 60, 190, 255} : lerpColor({50, 34, 120, 255}, {70, 50, 160, 255}, pp));
    DrawRectangleRoundedLinesEx(kStartButton, 0.35f, 10, 2.5f, Fade(kTimeC, 0.6f + 0.4f * pp));
    text("НАЧАТЬ ЗАХОД ▶", kStartButton.x + kStartButton.width / 2, kStartButton.y + 10, 22, kText, CENTER, true);
    text("Пробел / Enter", kStartButton.x + kStartButton.width / 2, kStartButton.y + 38, 13, kDim, CENTER);

    if (hovered > 0) {
        const NodeDef &n = kNodes[hovered];
        int lv = g.level(hovered);
        std::string title = n.name;
        std::string body = std::string(n.desc) + (n.maxLevel > 1 ? "  (за уровень)" : "");
        std::snprintf(buf, sizeof buf, "Уровень %d/%d", lv, n.maxLevel);
        std::string foot = lv >= n.maxLevel ? std::string(buf) + "  •  максимум" : std::string(buf) + "  •  цена " + costStr(hovered) + (g.canBuy(hovered) ? "  — кликни" : "  — не хватает");
        float w = std::max({measure(title, 18, true).x, measure(body, 15).x, measure(foot, 15, true).x}) + 28;
        float x = std::min(m.x + 18, VW - w - 8), ty = std::min(m.y + 18, VH - 96);
        Color c = branchColor(n.branch);
        DrawRectangleRounded({x, ty, w, 86}, 0.15f, 6, {12, 10, 30, 248});
        DrawRectangleRoundedLinesEx({x, ty, w, 86}, 0.15f, 6, 1.5f, c);
        text(title, x + 14, ty + 9, 18, c, LEFT, true);
        text(body, x + 14, ty + 35, 15, kText);
        text(foot, x + 14, ty + 59, 15, g.canBuy(hovered) ? kGood : kDim, LEFT, true);
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    }
}

// ---------- события игры ----------

void handleEvents()
{
    Game &g = app.game;
    for (const Event &e : g.events) {
        switch (e.type) {
        case EvType::Eat: case EvType::SatEat: {
            bool sat = e.type == EvType::SatEat;
            Color c = e.index >= 0 ? kTierColors[std::min(e.index, kTierCount - 1)] : kDarkC;
            float rel = e.size / std::max(1.0f, g.R);
            burstW(e.p, sat ? 4 : 6 + (int)(rel * 18), c, 120 + rel * 200, 2.5f + rel * 3, 0.6f);
            if (!sat) app.pulse = std::min(1.0f, app.pulse + 0.3f + rel);
            if (app.eatSoundCd <= 0 || rel > 0.4f) {
                float pitch = Clamp(1.5f - rel, 0.6f, 1.6f) + std::min(0.4f, g.combo * 0.01f);
                app.audio.play(e.crit ? SFX_CRIT : SFX_CLICK, pitch, sat ? 0.3f : 0.4f + rel * 0.6f);
                app.eatSoundCd = 0.05f;
            }
            if (e.crit || rel > 0.35f) {
                Vector2 sp = toScreen(e.p);
                floatText({sp.x, sp.y - 10}, (e.crit ? "КРИТ +" : "+") + fmtNum(e.value), e.crit ? kAccent : kText, e.crit ? 24 : 16 + rel * 10);
            }
            if (rel > 0.6f) addShake(3 + rel * 4);
            break;
        }
        case EvType::Hurt: {
            app.audio.play(SFX_DENY, 0.8f);
            app.hurtVignette = 1;
            addShake(10);
            Vector2 sp = toScreen(g.pos);
            char buf[32];
            std::snprintf(buf, sizeof buf, "−%.1f с", e.value);
            floatText({sp.x, sp.y - screenR() - 20}, buf, kBad, 22);
            burstW(e.p, 20, kBad, 300, 4, 0.6f);
            break;
        }
        case EvType::Clock: {
            app.audio.play(SFX_BUY, 1.6f);
            Vector2 sp = toScreen(g.pos);
            floatText({sp.x, sp.y - screenR() - 24}, "+" + fmtNum(e.value) + " с", kTimeC, 24);
            shockW(g.pos, 300, kTimeC, 0.5f, 6);
            break;
        }
        case EvType::Dark: {
            app.audio.play(SFX_COMET, 1.2f);
            Vector2 sp = toScreen(g.pos);
            floatText({sp.x, sp.y - screenR() - 24}, "+1 ◆ тёмная материя", kDarkC, 20);
            break;
        }
        case EvType::Gold:
            app.audio.play(SFX_ACH, 1.1f);
            floatText(toScreen(e.p), "ЗОЛОТО! +" + fmtNum(e.value), kGold, 28, 1.6f);
            burstW(e.p, 60, kGold, 400, 4, 1.0f);
            shockW(e.p, 500, kGold, 0.7f, 10);
            addShake(8);
            break;
        case EvType::Chain:
            app.audio.play(SFX_WAVE, 1.3f, 0.7f);
            shockW(e.p, 600, kAccent, 0.7f, 14);
            burstW(e.p, 40, {255, 200, 120, 255}, 500, 4, 0.9f);
            floatText(toScreen(e.p), "ЦЕПНАЯ РЕАКЦИЯ!", kAccent, 22, 1.4f);
            addShake(9);
            break;
        case EvType::RivalEaten:
            app.audio.play(SFX_ACH, 0.9f);
            floatText(toScreen(e.p), "СОПЕРНИК ПОГЛОЩЁН! +" + fmtNum(e.value), kGood, 26, 1.8f);
            burstW(e.p, 80, kRival, 500, 5, 1.1f);
            shockW(e.p, 700, kRival, 0.9f, 18);
            addShake(12);
            break;
        case EvType::Dash:
            app.audio.play(SFX_FRENZY, 1.5f, 0.5f);
            burstW(g.pos, 24, {150, 125, 255, 255}, 300, 4, 0.5f);
            break;
        case EvType::Collapse:
            app.audio.play(SFX_WAVE);
            shockW(g.pos, 1200, {150, 125, 255, 255}, 1.2f, 30);
            banner("ГРАВИТАЦИОННЫЙ КОЛЛАПС", "Всё съедобное тянется к тебе!", {150, 125, 255, 255});
            addShake(14);
            break;
        case EvType::LoopSave:
            app.audio.play(SFX_ACH);
            banner("ПЕТЛЯ ВРЕМЕНИ", "+8 секунд второй жизни!", kTimeC);
            shockW(g.pos, 900, kTimeC, 1.0f, 20);
            break;
        case EvType::RunStart:
            app.runStartBest = g.bestR;
            break;
        case EvType::RunEnd:
            app.audio.play(SFX_RANK, 0.8f);
            app.lastInterest = e.value;
            app.screen = Screen::RunEnd;
            app.runEndT = 0;
            burstW(g.pos, 80, kTimeC, 400, 3, 1.2f);
            break;
        case EvType::Win:
            app.audio.play(SFX_COLLAPSE);
            app.screen = Screen::Collapse;
            app.collapseT = 0;
            break;
        case EvType::NodeBuy: {
            app.audio.play(SFX_NODE, 0.9f + 0.05f * kNodes[e.index].ring);
            if (app.nodeFlash.size() == kNodes.size()) app.nodeFlash[e.index] = 1;
            Vector2 sp = treeToScreen(nodeWorld(e.index));
            floatText({sp.x, sp.y - 34}, kNodes[e.index].name, branchColor(kNodes[e.index].branch), 16, 1.2f);
            break;
        }
        }
    }
    g.events.clear();
}

// ---------- обновление ----------

void updateEffects(float dt)
{
    for (auto &p : app.parts) {
        p.v = Vector2Scale(p.v, 1 - 2.0f * dt);
        p.p = Vector2Add(p.p, Vector2Scale(p.v, dt));
        p.life -= dt;
    }
    app.parts.erase(std::remove_if(app.parts.begin(), app.parts.end(), [](const Particle &p) { return p.life <= 0; }), app.parts.end());
    if (app.parts.size() > 3000) app.parts.erase(app.parts.begin(), app.parts.begin() + (app.parts.size() - 3000));
    for (auto &f : app.floats) {
        f.p.y += f.vy * dt;
        f.vy *= 1 - 1.5f * dt;
        f.life -= dt;
    }
    app.floats.erase(std::remove_if(app.floats.begin(), app.floats.end(), [](const FloatText &f) { return f.life <= 0; }), app.floats.end());
    for (auto &s : app.shocks) {
        s.r += s.speed * dt;
        s.life -= dt;
    }
    app.shocks.erase(std::remove_if(app.shocks.begin(), app.shocks.end(), [](const Shock &s) { return s.life <= 0; }), app.shocks.end());
    for (float &f : app.nodeFlash) f = std::max(0.0f, f - dt * 1.5f);
    app.pulse = std::max(0.0f, app.pulse - dt * 4);
    app.bannerT = std::max(0.0f, app.bannerT - dt);
    app.hurtVignette = std::max(0.0f, app.hurtVignette - dt * 2);
    app.eatSoundCd -= dt;
    app.shake = std::max(0.0f, app.shake - dt * 30);
    app.camShake = {frand(-1, 1) * app.shake, frand(-1, 1) * app.shake};
}

void followCamera(float dt, float zoomTarget)
{
    Game &g = app.game;
    Vector2 before = app.cam;
    float k = std::min(1.0f, dt * 8);
    app.cam.x += (g.pos.x - app.cam.x) * k;
    app.cam.y += (g.pos.y - app.cam.y) * k;
    app.zoom += (zoomTarget - app.zoom) * std::min(1.0f, dt * 2.5f);
    app.starScroll = Vector2Add(app.starScroll, Vector2Scale(Vector2Subtract(app.cam, before), app.zoom));
}

void startRun()
{
    Game &g = app.game;
    g.startRun();
    app.cam = {g.pos.x, g.pos.y};
    app.zoom = g.zoomFor(g.R);
    app.parts.clear();
    app.floats.clear();
    app.shocks.clear();
    app.screen = Screen::Run;
}

void newGame()
{
    app.game.reset((uint32_t)std::time(nullptr));
    app.realTime = 0;
    app.treeCam = {0, 0};
    app.treeZoom = 0.85f;
    startRun();
}

void updateTree(Vector2 m, int hovered)
{
    Game &g = app.game;
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        float nz = Clamp(app.treeZoom * (wheel > 0 ? 1.12f : 0.89f), 0.45f, 1.8f);
        // масштаб вокруг курсора
        Vector2 wBefore = {(m.x - kScreenC.x) / app.treeZoom + app.treeCam.x, (m.y - kScreenC.y) / app.treeZoom + app.treeCam.y};
        app.treeZoom = nz;
        app.treeCam = {wBefore.x - (m.x - kScreenC.x) / nz, wBefore.y - (m.y - kScreenC.y) / nz};
    }
    if (gClicks > 0 && !app.pressed) {
        app.pressed = true;
        app.pressPos = m;
        app.pressCam = app.treeCam;
        app.dragging = false;
    }
    if (app.pressed && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        if (Vector2Distance(m, app.pressPos) > 6) app.dragging = true;
        if (app.dragging) app.treeCam = Vector2Subtract(app.pressCam, Vector2Scale(Vector2Subtract(m, app.pressPos), 1 / app.treeZoom));
    }
    bool released = app.pressed && !IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    // короткий тап может прийти без «удержания» — считаем его кликом
    bool tap = (released && !app.dragging) || (gClicks > 0 && !IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    if (released || (gClicks > 0 && !IsMouseButtonDown(MOUSE_BUTTON_LEFT))) app.pressed = false;
    if (tap) {
        Vector2 p = app.pressPos;
        if (gClicks > 0 && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) p = m;
        if (CheckCollisionPointRec(p, kStartButton)) {
            startRun();
            return;
        }
        if (hovered > 0) {
            if (!g.buyNode(hovered)) app.audio.play(SFX_DENY, 1.2f, 0.5f);
        }
    }
    if (keyHit(KEY_SPACE) || keyHit(KEY_ENTER) || keyHit(KEY_KP_ENTER)) startRun();
    if (CheckCollisionPointRec(m, kStartButton)) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
}

void drawIntro()
{
    drawBackground(kScreenC, 70, true);
    app.pulse = 0.2f;
    drawHoleAt({640, 380}, 70, 0.3f, 0);
    textGlow("ГОРИЗОНТ СОБЫТИЙ", 640, 70, 64, kText, CENTER, {150, 125, 255, 255});
    text("инкрементальная игра про чёрную дыру", 640, 146, 22, kDim, CENTER);
    text("Веди дыру мышкой и поглощай всё, что меньше тебя: от пыли до сверхскоплений галактик.", 640, 560, 18, kText, CENTER);
    text("Дыра испаряется — между заходами качай созвездие из 75 улучшений и стань больше.", 640, 584, 18, kText, CENTER);
    text("Цель — открыть и проглотить Ядро Вселенной. Около 9 минут.", 640, 608, 18, kText, CENTER);
    if (std::fmod(app.t, 1.0f) < 0.7f) textGlow("Кликни, чтобы начать", 640, 650, 28, kGold, CENTER, kAccent);
}

void drawEnd()
{
    Game &g = app.game;
    ClearBackground(BLACK);
    if (GetRandomValue(0, 1)) {
        Color c = ColorFromHSV(frand(0, 360), 0.6f, 1);
        float a = frand(0, 2 * PI), s = frand(60, 500);
        app.parts.push_back({{app.cam.x, app.cam.y}, {std::cos(a) * s / app.zoom, std::sin(a) * s / app.zoom}, 3, 3, frand(1, 4), c});
    }
    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient(640, 360, 200 + 30 * std::sin(app.t * 2), Fade({255, 230, 200, 255}, 0.35f), {0, 0, 0, 0});
    EndBlendMode();
    drawEffects();
    float k = Clamp(app.endT / 1.5f, 0, 1);
    Rectangle r = {360, 110, 560, 480};
    DrawRectangleRounded(r, 0.08f, 8, Fade({10, 8, 24, 255}, 0.85f * k));
    DrawRectangleRoundedLinesEx(r, 0.08f, 8, 2, Fade(kGold, k));
    textGlow("ВСЕЛЕННАЯ ПОГЛОЩЕНА", 640, r.y + 20, 40, Fade(kText, k), CENTER, Fade({150, 125, 255, 255}, k));
    text("...и из сингулярности рождается новый Большой взрыв", 640, r.y + 70, 18, Fade(kDim, k), CENTER);
    text("Время прохождения", 640, r.y + 106, 18, Fade(kDim, k), CENTER);
    textGlow(fmtTime(app.realTime), 640, r.y + 130, 54, Fade(kGold, k), CENTER, Fade(kAccent, k));
    float y = r.y + 210;
    auto row = [&](const char *a, const std::string &b) {
        text(a, r.x + 50, y, 18, Fade(kText, k));
        text(b, r.x + r.width - 50, y, 18, Fade(kText, k), RIGHT, true);
        y += 30;
    };
    row("Заходов", std::to_string(g.runs));
    row("Время в космосе", fmtTime(g.playTime));
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f", g.bestR);
    row("Рекорд размера", buf);
    row("Поглощено массы", fmtNum(g.total));
    int lv = 0, maxLv = 0;
    for (size_t i = 1; i < kNodes.size(); i++) { lv += g.level((int)i); maxLv += kNodes[i].maxLevel; }
    row("Уровней прокачки", std::to_string(lv) + " / " + std::to_string(maxLv));
    if (std::fmod(app.t, 1.0f) < 0.7f) text("R — сыграть снова     Esc — выход", 640, r.y + r.height + 24, 20, Fade(kText, k), CENTER, true);
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

// Отладочные сцены для скриншотов: BH_SCENE=intro|run|big|runend|tree|treelate|end.
void devScene(const std::string &scene)
{
    Game &g = app.game;
    if (scene == "intro") return;
    auto buyRounds = [&](double gift, double darkGift) {
        g.mass += gift;
        g.dark += darkGift;
        for (bool any = true; any;) {
            any = false;
            for (size_t i = 1; i < kNodes.size(); i++)
                if (g.buyNode((int)i)) any = true;
        }
        g.events.clear();
    };
    if (scene == "run") { newGame(); }
    if (scene == "big" || scene == "treelate" || scene == "end") { buyRounds(3e6, 25); g.runs = 6; }
    if (scene == "tree") { buyRounds(3000, 3); g.runs = 3; g.mass = 1800; g.dark = 2; }
    if (scene == "big") { startRun(); }
    if (scene == "runend") { newGame(); }
    if (scene == "tree" || scene == "treelate") { app.screen = Screen::Tree; g.bestR = 140; app.realTime = 200; }
    if (scene == "run" || scene == "big" || scene == "runend") {
        // немного полетать ботом, чтобы сцена ожила
        for (int k = 0; k < (scene == "big" ? 200 : 120) && g.phase == Phase::Run; k++) {
            Vec target = g.pos;
            float best = 1e30f;
            for (auto &o : g.objs)
                if (g.canEat(o) && o.kind == K_TIER) {
                    float d = std::hypot(o.p.x - g.pos.x, o.p.y - g.pos.y) / (o.size + 1);
                    if (d < best) { best = d; target = o.p; }
                }
            g.update(1.0 / 30, target, false, false);
            g.events.clear();
        }
        app.cam = {g.pos.x, g.pos.y};
        app.zoom = g.zoomFor(g.R);
    }
    if (scene == "runend") { g.timeLeft = 0.01; }
    if (scene == "end") { app.screen = Screen::End; app.endT = 3; app.realTime = 534; }
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

    const char *shotEnv = std::getenv("BH_SCREENSHOT");
    const int shotFrame = std::getenv("BH_FRAME") ? std::atoi(std::getenv("BH_FRAME")) : 90;
    int frame = 0;

    while (!WindowShouldClose()) {
        float dt = std::min(GetFrameTime(), 0.1f);
        gClicks = gPendingClicks;
        gRight = gPendingRight;
        gPendingClicks = gPendingRight = 0;
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

        Game &g = app.game;
        bool quit = false;
        int hovered = -1;
        switch (app.screen) {
        case Screen::Intro:
            if (gClicks > 0 || keyHit(KEY_SPACE) || keyHit(KEY_ENTER)) newGame();
            break;
        case Screen::Run: {
            app.realTime += dt;
            Vec target = toWorld(m);
            bool dash = keyHit(KEY_SPACE) || gRight > 0;
            bool col = keyHit(KEY_Q);
            g.update(dt, target, dash, col);
            followCamera(dt, g.zoomFor(g.R));
            break;
        }
        case Screen::RunEnd:
            app.realTime += dt;
            app.runEndT += dt;
            followCamera(dt, g.zoomFor(g.R) * 0.7f);
            if (app.runEndT > 0.6f && (gClicks > 0 || keyHit(KEY_SPACE) || keyHit(KEY_ENTER))) {
                g.finishRunScreen();
                app.screen = Screen::Tree;
                app.parts.clear();
                app.shocks.clear();
            }
            break;
        case Screen::Tree:
            app.realTime += dt;
            break;  // ввод — после отрисовки, когда известен узел под курсором
        case Screen::Collapse:
            app.collapseT += dt;
            addShake(4 + app.collapseT * 4);
            app.zoom *= 1 + dt * 0.8f;
            if (app.collapseT > 4.5f) {
                app.screen = Screen::End;
                app.endT = 0;
                app.flash = 1;
                app.parts.clear();
                for (int i = 0; i < 400; i++) {
                    Color c = ColorFromHSV(frand(0, 360), 0.5f, 1);
                    float a = frand(0, 2 * PI), s = frand(100, 900) / app.zoom;
                    app.parts.push_back({{app.cam.x, app.cam.y}, {std::cos(a) * s, std::sin(a) * s}, 3, 3, frand(2, 6), c});
                }
            }
            break;
        case Screen::End:
            app.endT += dt;
            if (keyHit(KEY_R)) newGame();
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
        switch (app.screen) {
        case Screen::Intro:
            BeginMode2D(ui);
            drawIntro();
            EndMode2D();
            break;
        case Screen::Run: case Screen::RunEnd: case Screen::Collapse:
            BeginMode2D(scene);
            drawRunWorld();
            EndMode2D();
            BeginMode2D(ui);
            drawFloats();
            if (app.screen == Screen::Run) drawRunHud();
            drawBanner();
            if (app.screen == Screen::RunEnd) drawRunEnd();
            if (app.screen == Screen::Collapse) DrawRectangle(0, 0, (int)VW, (int)VH, Fade(WHITE, Clamp((app.collapseT - 3.5f), 0, 1)));
            EndMode2D();
            break;
        case Screen::Tree:
            BeginMode2D(ui);
            drawTree(m, hovered);
            drawFloats();
            EndMode2D();
            break;
        case Screen::End:
            BeginMode2D(ui);
            drawEnd();
            EndMode2D();
            break;
        }
        if (app.flash > 0) {
            BeginMode2D(ui);
            DrawRectangle(0, 0, (int)VW, (int)VH, Fade(WHITE, app.flash));
            EndMode2D();
        }
        EndScissorMode();
        EndDrawing();

        if (app.screen == Screen::Tree) updateTree(m, hovered);

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
