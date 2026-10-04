#include "Game.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bh {

const std::array<GenDef, kGenCount> kGens = {{
    {"Космическая пыль",   "Облачко газа и пыли",            10,    0.5},
    {"Астероид",           "Каменная глыба без имени",       100,   2.5},
    {"Луна",               "Чей-то потерянный спутник",      1000,  14},
    {"Планета",            "Целый мир за горизонтом",        1e4,   75},
    {"Газовый гигант",     "Юпитер был вкусный",             1e5,   380},
    {"Звезда",             "Спагеттификация светила",        1e6,   2000},
    {"Нейтронная звезда",  "Сверхплотный деликатес",         1e7,   10500},
    {"Звёздное скопление", "Тысячи солнц одним глотком",     1e8,   55000},
    {"Галактика",          "Сто миллиардов звёзд разом",     1e9,   3e5},
    {"Сверхскопление",     "Паутина галактик",               1e10,  1.5e6},
}};

const std::array<NodeDef, N_COUNT> kNodes = {{
    {N_ROOT,      "Сингулярность",         "Точка, с которой всё начинается",               0,     N_ROOT,      Branch::Root,      0,  0},

    {G_STRONG,    "Сильная гравитация",    "Клик ×2",                                       30,    N_ROOT,      Branch::Gravity,   1,  0},
    {G_TIDAL,     "Приливный разрыв",      "10% шанс крита ×8",                             300,   G_STRONG,    Branch::Gravity,   2,  0},
    {G_COMBO,     "Орбитальное комбо",     "Предел комбо ×1.5 → ×3",                        2500,  G_TIDAL,     Branch::Gravity,   3, -1},
    {G_JETS,      "Релятивистские джеты",  "Клик +1% дохода в секунду",                     8000,  G_TIDAL,     Branch::Gravity,   3,  1},
    {G_RESONANCE, "Резонанс орбит",        "Клик в такт кольцу: ×3 и +3 к комбо",           6e4,   G_COMBO,     Branch::Gravity,   4, -1},
    {G_WAVE,      "Гравитационная волна",  "Каждый 50-й клик: +15 с дохода",                3e5,   G_JETS,      Branch::Gravity,   4,  1},
    {G_SPAGHETTI, "Спагеттификация",       "Криты: шанс 15%, сила ×15",                     5e6,    G_RESONANCE, Branch::Gravity,   5, -1},
    {G_KERR,      "Метрика Керра",         "Клик +1.5% дохода/с, комбо до ×3.5",                2e8,    G_WAVE,      Branch::Gravity,   5,  1},

    {A_NEBULA,    "Плотная туманность",    "Пыль и астероиды ×3",                           150,   N_ROOT,      Branch::Accretion, 1,  0},
    {A_BELT,      "Пояс Койпера",          "Луны и планеты ×3",                             3000,  A_NEBULA,    Branch::Accretion, 2,  0},
    {A_DISK,      "Аккреционный диск",     "Всё производство ×2",                           3e4,   A_BELT,      Branch::Accretion, 3, -1},
    {A_GIANTS,    "Красные гиганты",       "Газовые гиганты и звёзды ×3",                   2e5,   A_BELT,      Branch::Accretion, 3,  1},
    {A_SYNERGY,   "Пищевая цепочка",       "Каждый объект: +1% за штуку предыдущего типа",  3e6,    A_DISK,      Branch::Accretion, 4, -1},
    {A_FORGE,     "Нейтронная кузня",      "Нейтронные звёзды и скопления ×3",              5e7,    A_GIANTS,    Branch::Accretion, 4,  1},
    {A_PHOTON,    "Фотонная сфера",        "Всё производство ×2",                           6e8,    A_SYNERGY,   Branch::Accretion, 5, -1},
    {A_QUASAR,    "Квазар",                "Всё производство ×3",                           1.5e10,  A_FORGE,     Branch::Accretion, 5,  1},

    {C_WANDER,    "Блуждающие звёзды",     "Кометы прилетают вдвое чаще",                   500,   N_ROOT,      Branch::Cosmos,    1,  0},
    {C_MAGNETAR,  "Магнитар",              "Награда за комету ×2",                          6000,  C_WANDER,    Branch::Cosmos,    2,  0},
    {C_INTERCEPT, "Перехват",              "Кликай по падающим объектам: +4 с их дохода",  4e4,   C_MAGNETAR,  Branch::Cosmos,    3, -1},
    {C_SUPERNOVA, "Сверхновая",            "35% комет запускают БЕЗУМИЕ: доход ×7",         5e5,   C_MAGNETAR,  Branch::Cosmos,    3,  1},
    {C_METEOR,    "Метеоритный дождь",     "Время от времени — дождь метеоров для ловли",   2e6,   C_INTERCEPT, Branch::Cosmos,    4, -1},
    {C_WORMHOLE,  "Червоточина",           "Все объекты дешевле на 15%",                    1.5e7,  C_SUPERNOVA, Branch::Cosmos,    4,  1},
    {C_HAWKING,   "Излучение Хокинга",     "+2% дохода за каждый узел дерева",              1e9,    C_METEOR,    Branch::Cosmos,    5, -1},
    {C_ATTRACTOR, "Великий аттрактор",     "Вехи объектов дают ×2 вместо ×1.5",               6e9,     C_WORMHOLE,  Branch::Cosmos,    5,  1},

    {D_WARP,      "Искривление времени",   "Способность Q: время ×3 на 10 с",               3,     N_ROOT,      Branch::Dark,      1,  0},
    {D_RUSH,      "Гравитационный рывок",  "Способность W: клики ×3 на 10 с",              5,     D_WARP,      Branch::Dark,      2, -1},
    {D_PORTAL,    "Белая дыра",            "Способность E: мгновенно +90 с дохода",         6,     D_WARP,      Branch::Dark,      2,  1},
    {D_ECHO,      "Эхо горизонта",         "Перезарядка способностей −40%",                 8,     D_RUSH,      Branch::Dark,      3, -1},
    {D_FLOW,      "Тёмный поток",          "+1.5% дохода за каждую добытую тёмную материю",   10,     D_PORTAL,    Branch::Dark,      3,  1},
    {D_FOAM,      "Квантовая пена",        "10% покупок дают второй объект бесплатно",      12,     D_ECHO,      Branch::Dark,      4, -1},
    {D_ENERGY,    "Тёмная энергия",        "Галактики и сверхскопления ×3",                 15,    D_FLOW,      Branch::Dark,      4,  1},
}};

const std::array<AchDef, ACH_COUNT> kAchs = {{
    {"Первый глоток",        "Поглоти первую порцию материи"},
    {"Пылесос",              "100 кликов"},
    {"Ненасытная",           "1000 кликов"},
    {"Комбо-орбита",         "Разгони комбо до ×2"},
    {"Критическая масса",    "Первый крит"},
    {"Охотник за кометами",  "Поймай комету"},
    {"Кометный магнат",      "Поймай 10 комет"},
    {"Тысяча",               "Поглоти 1K массы"},
    {"Миллион",              "Поглоти 1M массы"},
    {"Миллиард",             "Поглоти 1B массы"},
    {"Коллекционер",         "Владей объектами всех 10 типов"},
    {"Садовник",             "Купи 16 узлов дерева"},
    {"Древо познания",       "Купи все узлы дерева"},
    {"Безумие!",             "Впади в безумие сверхновой"},
    {"Идеальный ритм",       "10 идеальных кликов в резонанс"},
    {"Перехватчик",          "Перехвати 10 падающих объектов"},
    {"Царь горы",            "Победи чёрную дыру-соперника"},
    {"Метеоролог",           "Поймай 15 метеоров"},
    {"Повелитель времени",   "Используй способность"},
    {"Веха",                 "Собери 25 объектов одного типа"},
    {"Тёмная сторона",       "Добудь 20 тёмной материи"},
}};

const std::array<AbilityDef, AB_COUNT> kAbilities = {{
    {"Искривление времени", "Время ×3 на 10 с",       D_WARP,   90,  10},
    {"Гравитационный рывок","Клики ×3 на 10 с",       D_RUSH,   60,  10},
    {"Белая дыра",          "Мгновенно +90 с дохода", D_PORTAL, 120, 0},
}};

static const char *kRanks[] = {
    "Микро-дыра", "Чёрная дыра звёздной массы", "Промежуточная чёрная дыра",
    "Сверхмассивная чёрная дыра", "Ультрамассивная чёрная дыра", "Квазар-монстр", "Пожиратель Вселенной",
};
static const double kRankAt[] = {0, 1e3, 1e5, 1e7, 1e9, 1e11, kGoal};
constexpr int kRankCount = 7;
constexpr double kResonancePeriod = 1.3;

Game::Game(uint32_t seed) { reset(seed); }

void Game::reset(uint32_t seed)
{
    mass = total = time = 0;
    dark = darkTotal = 0;
    clicks = 0;
    combo = 0;
    comboTimer = 0;
    gens.fill(0);
    frenzyLeft = cometLeft = 0;
    cometsCaught = crits = perfects = intercepts = meteorsCaught = 0;
    flareGen = -1;
    flareLeft = 0;
    rivalActive = false;
    rivalHp = rivalMaxHp = rivalStolen = rivalLeft = 0;
    rivalsKilled = 0;
    meteorLeft = 0;
    abilityCd.fill(0);
    abilityLeft.fill(0);
    won = false;
    nodes_.fill(false);
    nodes_[N_ROOT] = true;
    achs_.fill(false);
    milestoneSeen_.fill(0);
    lastRank_ = 0;
    events.clear();
    rng_.seed(seed);
    cometTimer = 20 + rnd() * 10;
    flareTimer = 70 + rnd() * 20;
    rivalTimer = 170 + rnd() * 30;
    meteorTimer = 30;
}

double Game::nextCometDelay()
{
    double d = 40 + rnd() * 25;
    return hasNode(C_WANDER) ? d * 0.5 : d;
}

int Game::milestoneLevel(int i) const
{
    int l = 0;
    for (int m : kMilestones)
        if (gens[i] >= m) l++;
    return l;
}

double Game::genMult(int i) const
{
    double m = 1;
    if ((i == 0 || i == 1) && hasNode(A_NEBULA)) m *= 3;
    if ((i == 2 || i == 3) && hasNode(A_BELT)) m *= 3;
    if ((i == 4 || i == 5) && hasNode(A_GIANTS)) m *= 3;
    if ((i == 6 || i == 7) && hasNode(A_FORGE)) m *= 3;
    if ((i == 8 || i == 9) && hasNode(D_ENERGY)) m *= 3;
    if (i > 0 && hasNode(A_SYNERGY)) m *= 1 + 0.01 * gens[i - 1];
    m *= std::pow(hasNode(C_ATTRACTOR) ? 2.0 : 1.5, milestoneLevel(i));
    if (i == flareGen && flareLeft > 0) m *= 5;
    return m;
}

double Game::genIncome(int i) const
{
    double s = gens[i] * kGens[i].rate * genMult(i);
    if (hasNode(A_DISK)) s *= 2;
    if (hasNode(A_PHOTON)) s *= 2;
    if (hasNode(A_QUASAR)) s *= 3;
    if (hasNode(C_HAWKING)) s *= 1 + 0.02 * nodesOwned();
    if (hasNode(D_FLOW)) s *= 1 + 0.015 * darkTotal;
    s *= 1 + 0.02 * achCount();
    if (frenzyLeft > 0) s *= 7;
    return s;
}

double Game::income() const
{
    double s = 0;
    for (int i = 0; i < kGenCount; i++) s += genIncome(i);
    return s;
}

double Game::baseClick() const
{
    double v = hasNode(G_STRONG) ? 2 : 1;
    if (hasNode(G_JETS)) v += income() * 0.01;
    if (hasNode(G_KERR)) v += income() * 0.015;
    if (abilityLeft[AB_RUSH] > 0) v *= 3;
    return v;
}

double Game::comboMax() const { return hasNode(G_KERR) ? 3.5 : hasNode(G_COMBO) ? 3.0 : 1.5; }

double Game::comboMult() const { return std::min(comboMax(), 1.0 + combo * 0.04); }

double Game::critChance() const
{
    if (hasNode(G_SPAGHETTI)) return 0.15;
    if (hasNode(G_TIDAL)) return 0.10;
    return 0;
}

double Game::critMult() const { return hasNode(G_SPAGHETTI) ? 15 : 8; }

double Game::genCost(int i) const { return genCostN(i, 1); }

double Game::genCostN(int i, int n) const
{
    double first = kGens[i].baseCost * std::pow(kCostGrowth, gens[i]);
    double c = first * (std::pow(kCostGrowth, n) - 1) / (kCostGrowth - 1);
    if (hasNode(C_WORMHOLE)) c *= 0.85;
    return std::floor(c);
}

int Game::genAffordable(int i) const
{
    double first = genCost(i);
    if (mass < first) return 0;
    int n = (int)std::floor(std::log(mass / first * (kCostGrowth - 1) + 1) / std::log(kCostGrowth));
    while (n > 1 && genCostN(i, n) > mass) n--;
    return std::max(n, 1);
}

bool Game::genVisible(int i) const
{
    if (i == 0) return true;
    return gens[i - 1] > 0 || total >= kGens[i].baseCost * 0.5;
}

bool Game::nodeAvailable(NodeId id) const { return !nodes_[id] && nodes_[kNodes[id].parent]; }

bool Game::canAffordNode(NodeId id) const
{
    if (!nodeAvailable(id)) return false;
    return kNodes[id].branch == Branch::Dark ? dark >= kNodes[id].cost : mass >= kNodes[id].cost;
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
    for (int i = 0; i < kRankCount; i++)
        if (total >= kRankAt[i]) r = i;
    return r;
}

const char *Game::rankName() const { return kRanks[rank()]; }

double Game::horizon() const { return std::clamp(std::log10(total + 1) / std::log10(kGoal), 0.0, 1.0); }

double Game::resonancePhase() const { return std::fmod(time, kResonancePeriod) / kResonancePeriod; }

bool Game::resonanceWindow() const
{
    double p = resonancePhase();
    return hasNode(G_RESONANCE) && (p > 0.80 || p < 0.04);
}

double Game::abilityCooldown(Ability a) const
{
    return kAbilities[a].cooldown * (hasNode(D_ECHO) ? 0.6 : 1.0);
}

void Game::gainDark(double d, const char *)
{
    dark += d;
    darkTotal += d;
    events.push_back({EvType::Dark, d});
}

void Game::update(double realDt)
{
    if (won) return;
    // Способности тикают в реальном времени, всё остальное — с учётом искривления.
    for (int a = 0; a < AB_COUNT; a++) {
        abilityCd[a] = std::max(0.0, abilityCd[a] - realDt);
        abilityLeft[a] = std::max(0.0, abilityLeft[a] - realDt);
    }
    double dt = realDt * timeScale();
    time += dt;
    gain(income() * dt);

    if (comboTimer > 0) {
        comboTimer -= realDt;
        if (comboTimer <= 0) combo = 0;
    }
    if (frenzyLeft > 0) frenzyLeft = std::max(0.0, frenzyLeft - dt);
    tickEvents(dt);
    // Соперник живёт в реальном времени: искривление не должно его ускорять.
    if (rivalActive) {
        rivalLeft -= realDt;
        if (rivalLeft <= 0) {
            rivalActive = false;
            events.push_back({EvType::RivalLeft, rivalStolen});
        }
    }

    int r = rank();
    if (r > lastRank_) {
        lastRank_ = r;
        events.push_back({EvType::Rank, 0, r});
        gainDark(2, "rank");
    }
    for (int i = 0; i < kGenCount; i++) {
        int l = milestoneLevel(i);
        if (l > milestoneSeen_[i]) {
            milestoneSeen_[i] = l;
            events.push_back({EvType::Milestone, (double)kMilestones[l - 1], i});
        }
    }
    checkAchievements();
}

void Game::tickEvents(double dt)
{
    // Кометы
    if (cometLeft > 0) {
        cometLeft = std::max(0.0, cometLeft - dt);
    } else {
        cometTimer -= dt;
        if (cometTimer <= 0) {
            cometLeft = cometTotalTime;
            cometTimer = nextCometDelay();
            events.push_back({EvType::CometSpawn});
        }
    }

    // Звёздная вспышка: случайный тип объектов ×5
    if (flareLeft > 0) {
        flareLeft = std::max(0.0, flareLeft - dt);
    } else {
        flareTimer -= dt;
        if (flareTimer <= 0) {
            flareTimer = 60 + rnd() * 30;
            std::vector<int> owned;
            for (int i = 0; i < kGenCount; i++)
                if (gens[i] > 0) owned.push_back(i);
            if (!owned.empty()) {
                // чаще выпадают старшие объекты — так вспышка ощутима
                int k = (int)owned.size();
                int pick = owned[std::min(k - 1, (int)(std::sqrt(rnd()) * k))];
                flareGen = pick;
                flareLeft = 20;
                events.push_back({EvType::Flare, 20, pick});
            }
        }
    }

    // Соперник
    if (rivalActive) {
        double steal = mass * 0.02 * dt;
        mass -= steal;
        rivalStolen += steal;
    } else if (rank() >= 2) {
        rivalTimer -= dt;
        if (rivalTimer <= 0) {
            rivalTimer = 110 + rnd() * 40;
            rivalActive = true;
            rivalMaxHp = rivalHp = 25 + 5 * rivalsKilled;
            rivalStolen = 0;
            rivalLeft = 25;
            rivalX = (float)(0.15 + rnd() * 0.7);
            rivalY = (float)(0.15 + rnd() * 0.25);
            events.push_back({EvType::RivalSpawn});
        }
    }

    // Метеоритный дождь
    if (meteorLeft > 0) {
        meteorLeft = std::max(0.0, meteorLeft - dt);
    } else if (hasNode(C_METEOR)) {
        meteorTimer -= dt;
        if (meteorTimer <= 0) {
            meteorTimer = 70 + rnd() * 25;
            meteorLeft = 10;
            events.push_back({EvType::MeteorStart, 10});
        }
    }
}

bool Game::click()
{
    if (won) return false;
    clicks++;
    bool perfect = resonanceWindow();
    combo += perfect ? 4 : 1;
    comboTimer = 1.0;
    double v = baseClick() * comboMult();
    if (perfect) {
        v *= 3;
        perfects++;
        if (perfects % 15 == 0) gainDark(1, "perfect");
    }
    bool crit = rnd() < critChance();
    if (crit) {
        v *= critMult();
        crits++;
    }
    gain(v);
    events.push_back({EvType::Click, v, 0, crit, perfect});

    if (hasNode(G_WAVE) && clicks % 50 == 0) {
        double w = std::max(100.0, income() * 15);
        gain(w);
        events.push_back({EvType::Wave, w});
    }
    return true;
}

int Game::buyGen(int i, int amount)
{
    if (won || i < 0 || i >= kGenCount || !genVisible(i)) return 0;
    int n = amount <= 0 ? genAffordable(i) : amount;
    if (n <= 0) return 0;
    double c = genCostN(i, n);
    if (mass < c) return 0;
    mass -= c;
    gens[i] += n;
    events.push_back({EvType::Buy, (double)n, i});
    if (hasNode(D_FOAM)) {
        int free = 0;
        for (int k = 0; k < n; k++) free += rnd() < 0.10;
        if (free > 0) {
            gens[i] += free;
            events.push_back({EvType::Foam, (double)free, i});
        }
    }
    return n;
}

bool Game::buyNode(NodeId id)
{
    if (won || !canAffordNode(id)) return false;
    if (kNodes[id].branch == Branch::Dark) dark -= kNodes[id].cost;
    else mass -= kNodes[id].cost;
    nodes_[id] = true;
    events.push_back({EvType::Node, kNodes[id].cost, id});
    return true;
}

bool Game::catchComet()
{
    if (cometLeft <= 0 || won) return false;
    cometLeft = 0;
    cometsCaught++;
    gainDark(1, "comet");
    if (hasNode(C_SUPERNOVA) && rnd() < 0.35) {
        frenzyLeft = 12;
        events.push_back({EvType::Frenzy, 12});
        unlock(ACH_FRENZY);
        return true;
    }
    double reward = std::max(50.0, income() * 20 + mass * 0.03);
    if (hasNode(C_MAGNETAR)) reward *= 2;
    gain(reward);
    events.push_back({EvType::Comet, reward});
    return true;
}

bool Game::hitRival()
{
    if (!rivalActive || won) return false;
    double dmg = rnd() < critChance() ? 3 : 1;
    if (abilityLeft[AB_RUSH] > 0) dmg *= 2;
    rivalHp -= dmg;
    events.push_back({EvType::RivalHit, dmg});
    if (rivalHp <= 0) {
        rivalActive = false;
        rivalsKilled++;
        double back = std::max(rivalStolen * 2, income() * 20);
        gain(back);
        gainDark(3, "rival");
        events.push_back({EvType::RivalKilled, back});
        unlock(ACH_RIVAL);
    }
    return true;
}

bool Game::catchMeteor()
{
    if (meteorLeft <= 0 || won) return false;
    double r = std::max(20.0, income() * 3);
    gain(r);
    meteorsCaught++;
    if (meteorsCaught % 5 == 0) gainDark(1, "meteor");
    events.push_back({EvType::Meteor, r});
    return true;
}

bool Game::intercept(int i)
{
    if (!hasNode(C_INTERCEPT) || won || i < 0 || i >= kGenCount) return false;
    double r = std::max(10.0, genIncome(i) * 4);
    gain(r);
    intercepts++;
    events.push_back({EvType::Intercept, r, i});
    return true;
}

bool Game::useAbility(Ability a)
{
    if (won || !abilityUnlocked(a) || abilityCd[a] > 0) return false;
    abilityCd[a] = abilityCooldown(a);
    abilityLeft[a] = kAbilities[a].duration;
    double v = 0;
    if (a == AB_PORTAL) {
        v = std::max(100.0, income() * 90);
        gain(v);
    }
    events.push_back({EvType::Ability, v, a});
    unlock(ACH_ABILITY);
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
    gainDark(1, "achievement");
}

void Game::checkAchievements()
{
    if (clicks >= 1) unlock(ACH_FIRST);
    if (clicks >= 100) unlock(ACH_CLICK100);
    if (clicks >= 1000) unlock(ACH_CLICK1000);
    if (comboMult() >= 2) unlock(ACH_COMBO);
    if (crits >= 1) unlock(ACH_CRIT);
    if (cometsCaught >= 1) unlock(ACH_COMET1);
    if (cometsCaught >= 10) unlock(ACH_COMET10);
    if (total >= 1e3) unlock(ACH_MASS1K);
    if (total >= 1e6) unlock(ACH_MASS1M);
    if (total >= 1e9) unlock(ACH_MASS1B);
    if (std::all_of(gens.begin(), gens.end(), [](int c) { return c > 0; })) unlock(ACH_ALLGENS);
    if (nodesOwned() >= 16) unlock(ACH_TREE16);
    if (nodesOwned() == N_COUNT - 1) unlock(ACH_TREEALL);
    if (perfects >= 10) unlock(ACH_PERFECT);
    if (intercepts >= 10) unlock(ACH_INTERCEPT);
    if (meteorsCaught >= 15) unlock(ACH_METEOR);
    if (std::any_of(gens.begin(), gens.end(), [](int c) { return c >= 25; })) unlock(ACH_MILESTONE);
    if (darkTotal >= 20) unlock(ACH_DARK);
}

std::string fmtNum(double v)
{
    static const char *suf[] = {"", "K", "M", "B", "T", "Qa", "Qi"};
    char buf[32];
    if (v < 10 && v != std::floor(v)) {
        std::snprintf(buf, sizeof buf, "%.1f", v);
        return buf;
    }
    int i = 0;
    while (v >= 1000 && i < 6) { v /= 1000; i++; }
    if (i == 0) std::snprintf(buf, sizeof buf, "%.0f", std::floor(v));
    else std::snprintf(buf, sizeof buf, v < 10 ? "%.2f%s" : v < 100 ? "%.1f%s" : "%.0f%s", v, suf[i]);
    return buf;
}

}  // namespace bh
