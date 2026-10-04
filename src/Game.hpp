// Логика игры «Горизонт событий»: заходы, в которых чёрная дыра летает по космосу
// и поглощает всё, что меньше неё, и дерево прокачки между заходами.
// Ничего не знает о графике и звуке: интерфейс читает состояние и события.
#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace bh {

struct Vec {
    float x = 0, y = 0;
};

// ---------- объекты космоса ----------
constexpr int kTierCount = 13;  // обычные объекты от пыли до пузырей Хаббла

struct TierDef {
    const char *name;
    float size;    // радиус в мировых единицах
    double value;  // масса за поглощение
};

extern const std::array<TierDef, kTierCount> kTiers;

enum Kind { K_TIER, K_ANTI, K_PULSAR, K_RIVAL, K_CLOCK, K_DARK, K_GOLD, K_CORE, K_MAGNET, K_NOVA, K_CHRONO, K_DOUBLE };

// ---------- вселенные ----------
constexpr int kUniverseCount = 4;

struct UniverseDef {
    const char *name;
    const char *desc;
    double valueMult;  // во сколько раз ценнее объекты
    float coreSize;    // радиус Ядра этой вселенной
    float hazard;      // множитель опасностей
};

extern const std::array<UniverseDef, kUniverseCount> kUniverses;

struct Obj {
    Kind kind = K_TIER;
    int tier = 0;
    Vec p, v;
    float size = 4;
    float rot = 0, spin = 0;
    float beam = 0;          // угол луча пульсара
    float hurtCd = 0;
    float hp = 1;            // Ядро: сколько осталось откусить
    bool pulled = false;
    bool fragment = false;   // осколок цепной реакции/сверхновой — сам не взрывается
    bool dead = false;
    uint32_t id = 0;
};

// ---------- дерево прокачки ----------
enum Stat {
    ST_SIZE, ST_SPEED, ST_PULL, ST_PULLSTR, ST_EAT, ST_GROWTH, ST_TIME, ST_VALUE, ST_VALUEX, ST_TIERVAL,
    ST_DENSITY, ST_RICH, ST_COMBO, ST_COMBOWIN, ST_CRIT, ST_CRITMULT, ST_CLOCK, ST_CLOCKVAL, ST_ARMOR,
    ST_GOLD, ST_CHAIN, ST_SAT, ST_SATSIZE, ST_DASH, ST_DASHCD, ST_COLLAPSE, ST_COLLAPSEPOW, ST_DARK,
    ST_INTEREST, ST_MAGNET, ST_TIMEFEED, ST_ANTIEAT, ST_LOOP, ST_RIVAL, ST_SWARM, ST_PHANTOM, ST_FINAL,
    ST_BITE, ST_POWER, ST_POWERDUR, ST_HUNGER, ST_HYPER, ST_CARDS, ST_EVO,
};

enum class Branch { Root, Gravity, Growth, Time, Wealth, Cosmos, Dark };

struct NodeDef {
    const char *name;
    const char *desc;   // что даёт один уровень
    Stat stat;
    double value;       // величина за уровень
    int param;          // для ST_TIERVAL: первый ярус (диапазон 2 яруса)
    int maxLevel;
    double cost;        // цена первого уровня
    double growth;      // рост цены за уровень
    int parent;
    Branch branch;
    int ring;           // кольцо созвездия
    float offset;       // смещение угла от оси ветки, градусы
};

extern const std::vector<NodeDef> kNodes;

struct Stats {
    double size = 13, speed = 1, pull = 1, pullStr = 1, eat = 0.85, growth = 1, time = 12;
    double value = 1, tierVal[kTierCount] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    double bite = 1, power = 0.004, powerDur = 1;
    double hunger = 1, hyperDur = 4;
    int cards = 3, evo = 5;
    double density = 1, rich = 0, comboMax = 1.5, comboWin = 0.6, crit = 0, critMult = 5;
    double clock = 0, clockVal = 2, armor = 0, gold = 0.002, chain = 0;
    int sats = 0;
    double satSize = 1;
    bool dash = false;
    double dashCd = 4;
    bool collapse = false;
    double collapseCd = 25, collapsePow = 1;
    double dark = 0.004, interest = 0, magnet = 1, timeFeed = 0;
    bool antiEat = false;
    double loop = 0, rival = 1;
    int swarm = 0;
    bool phantom = false, final = false;
};

// ---------- события для интерфейса ----------
enum class EvType {
    Eat, Hurt, Clock, Dark, Chain, RivalEaten, RunStart, RunEnd, Dash, Collapse, LoopSave, Win, NodeBuy, SatEat, Gold,
    CoreBite, CoreEaten, PowerUp, Nova, Evolve, CardPick, Hyper, QuestDone, Modifier
};

// ---------- эволюция в заходе: карты-усиления ----------
enum Card { CARD_PULL, CARD_SPEED, CARD_CHAIN, CARD_SAT, CARD_TIME, CARD_MAGNET, CARD_TEETH, CARD_GREED,
            CARD_CRIT, CARD_ARMOR, CARD_FEAST, CARD_NOVA, CARD_HUNGER, CARD_COUNT };

struct CardDef {
    const char *name;
    const char *desc;
    int rarity;  // 0 обычная, 1 редкая, 2 эпическая
};

extern const std::array<CardDef, CARD_COUNT> kCards;

// ---------- аномалии захода ----------
enum Modifier { MOD_NONE, MOD_METEOR, MOD_GOLD, MOD_DARK, MOD_RIVALS, MOD_STARFALL, MOD_CALM, MOD_PULSARS, MOD_COUNT };

struct ModifierDef {
    const char *name;
    const char *desc;
};

extern const std::array<ModifierDef, MOD_COUNT> kModifiers;

// ---------- задания захода ----------
enum QuestType { Q_EAT, Q_BIG, Q_COMBO, Q_NOHIT, Q_GROW, Q_ARTIFACT, Q_RIVAL, Q_GOLD, Q_COUNT };

struct Quest {
    QuestType type = Q_EAT;
    double target = 1, progress = 0;
    bool done = false;
};

std::string questText(const Quest &q);

constexpr double kMaxRunTime = 30;  // таймер захода не больше 30 секунд

struct Event {
    EvType type;
    double value = 0;
    int index = 0;      // ярус/вид
    Vec p;
    bool crit = false;
    float size = 0;
};

enum class Phase { Tree, Run, RunEnd, UniverseClear, Won };

class Game {
public:
    Game() = default;  // глобальные объекты: reset() вызывается явно, после статической инициализации
    explicit Game(uint32_t seed);
    void reset(uint32_t seed);

    // Дерево
    int level(int node) const { return levels_[node]; }
    int levelCap(int node) const;  // предел уровня растёт с каждой вселенной
    bool nodeVisible(int node) const;
    bool nodeAvailable(int node) const;  // родитель открыт и есть куда расти
    double nodeCost(int node) const;
    bool canBuy(int node) const;
    bool buyNode(int node);
    int affordableCount() const;
    const Stats &stats() const { return st_; }       // постоянные (дерево)
    const Stats &runStats() const { return rs_; }    // в текущем заходе, с картами и аномалией

    // Заход
    void startRun();
    // target — куда ведём дыру (мировые координаты)
    void update(double dt, Vec target, bool dash, bool collapse);
    void finishRunScreen() { if (phase == Phase::RunEnd) phase = Phase::Tree; }
    void nextUniverse();  // после Ядра: следующая вселенная
    bool chooseCard(int i);  // выбор карты эволюции (0..cards-1)

    // Камера: радиус дыры на экране и масштаб
    float zoomFor(float R) const;
    float viewRadius() const;  // половина диагонали экрана в мировых единицах
    bool canEat(const Obj &o) const;
    float holeR() const { return R; }

    // Состояние
    Phase phase = Phase::Tree;
    double mass = 0, dark = 0, total = 0;
    double playTime = 0;  // только время заходов
    int runs = 0;
    double bestR = 0;
    int universe = 0;
    // артефакты
    double magnetLeft = 0, chronoLeft = 0, doubleLeft = 0;
    // эволюция, голод, аномалия, задания
    bool choosing = false;
    std::vector<int> choices, picked;
    int evoLevel = 0;
    double hunger = 0, hyperLeft = 0;
    Modifier modifier = MOD_NONE;
    std::array<Quest, 2> quests{};
    double noHitTime = 0;

    // Текущий заход
    Vec pos, vel;
    float R = 13;
    double timeLeft = 0, runTime = 0, runMass = 0;
    int runEaten = 0, biggestTier = -1, combo = 0;
    double comboTimer = 0;
    double dashCd = 0, dashLeft = 0, collapseCd = 0, collapseLeft = 0;
    bool loopUsed = false;
    float satAngle = 0;
    double hurtFlash = 0;
    std::vector<Obj> objs;
    std::vector<Event> events;

    static constexpr float kScreenHalfDiag = 734;

private:
    void recompute();
    void spawnAround(bool initial);
    Obj makeObj(int tier, Vec p);
    int pickTier();
    void eat(Obj &o, bool bySat);
    void hurt(double seconds, Vec at);
    double tierValue(int tier) const;
    float rnd() { return std::uniform_real_distribution<float>(0, 1)(rng_); }
    float rnd(float a, float b) { return a + (b - a) * rnd(); }

    std::vector<int> levels_;
    Stats st_, rs_;
    void applyCard(int c);
    void questProgress(QuestType t, double v, bool absolute = false);
    void evolveCheck();
    std::mt19937 rng_;
    uint32_t nextId_ = 1;
    double spawnAcc_ = 0;
    double clockCd_ = 0, darkCd_ = 0, goldCd_ = 0, powerCd_ = 0, biteFx_ = 0;
    float runArea_ = 0;
    double timeGained_ = 0;  // прибавки за заход ограничены стартовым временем
    void addTime(double s);
    void nova();
    bool kindOnField(Kind k) const;
};

std::string fmtNum(double v);

// Ручки баланса (подобраны симулятором tools/sim.cpp)
extern double kRingCost, kLevelGrowthAdd;
extern float kGrowthK;
extern float kBiteRatio, kBiteRate, kMaxGrowth;
extern double kCapCost;

}  // namespace bh
