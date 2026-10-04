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

void onMouseButton(GLFWwindow *w, int button, int action, int mods)
{
    if (button == 0 && action == 1) gPendingClicks++;
    if (gPrevMouseCb) gPrevMouseCb(w, button, action, mods);
}

constexpr float VW = 1280, VH = 720;
constexpr Vector2 kCenter = {640, 405};
constexpr Rectangle kLeftPanel = {16, 96, 330, 608};
constexpr Rectangle kRightPanel = {934, 96, 330, 608};
constexpr Rectangle kTreeButton = {950, 112, 298, 64};
constexpr Rectangle kCollapseButton = {470, 626, 340, 62};

// ---------- палитра ----------
const Color kBg = {6, 5, 16, 255};
const Color kPanel = {16, 14, 36, 200};
const Color kPanelEdge = {90, 80, 170, 120};
const Color kText = {232, 228, 255, 255};
const Color kDim = {150, 145, 190, 255};
const Color kAccent = {255, 170, 70, 255};
const Color kGood = {120, 240, 150, 255};
const Color kBad = {255, 100, 110, 255};
const Color kGravity = {180, 120, 255, 255};
const Color kAccretion = {255, 150, 60, 255};
const Color kCosmos = {80, 205, 255, 255};
const Color kGold = {255, 215, 90, 255};

Color branchColor(Branch b)
{
    switch (b) {
    case Branch::Gravity: return kGravity;
    case Branch::Accretion: return kAccretion;
    case Branch::Cosmos: return kCosmos;
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
        text(s, x + std::cos(a) * 2.5f, y + std::sin(a) * 2.5f, size, Fade(glow, 0.25f), al, true);
    }
    text(s, x, y, size, c, al, true);
}

Font loadFont(const unsigned char *data, int size)
{
    std::vector<int> cps;
    for (int c = 32; c < 127; c++) cps.push_back(c);
    for (int c = 0x400; c < 0x460; c++) cps.push_back(c);
    for (int c : {0xAB, 0xBB, 0xD7, 0xB0, 0x2014, 0x2013, 0x2026, 0x2022, 0x2192, 0x2605, 0x221E, 0x2191, 0x2713})
        cps.push_back(c);
    Font f = LoadFontFromMemory(".ttf", data, size, 64, cps.data(), (int)cps.size());
    GenTextureMipmaps(&f.texture);
    SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
    return f;
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
    int ach;
    float t;
};

struct Star {
    Vector2 p;
    float b, tw, speed, size;
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
    float t = 0;            // реальное время
    float shake = 0;
    float pulse = 0;        // вспышка дыры от клика
    float horizonShown = 0; // плавный рост дыры
    float collapseT = 0;
    float endT = 0;
    float flash = 0;
    float clickTokens = 20;
    float infallAcc = 0;
    float bannerT = 0;
    std::string bannerTitle, bannerText;
    float cardShake[kGenCount] = {};
    float cardFlash[kGenCount] = {};
    float nodeFlash[N_COUNT] = {};
    Vector2 cometFrom{}, cometCtrl{}, cometTo{};
    Vector2 camShake{};

    std::vector<Particle> parts;
    std::vector<FloatText> floats;
    std::vector<Infaller> infallers;
    std::vector<Shock> shocks;
    std::vector<Toast> toasts;
    std::vector<Star> stars;
};

App app;

float holeRadius()
{
    float r = 38 + 47 * app.horizonShown;
    if (app.screen == Screen::Collapse) r *= 1 + 12 * std::pow(app.collapseT / 4.5f, 5);
    return r * (1 + 0.05f * app.pulse);
}

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

void spawnInfaller(int type)
{
    static const float sizes[kGenCount] = {7, 10, 13, 17, 19, 10, 24};
    float a = frand(0, 2 * PI);
    app.infallers.push_back({type, frand(620, 700), a, sizes[type] * frand(0.85f, 1.15f), frand(0, 360), frand(-90, 90)});
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
    case 3:  // планета с кольцом
        DrawCircleV({0, 0}, size * 0.8f, A({70, 140, 220, 255}));
        DrawCircleSector({0, 0}, size * 0.8f, 200, 340, 16, A({80, 190, 120, 255}));
        DrawEllipseLines(0, 0, size * 1.3f, size * 0.3f, A({230, 200, 150, 255}));
        DrawEllipseLines(0, 0, size * 1.2f, size * 0.25f, A({230, 200, 150, 200}));
        break;
    case 4:  // звезда
        DrawCircleGradient(0, 0, size * 1.4f, A({255, 230, 120, 140}), A({255, 120, 30, 0}));
        DrawCircleV({0, 0}, size * 0.7f, A({255, 240, 170, 255}));
        DrawCircleV({0, 0}, size * 0.45f, A({255, 255, 240, 255}));
        break;
    case 5: {  // нейтронная звезда с лучами
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
    case 6:  // галактика
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

// ---------- сцена ----------

void initStars()
{
    app.stars.clear();
    for (int i = 0; i < 420; i++)
        app.stars.push_back({{frand(0, VW), frand(0, VH)}, frand(0.25f, 1), frand(0, 10), frand(2, 9), frand(0.6f, 1.8f)});
}

void drawBackground()
{
    ClearBackground(kBg);
    // Туманности
    float t = app.t;
    DrawCircleGradient(260 + 30 * std::sin(t * 0.05f), 220, 420, {70, 30, 120, 70}, {0, 0, 0, 0});
    DrawCircleGradient(1050, 520 + 20 * std::cos(t * 0.04f), 460, {20, 60, 120, 70}, {0, 0, 0, 0});
    DrawCircleGradient(700, 120, 300, {120, 40, 80, 45}, {0, 0, 0, 0});
    if (app.game.frenzyLeft > 0)
        DrawCircleGradient(640, 400, 700, Fade({255, 60, 200, 255}, 0.18f + 0.08f * std::sin(t * 10)), {0, 0, 0, 0});

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
        if (stretch > 1.3f) {
            // растянутые линзой звёзды — дуги
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
    float R = holeRadius();
    // Тень
    DrawCircleV(kCenter, R, BLACK);

    // Падающие объекты
    for (auto &f : app.infallers) {
        float k = Clamp(R / f.r, 0, 1);
        float stretch = 1 + 3.5f * k * k;
        Vector2 p = {kCenter.x + std::cos(f.ang) * f.r, kCenter.y + std::sin(f.ang) * f.r * 0.62f};
        float size = f.size * (0.45f + 0.55f * (1 - k * k));
        float rot = f.ang * RAD2DEG;
        drawIconAt(f.type, p, size, app.t, stretch > 1.15f ? rot : f.rot, stretch, Clamp((f.r - R) / (R * 0.4f), 0, 1));
    }

    // Аккреционный диск и свечение
    float quad = R * 4.4f;
    BeginBlendMode(BLEND_ADDITIVE);
    if (app.shaderOk) {
        float tt = app.t, pu = app.pulse, fr = app.game.frenzyLeft > 0 ? 1.0f : 0.0f, heat = app.horizonShown;
        SetShaderValue(app.hole, app.locTime, &tt, SHADER_UNIFORM_FLOAT);
        SetShaderValue(app.hole, app.locPulse, &pu, SHADER_UNIFORM_FLOAT);
        SetShaderValue(app.hole, app.locFrenzy, &fr, SHADER_UNIFORM_FLOAT);
        SetShaderValue(app.hole, app.locHeat, &heat, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(app.hole);
        DrawTexturePro(app.white, {0, 0, 1, 1}, {kCenter.x - quad, kCenter.y - quad, quad * 2, quad * 2}, {0, 0}, 0, WHITE);
        EndShaderMode();
    } else {
        // Запасной вариант без шейдеров.
        DrawRing(kCenter, R * 1.0f, R * 1.08f, 0, 360, 64, {255, 200, 140, 255});
        for (int i = 0; i < 40; i++) {
            float a = i * 2 * PI / 40 + app.t * 0.8f;
            float rr = R * (1.7f + 2.2f * ((i * 37) % 40) / 40.0f);
            Vector2 p = {kCenter.x + std::cos(a) * rr, kCenter.y + std::sin(a) * rr * 0.2f};
            DrawCircleV(p, 3, Fade(kAccent, 0.6f));
        }
    }
    EndBlendMode();
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
    // Хвост
    if (GetRandomValue(0, 1)) {
        app.parts.push_back({p, {frand(-30, 30), frand(-30, 30)}, 1.2f, 1.2f, frand(2, 5), kGold, false});
    }
    // Мерцающая подпись
    if (std::fmod(app.t, 0.6f) < 0.4f) text("КЛИКНИ!", p.x, p.y - 44, 16, kGold, CENTER, true);
}

// ---------- интерфейс ----------

bool hover(Rectangle r, Vector2 m) { return CheckCollisionPointRec(m, r); }

void panel(Rectangle r, Color edge = kPanelEdge)
{
    DrawRectangleRounded(r, 0.06f, 8, kPanel);
    DrawRectangleRoundedLinesEx(r, 0.06f, 8, 1.5f, edge);
}

Rectangle genCard(int i) { return {kLeftPanel.x + 10, kLeftPanel.y + 44 + i * 80.0f, kLeftPanel.width - 20, 74}; }

void drawLeftPanel(Vector2 m)
{
    Game &g = app.game;
    panel(kLeftPanel);
    text("ОБЪЕКТЫ", kLeftPanel.x + 16, kLeftPanel.y + 12, 20, kText, LEFT, true);
    text("клавиши 1–7", kLeftPanel.x + kLeftPanel.width - 16, kLeftPanel.y + 15, 15, kDim, RIGHT);

    for (int i = 0; i < kGenCount; i++) {
        Rectangle r = genCard(i);
        r.x += std::sin(app.cardShake[i] * 60) * app.cardShake[i] * 20;
        bool vis = g.genVisible(i);
        double cost = g.genCost(i);
        bool can = vis && g.mass >= cost;
        bool hov = vis && hover(r, m);
        Color bg = can ? Color{40, 34, 80, 220} : Color{24, 22, 48, 200};
        if (hov) bg = can ? Color{60, 50, 120, 240} : Color{34, 30, 64, 230};
        DrawRectangleRounded(r, 0.18f, 6, bg);
        if (app.cardFlash[i] > 0) DrawRectangleRounded(r, 0.18f, 6, Fade(kGood, app.cardFlash[i] * 0.5f));
        DrawRectangleRoundedLinesEx(r, 0.18f, 6, 1.2f, can ? Fade(kGood, 0.55f + 0.25f * std::sin(app.t * 4)) : Fade(kPanelEdge, 0.6f));

        Vector2 ic = {r.x + 38, r.y + r.height / 2};
        if (!vis) {
            DrawCircleV(ic, 20, {40, 36, 70, 255});
            text("?", ic.x, ic.y - 14, 26, kDim, CENTER, true);
            text("???", r.x + 74, r.y + 14, 19, kDim, LEFT, true);
            text("Поглоти больше массы", r.x + 74, r.y + 40, 14, Fade(kDim, 0.7f));
            continue;
        }
        drawIconAt(i, ic, 18, app.t, hov ? app.t * 40 : 0);
        text(kGens[i].name, r.x + 74, r.y + 8, 18, kText, LEFT, true);
        text(kGens[i].desc, r.x + 74, r.y + 30, 13, kDim);
        text("+" + fmtNum(kGens[i].rate * g.genMult(i)) + "/с за шт.", r.x + 74, r.y + 50, 14, Fade(kGood, 0.85f));
        text(std::to_string(g.gens[i]), r.x + r.width - 12, r.y + 6, 26, kText, RIGHT, true);
        text(fmtNum(cost), r.x + r.width - 12, r.y + 46, 18, can ? kGood : kBad, RIGHT, true);
    }
}

int affordableNodes()
{
    int n = 0;
    for (int i = 1; i < N_COUNT; i++)
        if (app.game.nodeAvailable((NodeId)i) && app.game.mass >= kNodes[i].cost) n++;
    return n;
}

Vector2 achPos(int a) { return {kRightPanel.x + 36 + (a % 7) * 43.0f, kRightPanel.y + 300 + (a / 7) * 44.0f}; }

void drawTooltip(Vector2 m, const std::string &title, const std::string &body, const std::string &foot, Color tc)
{
    float w = std::max({measure(title, 19, true).x, measure(body, 16).x, measure(foot, 16, true).x}) + 28;
    float h = foot.empty() ? 66 : 92;
    float x = std::min(m.x + 18, VW - w - 8), y = std::min(m.y + 18, VH - h - 8);
    DrawRectangleRounded({x, y, w, h}, 0.15f, 6, {12, 10, 30, 245});
    DrawRectangleRoundedLinesEx({x, y, w, h}, 0.15f, 6, 1.5f, Fade(tc, 0.8f));
    text(title, x + 14, y + 10, 19, tc, LEFT, true);
    text(body, x + 14, y + 38, 16, kText);
    if (!foot.empty()) text(foot, x + 14, y + 64, 16, kDim, LEFT, true);
}

void drawRightPanel(Vector2 m, std::string &tipTitle, std::string &tipBody, Color &tipColor)
{
    Game &g = app.game;
    panel(kRightPanel);

    // Кнопка дерева
    int aff = affordableNodes();
    bool hov = hover(kTreeButton, m);
    Color bc = hov ? Color{90, 60, 170, 255} : Color{60, 40, 120, 255};
    DrawRectangleRounded(kTreeButton, 0.25f, 8, bc);
    if (aff > 0) DrawRectangleRoundedLinesEx(kTreeButton, 0.25f, 8, 2.5f, Fade(kGood, 0.6f + 0.4f * std::sin(app.t * 5)));
    else DrawRectangleRoundedLinesEx(kTreeButton, 0.25f, 8, 1.5f, Fade(kGravity, 0.8f));
    text("ДЕРЕВО ПРОКАЧКИ", kTreeButton.x + kTreeButton.width / 2, kTreeButton.y + 10, 22, kText, CENTER, true);
    text("узлов " + std::to_string(g.nodesOwned()) + "/" + std::to_string(N_COUNT - 1) + "   •   клавиша T",
         kTreeButton.x + kTreeButton.width / 2, kTreeButton.y + 38, 14, kDim, CENTER);
    if (aff > 0) {
        Vector2 bp = {kTreeButton.x + kTreeButton.width - 6, kTreeButton.y + 6};
        DrawCircleV(bp, 14 + 2 * std::sin(app.t * 6), kGood);
        text(std::to_string(aff), bp.x, bp.y - 10, 18, {10, 30, 10, 255}, CENTER, true);
    }

    // Клик
    float x = kRightPanel.x + 18, y = kRightPanel.y + 98;
    text("СИЛА КЛИКА", x, y, 16, kDim, LEFT, true);
    text("+" + fmtNum(g.baseClick() * g.comboMult()), kRightPanel.x + kRightPanel.width - 18, y - 4, 22, kText, RIGHT, true);
    y += 28;
    char buf[96];
    if (g.critChance() > 0) std::snprintf(buf, sizeof buf, "Крит: %.0f%% шанс, сила ×%.0f", g.critChance() * 100, g.critMult());
    else std::snprintf(buf, sizeof buf, "Криты закрыты — ищи в дереве");
    text(buf, x, y, 15, g.critChance() > 0 ? kText : kDim);
    y += 26;
    // Комбо
    float cm = (float)g.comboMult(), cmax = (float)g.comboMax();
    std::snprintf(buf, sizeof buf, "Комбо ×%.2f  (макс ×%.1f)", cm, cmax);
    text(buf, x, y, 15, cm > 1.01f ? kAccent : kDim);
    Rectangle bar = {x, y + 22, kRightPanel.width - 36, 10};
    DrawRectangleRounded(bar, 1, 6, {30, 26, 60, 255});
    float k = (cm - 1) / 2.0f;
    if (k > 0) DrawRectangleRounded({bar.x, bar.y, bar.width * k, bar.height}, 1, 6, lerpColor(kAccent, kBad, k));
    float capX = bar.x + bar.width * (cmax - 1) / 2.0f;
    DrawRectangle((int)capX - 1, (int)bar.y - 3, 2, 16, Fade(kText, 0.6f));
    y += 46;

    // Безумие
    if (g.frenzyLeft > 0) {
        std::snprintf(buf, sizeof buf, "БЕЗУМИЕ ×7  %.1f с", g.frenzyLeft);
        textGlow(buf, kRightPanel.x + kRightPanel.width / 2, y, 20, {255, 140, 230, 255}, CENTER, {255, 60, 200, 255});
    } else {
        text("Доход: +" + fmtNum(g.income()) + "/с", x, y, 16, kGood, LEFT, true);
    }

    // Достижения
    y = kRightPanel.y + 250;
    text("ДОСТИЖЕНИЯ", x, y, 16, kDim, LEFT, true);
    std::snprintf(buf, sizeof buf, "%d/%d  •  +%d%% дохода", g.achCount(), ACH_COUNT, g.achCount() * 2);
    text(buf, kRightPanel.x + kRightPanel.width - 18, y + 1, 14, kDim, RIGHT);
    for (int a = 0; a < ACH_COUNT; a++) {
        Vector2 p = achPos(a);
        bool has = g.hasAch(a);
        if (has) {
            DrawCircleGradient((int)p.x, (int)p.y, 20, Fade(kGold, 0.35f), {0, 0, 0, 0});
            DrawCircleV(p, 15, {90, 70, 20, 255});
            DrawRing(p, 13, 16, 0, 360, 24, kGold);
            DrawPoly(p, 5, 7, -90 + app.t * 20, kGold);
        } else {
            DrawCircleV(p, 15, {28, 26, 50, 255});
            DrawRing(p, 14, 15.5f, 0, 360, 24, Fade(kDim, 0.4f));
            text("?", p.x, p.y - 10, 18, Fade(kDim, 0.6f), CENTER, true);
        }
        if (CheckCollisionPointCircle(m, p, 16)) {
            tipTitle = kAchs[a].name;
            tipBody = kAchs[a].desc;
            tipColor = has ? kGold : kDim;
        }
    }

    // Цель
    y = kRightPanel.y + 420;
    text("ПОГЛОЩЕНИЕ ВСЕЛЕННОЙ", x, y, 16, kDim, LEFT, true);
    double prog = std::min(1.0, g.mass / kGoal);
    bar = {x, y + 28, kRightPanel.width - 36, 18};
    DrawRectangleRounded(bar, 1, 8, {30, 26, 60, 255});
    if (prog > 0.002) DrawRectangleRounded({bar.x, bar.y, (float)(bar.width * prog), bar.height}, 1, 8, lerpColor(kGravity, kBad, (float)prog));
    std::snprintf(buf, sizeof buf, "%.1f%%", prog * 100);
    text(buf, bar.x + bar.width / 2, bar.y + 1, 15, kText, CENTER, true);
    text(fmtNum(g.mass) + " / " + fmtNum(kGoal), x, y + 54, 15, kDim);
    int sec = (int)g.time;
    std::snprintf(buf, sizeof buf, "%02d:%02d", sec / 60, sec % 60);
    text(buf, kRightPanel.x + kRightPanel.width - 18, y + 54, 15, kDim, RIGHT, true);

    y = kRightPanel.y + kRightPanel.height - 64;
    text(std::string("M — звук: ") + (app.audio.muted() ? "выкл" : "вкл"), x, y, 14, Fade(kDim, 0.8f));
    text("F — полный экран", x, y + 20, 14, Fade(kDim, 0.8f));
    text("Пробел — клик", x, y + 40, 14, Fade(kDim, 0.8f));
    text("Кометы ловятся мышкой", kRightPanel.x + kRightPanel.width - 18, y + 40, 14, Fade(kDim, 0.8f), RIGHT);
}

void drawTop()
{
    Game &g = app.game;
    std::string ms = fmtNum(g.mass);
    textGlow(ms, 640, 14, 54, kText, CENTER, kGravity);
    text("массы   •   +" + fmtNum(g.income()) + " в секунду", 640, 72, 18, kDim, CENTER);
    Color rc = lerpColor(kAccent, kGold, 0.5f + 0.5f * std::sin(app.t * 2));
    text(g.rankName(), 640, 100, 17, rc, CENTER, true);
}

void drawBottom(Vector2 m)
{
    Game &g = app.game;
    if (g.combo >= 3) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "КОМБО ×%.2f", g.comboMult());
        float s = 22 + 6 * (float)((g.comboMult() - 1) / 2);
        textGlow(buf, 640, 572, s, kAccent, CENTER, kBad);
    }
    if (g.mass >= kGoal) {
        bool hov = hover(kCollapseButton, m);
        float p = 0.5f + 0.5f * std::sin(app.t * 5);
        DrawRectangleRounded(kCollapseButton, 0.4f, 10, lerpColor({120, 20, 60, 255}, {200, 40, 90, 255}, hov ? 1 : p));
        DrawRectangleRoundedLinesEx(kCollapseButton, 0.4f, 10, 3, Fade(kGold, 0.6f + 0.4f * p));
        text("ПОГЛОТИТЬ ВСЕЛЕННУЮ", 640, kCollapseButton.y + 9, 24, kText, CENTER, true);
        text("клик или Enter", 640, kCollapseButton.y + 38, 14, Fade(kText, 0.8f), CENTER);
        return;
    }
    std::string hint;
    if (g.clicks < 5) hint = "Кликай по чёрной дыре, чтобы затягивать материю";
    else if (g.gens[0] == 0) hint = "Купи космическую пыль на панели слева — она даёт массу сама";
    else if (g.nodesOwned() == 0 && affordableNodes() > 0) hint = "Открой ДЕРЕВО ПРОКАЧКИ справа (или нажми T)";
    else if (g.cometLeft > 0) hint = "Комета! Кликни по ней, пока не улетела";
    if (!hint.empty()) text(hint, 640, 690, 17, Fade(kText, 0.6f + 0.3f * std::sin(app.t * 3)), CENTER);
}

void drawToasts()
{
    if (app.toasts.empty()) return;
    {
        Toast &ts = app.toasts.front();
        float in = std::min(1.0f, ts.t * 4), out = std::min(1.0f, (3.2f - ts.t) * 3);
        float k = Clamp(std::min(in, out), 0, 1);
        float x = kLeftPanel.x - (1 - in) * 360;
        Rectangle r = {x, 14, kLeftPanel.width, 72};
        DrawRectangleRounded(r, 0.25f, 8, Fade({40, 30, 10, 255}, 0.95f * k));
        DrawRectangleRoundedLinesEx(r, 0.25f, 8, 2, Fade(kGold, k));
        DrawPoly({r.x + 34, r.y + 34}, 5, 16, -90 + app.t * 60, Fade(kGold, k));
        text("ДОСТИЖЕНИЕ!", r.x + 62, r.y + 8, 14, Fade(kGold, k), LEFT, true);
        text(kAchs[ts.ach].name, r.x + 62, r.y + 25, 19, Fade(kText, k), LEFT, true);
        text("+2% к доходу", r.x + 62, r.y + 47, 13, Fade(kDim, k));
        if (app.toasts.size() > 1)
            text("+" + std::to_string(app.toasts.size() - 1), r.x + r.width - 12, r.y + 8, 14, Fade(kGold, k), RIGHT, true);
    }
}

void drawBanner()
{
    if (app.bannerT <= 0) return;
    float k = std::min(1.0f, std::min(app.bannerT, 3.5f - app.bannerT) * 3);
    k = Clamp(k, 0, 1);
    float s = 1 + 0.15f * (1 - k);
    textGlow(app.bannerTitle, 640, 170, 22 * s, Fade(kGold, k), CENTER, Fade(kAccent, k));
    textGlow(app.bannerText, 640, 198, 36 * s, Fade(kText, k), CENTER, Fade(kGravity, k));
}

// ---------- дерево прокачки ----------

Vector2 nodePos(int i) { return {640 + kNodes[i].x * 500, 432 + kNodes[i].y * 280}; }

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
    case Branch::Root:
        DrawCircleV(p, r * 0.45f, BLACK);
        DrawRing(p, r * 0.45f, r * 0.55f, 0, 360, 24, kAccent);
        break;
    }
}

int drawTree(Vector2 m, bool clicked)
{
    Game &g = app.game;
    DrawRectangle(0, 0, (int)VW, (int)VH, {6, 4, 18, 242});
    textGlow("ДЕРЕВО ПРОКАЧКИ", 640, 18, 34, kText, CENTER, kGravity);
    if (g.cometLeft > 0 && std::fmod(app.t, 0.8f) < 0.5f)
        text("★ Пролетает комета! Закрой дерево (T), чтобы поймать", 640, 60, 17, kGold, CENTER, true);
    else
        text("Масса: " + fmtNum(g.mass) + "   •   T или Esc — закрыть", 640, 60, 17, kDim, CENTER);
    text("ГРАВИТАЦИЯ  •  клик", 640 - 0.72f * 500, 92, 17, kGravity, CENTER, true);
    text("АККРЕЦИЯ  •  доход", 640, 92, 17, kAccretion, CENTER, true);
    text("КОСМОС  •  события", 640 + 0.72f * 500, 92, 17, kCosmos, CENTER, true);

    // Связи
    for (int i = 1; i < N_COUNT; i++) {
        Vector2 a = nodePos(kNodes[i].parent), b = nodePos(i);
        Color c = branchColor(kNodes[i].branch);
        if (g.hasNode((NodeId)i)) {
            DrawLineEx(a, b, 5, Fade(c, 0.9f));
            // бегущие огоньки по купленным связям
            float k = std::fmod(app.t * 0.7f + i * 0.13f, 1.0f);
            BeginBlendMode(BLEND_ADDITIVE);
            DrawCircleV(Vector2Lerp(a, b, k), 4, WHITE);
            EndBlendMode();
        } else if (g.nodeAvailable((NodeId)i)) {
            for (int s = 0; s < 12; s += 2)
                DrawLineEx(Vector2Lerp(a, b, s / 12.0f), Vector2Lerp(a, b, (s + 1) / 12.0f), 3, Fade(c, 0.6f));
        } else {
            DrawLineEx(a, b, 2, Fade(c, 0.15f));
        }
    }

    int hovered = -1;
    for (int i = 0; i < N_COUNT; i++) {
        NodeId id = (NodeId)i;
        Vector2 p = nodePos(i);
        Color c = branchColor(kNodes[i].branch);
        bool owned = g.hasNode(id), avail = g.nodeAvailable(id), can = avail && g.mass >= kNodes[i].cost;
        float r = i == 0 ? 34 : 28;
        bool hov = CheckCollisionPointCircle(m, p, r + 4);
        if (hov) hovered = i;
        if (hov) r += 3;
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
            text(fmtNum(kNodes[i].cost), p.x, p.y + r + 4, 15, can ? kGood : kBad, CENTER, true);
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
        if (owned || avail)
            text(kNodes[i].name, p.x, p.y - r - 20, 14, owned ? Fade(kText, 0.9f) : kText, CENTER, owned);
    }

    if (hovered >= 0) {
        const NodeDef &n = kNodes[hovered];
        std::string foot;
        if (hovered == 0) foot = "Корень дерева";
        else if (g.hasNode(n.id)) foot = "✓ Куплено";
        else if (!g.nodeAvailable(n.id)) foot = std::string("Сначала: ") + kNodes[n.parent].name;
        else foot = "Цена: " + fmtNum(n.cost) + (g.mass >= n.cost ? "  — кликни, чтобы купить" : "  — не хватает массы");
        drawTooltip(m, n.name, n.desc, foot, branchColor(n.branch));
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    }
    return clicked ? hovered : -1;
}

// ---------- обработка событий игры ----------

void onBuyNode(int id)
{
    Vector2 p = nodePos(id);
    Color c = branchColor(kNodes[id].branch);
    burst(p, 40, c, 260, 4, 0.9f);
    burst(p, 20, WHITE, 140, 2.5f, 0.6f);
    shock(p, 220, c, 0.6f, 10);
    app.nodeFlash[id] = 1;
    addShake(5);
}

void handleEvents()
{
    Game &g = app.game;
    for (const Event &e : g.events) {
        switch (e.type) {
        case EvType::Click: {
            app.pulse = 1;
            float pitch = 0.85f + 0.3f * (float)std::min(1.0, (g.comboMult() - 1) / 2) + frand(-0.05f, 0.05f);
            if (e.crit) {
                app.audio.play(SFX_CRIT, frand(0.9f, 1.1f));
                addShake(10);
                shock(kCenter, 500, kAccent, 0.5f, 14);
                floatText({kCenter.x + frand(-60, 60), kCenter.y - holeRadius() - 30}, "КРИТ! +" + fmtNum(e.value), kAccent, 36, 1.4f);
                suckParticles(24, kAccent);
            } else {
                app.audio.play(SFX_CLICK, pitch, 0.7f);
                floatText({kCenter.x + frand(-70, 70), kCenter.y - holeRadius() - 20 + frand(-10, 10)}, "+" + fmtNum(e.value), kText, 22, 1.0f);
                suckParticles(6, lerpColor({200, 180, 255, 255}, kAccent, frand()));
            }
            break;
        }
        case EvType::Buy:
            app.audio.play(SFX_BUY, 0.9f + 0.05f * e.index);
            app.cardFlash[e.index] = 1;
            for (int k = 0; k < 1 + e.index / 3; k++) spawnInfaller(e.index);
            {
                Rectangle r = genCard(e.index);
                burst({r.x + 38, r.y + r.height / 2}, 14, kGood, 150, 3, 0.6f);
            }
            break;
        case EvType::Node:
            app.audio.play(SFX_NODE);
            onBuyNode(e.index);
            break;
        case EvType::Achievement:
            app.audio.play(SFX_ACH);
            app.toasts.push_back({e.index, 0});
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
            app.bannerTitle = "СВЕРХНОВАЯ";
            app.bannerText = "БЕЗУМИЕ: доход ×7 на 12 секунд!";
            app.bannerT = 3.5f;
            addShake(12);
            break;
        }
        case EvType::Wave:
            app.audio.play(SFX_WAVE);
            shock(kCenter, 900, kGravity, 1.3f, 30);
            shock(kCenter, 650, WHITE, 1.0f, 8);
            floatText({kCenter.x, kCenter.y - holeRadius() - 70}, "ГРАВИТАЦИОННАЯ ВОЛНА +" + fmtNum(e.value), kGravity, 30, 2.0f);
            addShake(16);
            break;
        case EvType::Rank:
            app.audio.play(SFX_RANK);
            app.bannerTitle = "НОВЫЙ РАНГ";
            app.bannerText = g.rankName();
            app.bannerT = 3.5f;
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
        case EvType::Collapse:
            app.audio.play(SFX_COLLAPSE);
            app.screen = Screen::Collapse;
            app.collapseT = 0;
            g.cometLeft = 0;
            app.treeOpen = false;
            break;
        }
    }
    g.events.clear();
}

// ---------- обновление ----------

void updateEffects(float dt)
{
    float R = holeRadius();
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

    for (auto &s : app.shocks) {
        s.r += s.speed * dt;
        s.life -= dt;
    }
    app.shocks.erase(std::remove_if(app.shocks.begin(), app.shocks.end(), [](const Shock &s) { return s.life <= 0; }), app.shocks.end());

    for (auto &f : app.infallers) {
        float speed = 35 + 26000 / std::max(f.r, 20.0f);
        f.r -= speed * dt;
        f.ang += dt * 260 / std::pow(std::max(f.r, 20.0f), 1.05f);
        f.rot += f.spin * dt;
        if (f.r < R) {
            Vector2 p = {kCenter.x + std::cos(f.ang) * R, kCenter.y + std::sin(f.ang) * R * 0.62f};
            static const Color cols[kGenCount] = {{190, 170, 150, 255}, {160, 140, 120, 255}, {220, 220, 230, 255},
                                                  {90, 170, 240, 255},  {255, 220, 120, 255}, {170, 210, 255, 255},
                                                  {200, 140, 255, 255}};
            burst(p, 6 + f.type * 2, cols[f.type], 90, 2.5f, 0.5f);
        }
    }
    app.infallers.erase(std::remove_if(app.infallers.begin(), app.infallers.end(), [R](const Infaller &f) { return f.r < R; }), app.infallers.end());

    // Достижения показываются по одному, остальные ждут в очереди.
    if (!app.toasts.empty()) {
        app.toasts.front().t += dt * (app.toasts.size() > 2 ? 1.6f : 1.0f);
        if (app.toasts.front().t > 3.2f) app.toasts.erase(app.toasts.begin());
    }

    for (auto &s : app.stars) {
        s.p.x -= s.speed * dt;
        if (s.p.x < -10) s.p = {VW + 10, frand(0, VH)};
    }

    for (int i = 0; i < kGenCount; i++) {
        app.cardShake[i] = std::max(0.0f, app.cardShake[i] - dt);
        app.cardFlash[i] = std::max(0.0f, app.cardFlash[i] - dt * 2.5f);
    }
    for (float &f : app.nodeFlash) f = std::max(0.0f, f - dt * 1.5f);

    app.pulse = std::max(0.0f, app.pulse - dt * 5);
    app.bannerT = std::max(0.0f, app.bannerT - dt);
    app.shake = std::max(0.0f, app.shake - dt * 30);
    app.camShake = {frand(-1, 1) * app.shake, frand(-1, 1) * app.shake};
    app.horizonShown += ((float)app.game.horizon() - app.horizonShown) * std::min(1.0f, dt * 2);
}

void spawnPassiveInfallers(float dt)
{
    Game &g = app.game;
    int total = 0;
    for (int c : g.gens) total += c;
    if (total == 0) return;
    app.infallAcc += dt * std::min(3.5f, 0.4f + total * 0.04f);
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

void startGame()
{
    app.game.reset((uint32_t)std::time(nullptr));
    app.parts.clear();
    app.floats.clear();
    app.infallers.clear();
    app.shocks.clear();
    app.toasts.clear();
    app.horizonShown = 0;
    app.treeOpen = false;
    app.bannerT = 0;
    app.screen = Screen::Play;
}

void updatePlay(float dt, Vector2 m)
{
    Game &g = app.game;
    bool click = gClicks > 0;
    float R = holeRadius();

    if (IsKeyPressed(KEY_T)) app.treeOpen = !app.treeOpen;
    if (IsKeyPressed(KEY_ESCAPE)) app.treeOpen = false;

    if (!app.treeOpen) {
        if (IsKeyPressed(KEY_SPACE)) doClick();
        for (int i = 0; i < kGenCount; i++)
            if (IsKeyPressed(KEY_ONE + i)) {
                if (!g.buyGen(i)) { app.cardShake[i] = 0.3f; app.audio.play(SFX_DENY); }
            }
        if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && g.mass >= kGoal) g.collapse();

        bool overUi = hover(kLeftPanel, m) || hover(kRightPanel, m);
        bool overComet = g.cometLeft > 0 && Vector2Distance(m, cometPos()) < 46;
        bool overHole = !overUi && CheckCollisionPointCircle(m, kCenter, R * 1.7f);
        bool overCollapse = g.mass >= kGoal && hover(kCollapseButton, m);

        if (overComet || overHole || overCollapse || hover(kTreeButton, m)) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        for (int i = 0; i < kGenCount; i++)
            if (g.genVisible(i) && hover(genCard(i), m)) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);

        if (click) {
            if (overComet) g.catchComet();
            else if (overCollapse) g.collapse();
            else if (hover(kTreeButton, m)) { app.treeOpen = true; app.audio.play(SFX_BUY, 1.4f, 0.5f); }
            else if (overHole) for (int i = 0; i < gClicks; i++) doClick();
            else
                for (int i = 0; i < kGenCount; i++)
                    if (g.genVisible(i) && hover(genCard(i), m) && !g.buyGen(i)) {
                        app.cardShake[i] = 0.3f;
                        app.audio.play(SFX_DENY);
                    }
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
    float a = 0.7f;
    DrawRectangle(0, 0, (int)VW, (int)VH, Fade(kBg, a));
    textGlow("ГОРИЗОНТ СОБЫТИЙ", 640, 120, 64, kText, CENTER, kGravity);
    text("инкрементальная игра про чёрную дыру", 640, 196, 22, kDim, CENTER);
    text("Ты — крошечная чёрная дыра. Поглощай пыль, астероиды, планеты, звёзды и галактики,", 640, 560, 18, kText, CENTER);
    text("прокачивай дерево умений, лови кометы — и проглоти всю Вселенную примерно за 5 минут.", 640, 584, 18, kText, CENTER);
    if (std::fmod(app.t, 1.0f) < 0.7f) textGlow("Кликни, чтобы начать", 640, 630, 28, kGold, CENTER, kAccent);
}

void drawEnd()
{
    Game &g = app.game;
    ClearBackground(BLACK);
    // Новый Большой взрыв: непрерывный цветной фейерверк из центра.
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
    Rectangle r = {340, 150, 600, 430};
    DrawRectangleRounded(r, 0.08f, 8, Fade({10, 8, 24, 255}, 0.85f * k));
    DrawRectangleRoundedLinesEx(r, 0.08f, 8, 2, Fade(kGold, k));
    textGlow("ВСЕЛЕННАЯ ПОГЛОЩЕНА", 640, 172, 40, Fade(kText, k), CENTER, Fade(kGravity, k));
    text("...и из сингулярности рождается новый Большой взрыв", 640, 222, 18, Fade(kDim, k), CENTER);
    int sec = (int)g.time;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%02d:%02d", sec / 60, sec % 60);
    text("Время прохождения", 640, 262, 18, Fade(kDim, k), CENTER);
    textGlow(buf, 640, 286, 54, Fade(kGold, k), CENTER, Fade(kAccent, k));
    auto row = [&](const char *name, const std::string &val, float y) {
        text(name, 400, y, 19, Fade(kText, k));
        text(val, 880, y, 19, Fade(kText, k), RIGHT, true);
    };
    row("Поглощено массы", fmtNum(g.total), 366);
    row("Кликов", std::to_string(g.clicks), 396);
    row("Критов", std::to_string(g.crits), 426);
    row("Поймано комет", std::to_string(g.cometsCaught), 456);
    row("Узлов дерева", std::to_string(g.nodesOwned()) + " / " + std::to_string(N_COUNT - 1), 486);
    row("Достижений", std::to_string(g.achCount()) + " / " + std::to_string(ACH_COUNT), 516);
    if (std::fmod(app.t, 1.0f) < 0.7f) text("R — сыграть снова     Esc — выход", 640, 600, 20, Fade(kText, k), CENTER, true);
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

// Отладочные сцены для скриншотов: BH_SCENE=play|late|tree|end.
void devScene(const std::string &scene)
{
    startGame();
    Game &g = app.game;
    auto give = [&](double m) { g.mass += m; g.total += m; };
    if (scene == "intro") { app.screen = Screen::Intro; return; }
    give(scene == "play" ? 3e3 : 3e7);
    int n = scene == "play" ? 3 : 7;
    for (int i = 0; i < n; i++)
        for (int k = 0; k < (scene == "play" ? 6 - i * 2 : 12); k++) { give(g.genCost(i)); g.buyGen(i); }
    for (int i = 1; i < N_COUNT; i++)
        if (scene != "play" || kNodes[i].cost < 400) { give(kNodes[i].cost); g.buyNode((NodeId)i); }
    if (scene == "late" || scene == "collapse") give(3e8);
    g.events.clear();
    for (int i = 0; i < 25; i++) g.click();
    g.events.clear();
    g.cometLeft = 5;
    app.cometFrom = {-60, 200}; app.cometCtrl = {600, 80}; app.cometTo = {1340, 400};
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
    int frame = 0;
    const int shotFrame = std::getenv("BH_FRAME") ? std::atoi(std::getenv("BH_FRAME")) : 90;

    while (!WindowShouldClose()) {
        float dt = std::min(GetFrameTime(), 0.1f);
        gClicks = gPendingClicks;
        gPendingClicks = 0;
        app.t += dt;
        frame++;

        // Масштабирование виртуального экрана 1280×720 в окно с полями.
        float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
        float scale = std::min(sw / VW, sh / VH);
        Camera2D ui{};
        ui.offset = {(sw - VW * scale) / 2, (sh - VH * scale) / 2};
        ui.zoom = scale;
        Vector2 m = GetScreenToWorld2D(GetMousePosition(), ui);
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);

        if (IsKeyPressed(KEY_F) || IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
        if (IsKeyPressed(KEY_M)) app.audio.toggleMute();
        app.audio.update();
        app.clickTokens = std::min(20.0f, app.clickTokens + dt * 20);

        int treeClick = -1;
        switch (app.screen) {
        case Screen::Intro:
            if (gClicks > 0 || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) startGame();
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
            if (IsKeyPressed(KEY_R)) startGame();
            if (IsKeyPressed(KEY_ESCAPE)) goto quit;
            break;
        }
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
            drawEffects();
            drawComet();
            EndMode2D();

            BeginMode2D(ui);
            if (app.screen == Screen::Intro) {
                drawIntro();
            } else {
                float uiAlpha = app.screen == Screen::Collapse ? std::max(0.0f, 1 - app.collapseT) : 1;
                if (uiAlpha > 0) {
                    std::string tipTitle, tipBody;
                    Color tipColor = kText;
                    drawFloats();
                    drawTop();
                    drawBottom(m);
                    drawLeftPanel(m);
                    drawRightPanel(m, tipTitle, tipBody, tipColor);
                    drawBanner();
                    drawToasts();
                    if (!app.treeOpen && !tipTitle.empty()) drawTooltip(m, tipTitle, tipBody, "", tipColor);
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
quit:
    app.audio.shutdown();
    UnloadShader(app.hole);
    UnloadFont(gReg);
    UnloadFont(gBold);
    CloseWindow();
    return 0;
}
