#include "game.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const double GOAL_COST = 5e7;

static const Generator GEN_DEFS[GEN_COUNT] = {
    {"Космическая пыль",   "облачко газа и пыли",        10,     0.5,    0},
    {"Астероид",           "каменная глыба",             60,     3,      0},
    {"Луна",               "чей-то потерянный спутник",  450,    18,     0},
    {"Планета",            "целый мир за горизонтом",    3500,   110,    0},
    {"Звезда",             "спагеттификация светила",    30000,  700,    0},
    {"Нейтронная звезда",  "сверхплотный деликатес",     2.6e5,  4500,   0},
    {"Галактика",          "сто миллиардов звёзд",       2.2e6,  30000,  0},
};

static const Upgrade UPG_DEFS[UPG_COUNT] = {
    {"Гравитационный колодец", "клик x2",                  40,     UPG_CLICK, 0, 2, 0},
    {"Плотная туманность",     "пыль x3",                  150,    UPG_GEN,   0, 3, 0},
    {"Пояс астероидов",        "астероиды x3",             1000,   UPG_GEN,   1, 3, 0},
    {"Релятивистские джеты",   "клик +10% дохода/с",       3000,   UPG_JETS,  0, 0.10, 0},
    {"Приливный захват",       "луны x3",                  7000,   UPG_GEN,   2, 3, 0},
    {"Аккреционный диск",      "всё производство x2",      25000,  UPG_ALL,   0, 2, 0},
    {"Красные гиганты",        "звёзды x3",                2e5,    UPG_GEN,   4, 3, 0},
    {"Фотонная сфера",         "всё производство x2",      1e6,    UPG_ALL,   0, 2, 0},
    {"Пульсарный ритм",        "нейтронные звёзды x3",     3e6,    UPG_GEN,   5, 3, 0},
    {"Квазар",                 "всё производство x3",      2e7,    UPG_ALL,   0, 3, 0},
};

static double frand(void) { return rand() / (double)RAND_MAX; }

static void set_msg(Game *g, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g->msg, sizeof g->msg, fmt, ap);
    va_end(ap);
    g->msg_left = 3.0;
}

void game_init(Game *g, unsigned seed)
{
    memset(g, 0, sizeof *g);
    memcpy(g->gens, GEN_DEFS, sizeof GEN_DEFS);
    memcpy(g->upgs, UPG_DEFS, sizeof UPG_DEFS);
    srand(seed);
    g->star_timer = 25 + frand() * 15;
    set_msg(g, "Жми ПРОБЕЛ, чтобы затягивать материю за горизонт событий");
}

double gen_cost(const Generator *gen)
{
    return floor(gen->base_cost * pow(1.14, gen->count));
}

static double gen_mult(const Game *g, int i)
{
    double m = 1;
    for (int u = 0; u < UPG_COUNT; u++) {
        const Upgrade *up = &g->upgs[u];
        if (!up->bought) continue;
        if (up->kind == UPG_ALL) m *= up->value;
        else if (up->kind == UPG_GEN && up->target == i) m *= up->value;
    }
    return m;
}

double game_income(const Game *g)
{
    double s = 0;
    for (int i = 0; i < GEN_COUNT; i++)
        s += g->gens[i].count * g->gens[i].rate * gen_mult(g, i);
    return s;
}

double game_click_value(const Game *g)
{
    double v = 1, jets = 0;
    for (int u = 0; u < UPG_COUNT; u++) {
        const Upgrade *up = &g->upgs[u];
        if (!up->bought) continue;
        if (up->kind == UPG_CLICK) v *= up->value;
        if (up->kind == UPG_JETS) jets += up->value;
    }
    return v + jets * game_income(g);
}

static void gain(Game *g, double m)
{
    g->mass += m;
    g->total += m;
}

void game_tick(Game *g, double dt)
{
    if (g->won) return;
    g->time += dt;
    gain(g, game_income(g) * dt);

    if (g->msg_left > 0) g->msg_left -= dt;

    if (g->star_left > 0) {
        g->star_left -= dt;
        if (g->star_left <= 0) {
            g->star_left = 0;
            set_msg(g, "Блуждающая звезда улетела...");
        }
    } else {
        g->star_timer -= dt;
        if (g->star_timer <= 0) {
            g->star_left = 8;
            g->star_timer = 30 + frand() * 20;
            g->star_reward = fmax(60, game_income(g) * 30 + g->mass * 0.10);
            set_msg(g, "Рядом пролетает блуждающая звезда! Жми Z!");
        }
    }
}

int game_click(Game *g)
{
    if (g->won) return 0;
    g->clicks++;
    gain(g, game_click_value(g));
    return 1;
}

int gen_visible(const Game *g, int i)
{
    if (i == 0) return 1;
    return g->gens[i - 1].count > 0 || g->total >= g->gens[i].base_cost * 0.5;
}

int upg_visible(const Game *g, int i)
{
    const Upgrade *up = &g->upgs[i];
    if (up->bought) return 0;
    if (up->kind == UPG_GEN && g->gens[up->target].count == 0) return 0;
    return g->total >= up->cost * 0.3;
}

int game_buy_gen(Game *g, int i)
{
    if (i < 0 || i >= GEN_COUNT || !gen_visible(g, i)) return 0;
    Generator *gen = &g->gens[i];
    double c = gen_cost(gen);
    if (g->mass < c) return 0;
    g->mass -= c;
    gen->count++;
    set_msg(g, "Поглощено: %s (%d)", gen->name, gen->count);
    return 1;
}

int game_buy_upg(Game *g, int i)
{
    if (i < 0 || i >= UPG_COUNT || !upg_visible(g, i)) return 0;
    Upgrade *up = &g->upgs[i];
    if (g->mass < up->cost) return 0;
    g->mass -= up->cost;
    up->bought = 1;
    set_msg(g, "Улучшение: %s — %s", up->name, up->desc);
    return 1;
}

int game_catch_star(Game *g)
{
    if (g->star_left <= 0) return 0;
    gain(g, g->star_reward);
    g->star_left = 0;
    g->stars_caught++;
    set_msg(g, "Звезда разорвана приливными силами! +%.0f массы", g->star_reward);
    return 1;
}

int game_collapse(Game *g)
{
    if (g->won || g->mass < GOAL_COST) return 0;
    g->mass -= GOAL_COST;
    g->won = 1;
    return 1;
}

const char *game_rank(const Game *g)
{
    static const struct { double at; const char *name; } R[] = {
        {0,     "Микро-дыра"},
        {1e3,   "Чёрная дыра звёздной массы"},
        {1e5,   "Чёрная дыра промежуточной массы"},
        {1e6,   "Сверхмассивная чёрная дыра"},
        {2e7,   "Ультрамассивная чёрная дыра"},
    };
    const char *r = R[0].name;
    for (size_t i = 0; i < sizeof R / sizeof R[0]; i++)
        if (g->total >= R[i].at) r = R[i].name;
    return r;
}
