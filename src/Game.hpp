// Логика игры «Горизонт событий». Ничего не знает о графике и звуке:
// интерфейс читает состояние и забирает события из очереди.
#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace bh {

constexpr int kGenCount = 10;
constexpr double kGoal = 3e12;         // масса для поглощения Вселенной
constexpr double kCostGrowth = 1.15;
constexpr int kMilestones[] = {10, 25, 50, 100};

struct GenDef {
    const char *name;
    const char *desc;
    double baseCost;
    double rate;  // масса в секунду за штуку
};

extern const std::array<GenDef, kGenCount> kGens;

enum class Branch { Root, Gravity, Accretion, Cosmos, Dark };

enum NodeId {
    N_ROOT,
    // Гравитация — сила клика
    G_STRONG, G_TIDAL, G_COMBO, G_JETS, G_RESONANCE, G_WAVE, G_SPAGHETTI, G_KERR,
    // Аккреция — производство
    A_NEBULA, A_BELT, A_DISK, A_GIANTS, A_SYNERGY, A_FORGE, A_PHOTON, A_QUASAR,
    // Космос — события и удача
    C_WANDER, C_MAGNETAR, C_INTERCEPT, C_SUPERNOVA, C_METEOR, C_WORMHOLE, C_HAWKING, C_ATTRACTOR,
    // Тёмная материя — способности (цена в тёмной материи)
    D_WARP, D_RUSH, D_PORTAL, D_ECHO, D_FLOW, D_FOAM, D_ENERGY,
    N_COUNT
};

struct NodeDef {
    NodeId id;
    const char *name;
    const char *desc;
    double cost;     // масса, а для ветки Dark — тёмная материя
    NodeId parent;
    Branch branch;
    int row, col;    // позиция в дереве: ряд снизу вверх, колонка -1/0/1 внутри ветки
};

extern const std::array<NodeDef, N_COUNT> kNodes;

enum AchId {
    ACH_FIRST, ACH_CLICK100, ACH_CLICK1000, ACH_COMBO, ACH_CRIT, ACH_COMET1, ACH_COMET10,
    ACH_MASS1K, ACH_MASS1M, ACH_MASS1B, ACH_ALLGENS, ACH_TREE16, ACH_TREEALL, ACH_FRENZY,
    ACH_PERFECT, ACH_INTERCEPT, ACH_RIVAL, ACH_METEOR, ACH_ABILITY, ACH_MILESTONE, ACH_DARK,
    ACH_COUNT
};

struct AchDef {
    const char *name;
    const char *desc;
};

extern const std::array<AchDef, ACH_COUNT> kAchs;

enum Ability { AB_WARP, AB_RUSH, AB_PORTAL, AB_COUNT };

struct AbilityDef {
    const char *name;
    const char *desc;
    NodeId node;
    double cooldown;
    double duration;  // 0 — мгновенная
};

extern const std::array<AbilityDef, AB_COUNT> kAbilities;

enum class EvType {
    Click, Buy, Node, Achievement, Comet, Frenzy, Wave, Rank, CometSpawn, Collapse,
    Milestone, Flare, RivalSpawn, RivalHit, RivalKilled, RivalLeft, MeteorStart, Meteor,
    Intercept, Ability, Dark, Foam
};

struct Event {
    EvType type;
    double value = 0;
    int index = 0;
    bool crit = false;
    bool perfect = false;
};

class Game {
public:
    explicit Game(uint32_t seed = 1);
    void reset(uint32_t seed);

    void update(double dt);

    // Действия игрока. Возвращают true, если что-то произошло.
    bool click();
    int buyGen(int i, int amount);  // amount <= 0 — сколько хватит; возвращает купленное
    bool buyNode(NodeId id);
    bool catchComet();
    bool hitRival();
    bool catchMeteor();
    bool intercept(int gen);
    bool useAbility(Ability a);
    bool collapse();

    // Запросы
    double income() const;            // масса/с с учётом всех бонусов
    double genIncome(int i) const;    // доход одного типа объектов
    double baseClick() const;         // клик без крита, комбо и резонанса
    double comboMult() const;
    double comboMax() const;
    double critChance() const;
    double critMult() const;
    double genCost(int i) const;
    double genCostN(int i, int n) const;
    int genAffordable(int i) const;
    double genMult(int i) const;
    int milestoneLevel(int i) const;
    bool genVisible(int i) const;
    bool hasNode(NodeId id) const { return nodes_[id]; }
    bool nodeAvailable(NodeId id) const;  // родитель куплен, сам — нет
    bool canAffordNode(NodeId id) const;
    int nodesOwned() const;
    int achCount() const;
    bool hasAch(int a) const { return achs_[a]; }
    int rank() const;
    const char *rankName() const;
    double horizon() const;           // 0..1, растёт с массой
    double resonancePhase() const;    // 0..1, кольцо резонанса; около 1 — идеальный момент
    bool resonanceWindow() const;
    double timeScale() const { return abilityLeft[AB_WARP] > 0 ? 3.0 : 1.0; }
    bool abilityUnlocked(Ability a) const { return hasNode(kAbilities[a].node); }
    double abilityCooldown(Ability a) const;

    double mass = 0, total = 0, time = 0;
    double dark = 0, darkTotal = 0;   // тёмная материя
    long clicks = 0;
    int combo = 0;
    double comboTimer = 0;
    std::array<int, kGenCount> gens{};
    double frenzyLeft = 0;
    double cometLeft = 0, cometTotalTime = 9, cometTimer = 0;
    int cometsCaught = 0, crits = 0, perfects = 0, intercepts = 0, meteorsCaught = 0;
    // вспышка: один тип объектов ×5
    int flareGen = -1;
    double flareLeft = 0, flareTimer = 0;
    // соперник
    bool rivalActive = false;
    double rivalHp = 0, rivalMaxHp = 0, rivalStolen = 0, rivalLeft = 0, rivalTimer = 0;
    float rivalX = 0.5f, rivalY = 0.5f;  // позиция в долях центральной области
    int rivalsKilled = 0;
    // метеоритный дождь
    double meteorLeft = 0, meteorTimer = 0;
    // способности
    std::array<double, AB_COUNT> abilityCd{};
    std::array<double, AB_COUNT> abilityLeft{};
    bool won = false;

    std::vector<Event> events;

private:
    void gain(double m) { mass += m; total += m; }
    void gainDark(double d, const char *why);
    void checkAchievements();
    void unlock(AchId a);
    double rnd() { return std::uniform_real_distribution<double>(0, 1)(rng_); }
    double nextCometDelay();
    void tickEvents(double dt);

    std::array<bool, N_COUNT> nodes_{};
    std::array<bool, ACH_COUNT> achs_{};
    std::array<int, kGenCount> milestoneSeen_{};
    int lastRank_ = 0;
    std::mt19937 rng_;
};

std::string fmtNum(double v);

}  // namespace bh
