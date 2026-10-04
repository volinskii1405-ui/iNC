#include "Game.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bh {

const std::array<GenDef, kGenCount> kGens = {{
    {"Космическая пыль",  "Облачко газа и пыли",          10,    0.5},
    {"Астероид",          "Каменная глыба без имени",     60,    3},
    {"Луна",              "Чей-то потерянный спутник",    450,   18},
    {"Планета",           "Целый мир за горизонтом",      3500,  110},
    {"Звезда",            "Спагеттификация светила",      30000, 700},
    {"Нейтронная звезда", "Сверхплотный деликатес",       2.6e5, 4500},
    {"Галактика",         "Сто миллиардов звёзд разом",   2.2e6, 30000},
}};

const std::array<NodeDef, N_COUNT> kNodes = {{
    {N_ROOT,      "Сингулярность",         "Точка, с которой всё начинается",            0,      N_ROOT,    Branch::Root,      0.00f,  0.85f},

    {G_STRONG,    "Сильная гравитация",    "Клик x2",                                    30,     N_ROOT,    Branch::Gravity,  -0.45f, 0.50f},
    {G_TIDAL,     "Приливный разрыв",      "10% шанс крита x8",                         250,    G_STRONG,  Branch::Gravity,  -0.66f, 0.15f},
    {G_COMBO,     "Орбитальное комбо",     "Предел комбо x1.5 -> x3",                    1500,   G_TIDAL,   Branch::Gravity,  -0.95f, -0.20f},
    {G_JETS,      "Релятивистские джеты",  "Клик +2% дохода в секунду",                  4000,   G_TIDAL,   Branch::Gravity,  -0.56f, -0.20f},
    {G_SPAGHETTI, "Спагеттификация",       "Криты: шанс 15%, сила x15",                  40000,  G_COMBO,   Branch::Gravity,  -0.95f, -0.58f},
    {G_WAVE,      "Гравитационная волна",  "Каждый 40-й клик: +20 с дохода",             3e5,    G_JETS,    Branch::Gravity,  -0.56f, -0.58f},

    {A_NEBULA,    "Плотная туманность",    "Пыль x3",                                    120,    N_ROOT,    Branch::Accretion, 0.00f, 0.50f},
    {A_BELT,      "Пояс астероидов",       "Астероиды и луны x3",                        900,    A_NEBULA,  Branch::Accretion, 0.00f, 0.15f},
    {A_DISK,      "Аккреционный диск",     "Всё производство x2",                        15000,  A_BELT,    Branch::Accretion, 0.00f, -0.20f},
    {A_GIANTS,    "Красные гиганты",       "Планеты и звёзды x3",                        1.5e5,  A_DISK,    Branch::Accretion,-0.19f, -0.58f},
    {A_PHOTON,    "Фотонная сфера",        "Всё производство x2",                        6e5,    A_DISK,    Branch::Accretion, 0.19f, -0.58f},
    {A_QUASAR,    "Квазар",                "Всё производство x3",                        1.2e7,  A_PHOTON,  Branch::Accretion, 0.19f, -0.92f},

    {C_WANDER,    "Блуждающие звёзды",     "Кометы прилетают вдвое чаще",                300,    N_ROOT,    Branch::Cosmos,    0.45f, 0.50f},
    {C_MAGNETAR,  "Магнитар",              "Награда за комету x2",                       3000,   C_WANDER,  Branch::Cosmos,    0.66f, 0.15f},
    {C_SUPERNOVA, "Сверхновая",            "35% комет запускают БЕЗУМИЕ: доход x7",      40000,  C_MAGNETAR,Branch::Cosmos,    0.95f, -0.20f},
    {C_WORMHOLE,  "Червоточина",           "Все объекты дешевле на 15%",                 80000,  C_MAGNETAR,Branch::Cosmos,    0.56f, -0.20f},
    {C_HAWKING,   "Излучение Хокинга",     "+4% дохода за каждый узел дерева",           1.5e6,  C_WORMHOLE,Branch::Cosmos,    0.56f, -0.58f},
    {C_DARK,      "Тёмная материя",        "Нейтронные звёзды и галактики x3",           5e6,    C_SUPERNOVA,Branch::Cosmos,   0.95f, -0.58f},
}};

const std::array<AchDef, ACH_COUNT> kAchs = {{
    {"Первый глоток",       "Поглоти первую порцию материи"},
    {"Пылесос",             "100 кликов"},
    {"Ненасытная",          "500 кликов"},
    {"Комбо-орбита",        "Разгони комбо до x2"},
    {"Критическая масса",   "Первый крит"},
    {"Охотник за кометами", "Поймай комету"},
    {"Кометный магнат",     "Поймай 5 комет"},
    {"Тысяча",              "Поглоти 1K массы"},
    {"Сто тысяч",           "Поглоти 100K массы"},
    {"Десять миллионов",    "Поглоти 10M массы"},
    {"Коллекционер",        "Владей объектами всех 7 типов"},
    {"Садовник",            "Купи 9 узлов дерева"},
    {"Древо познания",      "Купи все узлы дерева"},
    {"Безумие!",            "Впади в безумие сверхновой"},
}};

static const char *kRanks[] = {
    "Микро-дыра", "Чёрная дыра звёздной массы", "Промежуточная чёрная дыра",
    "Сверхмассивная чёрная дыра", "Ультрамассивная чёрная дыра", "Пожиратель Вселенной",
};
static const double kRankAt[] = {0, 1e3, 1e5, 1e6, 2e7, kGoal};

Game::Game(uint32_t seed) { reset(seed); }

void Game::reset(uint32_t seed)
{
    mass = total = time = 0;
    clicks = 0;
    combo = 0;
    comboTimer = 0;
    gens.fill(0);
    frenzyLeft = cometLeft = 0;
    cometsCaught = crits = 0;
    won = false;
    nodes_.fill(false);
    nodes_[N_ROOT] = true;
    achs_.fill(false);
    lastRank_ = 0;
    events.clear();
    rng_.seed(seed);
    cometTimer = 20 + rnd() * 10;
}

double Game::nextCometDelay()
{
    double d = 40 + rnd() * 25;
    return hasNode(C_WANDER) ? d * 0.5 : d;
}

double Game::genMult(int i) const
{
    double m = 1;
    if (i == 0 && hasNode(A_NEBULA)) m *= 3;
    if ((i == 1 || i == 2) && hasNode(A_BELT)) m *= 3;
    if ((i == 3 || i == 4) && hasNode(A_GIANTS)) m *= 3;
    if ((i == 5 || i == 6) && hasNode(C_DARK)) m *= 3;
    return m;
}

double Game::income() const
{
    double s = 0;
    for (int i = 0; i < kGenCount; i++) s += gens[i] * kGens[i].rate * genMult(i);
    if (hasNode(A_DISK)) s *= 2;
    if (hasNode(A_PHOTON)) s *= 2;
    if (hasNode(A_QUASAR)) s *= 3;
    if (hasNode(C_HAWKING)) s *= 1 + 0.04 * nodesOwned();
    s *= 1 + 0.02 * achCount();
    if (frenzyLeft > 0) s *= 7;
    return s;
}

double Game::baseClick() const
{
    double v = hasNode(G_STRONG) ? 2 : 1;
    if (hasNode(G_JETS)) v += income() * 0.02;
    return v;
}

double Game::comboMax() const { return hasNode(G_COMBO) ? 3.0 : 1.5; }

double Game::comboMult() const { return std::min(comboMax(), 1.0 + combo * 0.04); }

double Game::critChance() const
{
    if (hasNode(G_SPAGHETTI)) return 0.15;
    if (hasNode(G_TIDAL)) return 0.10;
    return 0;
}

double Game::critMult() const { return hasNode(G_SPAGHETTI) ? 15 : 8; }

double Game::genCost(int i) const
{
    double c = kGens[i].baseCost * std::pow(kCostGrowth, gens[i]);
    if (hasNode(C_WORMHOLE)) c *= 0.85;
    return std::floor(c);
}

bool Game::genVisible(int i) const
{
    if (i == 0) return true;
    return gens[i - 1] > 0 || total >= kGens[i].baseCost * 0.5;
}

bool Game::nodeAvailable(NodeId id) const
{
    return !nodes_[id] && nodes_[kNodes[id].parent];
}

int Game::nodesOwned() const
{
    int n = 0;
    for (int i = 1; i < N_COUNT; i++) n += nodes_[i];
    return n;
}

int Game::achCount() const
{
    int n = 0;
    for (bool a : achs_) n += a;
    return n;
}

int Game::rank() const
{
    int r = 0;
    for (int i = 0; i < 6; i++)
        if (total >= kRankAt[i]) r = i;
    return r;
}

const char *Game::rankName() const { return kRanks[rank()]; }

double Game::horizon() const
{
    return std::clamp(std::log10(total + 1) / std::log10(kGoal), 0.0, 1.0);
}

void Game::update(double dt)
{
    if (won) return;
    time += dt;
    gain(income() * dt);

    if (comboTimer > 0) {
        comboTimer -= dt;
        if (comboTimer <= 0) combo = 0;
    }
    if (frenzyLeft > 0) frenzyLeft = std::max(0.0, frenzyLeft - dt);

    if (cometLeft > 0) {
        cometLeft -= dt;
        if (cometLeft <= 0) cometLeft = 0;
    } else {
        cometTimer -= dt;
        if (cometTimer <= 0) {
            cometLeft = cometTotalTime;
            cometTimer = nextCometDelay();
            events.push_back({EvType::CometSpawn});
        }
    }

    int r = rank();
    if (r > lastRank_) {
        lastRank_ = r;
        events.push_back({EvType::Rank, 0, r});
    }
    checkAchievements();
}

bool Game::click()
{
    if (won) return false;
    clicks++;
    combo++;
    comboTimer = 1.0;
    double v = baseClick() * comboMult();
    bool crit = rnd() < critChance();
    if (crit) {
        v *= critMult();
        crits++;
    }
    gain(v);
    events.push_back({EvType::Click, v, 0, crit});

    if (hasNode(G_WAVE) && clicks % 40 == 0) {
        double w = std::max(100.0, income() * 20);
        gain(w);
        events.push_back({EvType::Wave, w});
    }
    return true;
}

bool Game::buyGen(int i)
{
    if (won || i < 0 || i >= kGenCount || !genVisible(i)) return false;
    double c = genCost(i);
    if (mass < c) return false;
    mass -= c;
    gens[i]++;
    events.push_back({EvType::Buy, c, i});
    return true;
}

bool Game::buyNode(NodeId id)
{
    if (won || !nodeAvailable(id)) return false;
    double c = kNodes[id].cost;
    if (mass < c) return false;
    mass -= c;
    nodes_[id] = true;
    events.push_back({EvType::Node, c, id});
    return true;
}

bool Game::catchComet()
{
    if (cometLeft <= 0 || won) return false;
    cometLeft = 0;
    cometsCaught++;
    if (hasNode(C_SUPERNOVA) && rnd() < 0.35) {
        frenzyLeft = 12;
        events.push_back({EvType::Frenzy, 12});
        unlock(ACH_FRENZY);
        return true;
    }
    double reward = std::max(50.0, income() * 25 + mass * 0.08);
    if (hasNode(C_MAGNETAR)) reward *= 2;
    gain(reward);
    events.push_back({EvType::Comet, reward});
    return true;
}

bool Game::collapse()
{
    if (won || mass < kGoal) return false;
    mass -= kGoal;
    won = true;
    events.push_back({EvType::Collapse});
    return true;
}

void Game::unlock(AchId a)
{
    if (achs_[a]) return;
    achs_[a] = true;
    events.push_back({EvType::Achievement, 0, a});
}

void Game::checkAchievements()
{
    if (clicks >= 1) unlock(ACH_FIRST);
    if (clicks >= 100) unlock(ACH_CLICK100);
    if (clicks >= 500) unlock(ACH_CLICK500);
    if (comboMult() >= 2) unlock(ACH_COMBO);
    if (crits >= 1) unlock(ACH_CRIT);
    if (cometsCaught >= 1) unlock(ACH_COMET1);
    if (cometsCaught >= 5) unlock(ACH_COMET5);
    if (total >= 1e3) unlock(ACH_MASS1K);
    if (total >= 1e5) unlock(ACH_MASS100K);
    if (total >= 1e7) unlock(ACH_MASS10M);
    if (std::all_of(gens.begin(), gens.end(), [](int c) { return c > 0; })) unlock(ACH_ALLGENS);
    if (nodesOwned() >= 9) unlock(ACH_TREE9);
    if (nodesOwned() == N_COUNT - 1) unlock(ACH_TREEALL);
}

std::string fmtNum(double v)
{
    static const char *suf[] = {"", "K", "M", "B", "T", "Qa"};
    char buf[32];
    if (v < 10 && v != std::floor(v)) {
        std::snprintf(buf, sizeof buf, "%.1f", v);
        return buf;
    }
    int i = 0;
    while (v >= 1000 && i < 5) { v /= 1000; i++; }
    if (i == 0) std::snprintf(buf, sizeof buf, "%.0f", std::floor(v));
    else std::snprintf(buf, sizeof buf, v < 10 ? "%.2f%s" : v < 100 ? "%.1f%s" : "%.0f%s", v, suf[i]);
    return buf;
}

}  // namespace bh
