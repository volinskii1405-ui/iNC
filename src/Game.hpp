// Логика игры «Горизонт событий». Ничего не знает о графике и звуке:
// интерфейс читает состояние и забирает события из очереди.
#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace bh {

constexpr int kGenCount = 7;
constexpr double kGoal = 2e8;          // масса для поглощения Вселенной
constexpr double kCostGrowth = 1.17;

struct GenDef {
    const char *name;
    const char *desc;
    double baseCost;
    double rate;  // масса в секунду за штуку
};

extern const std::array<GenDef, kGenCount> kGens;

enum class Branch { Root, Gravity, Accretion, Cosmos };

enum NodeId {
    N_ROOT,
    // Гравитация — сила клика
    G_STRONG, G_TIDAL, G_COMBO, G_JETS, G_SPAGHETTI, G_WAVE,
    // Аккреция — производство
    A_NEBULA, A_BELT, A_DISK, A_GIANTS, A_PHOTON, A_QUASAR,
    // Космос — события и удача
    C_WANDER, C_MAGNETAR, C_SUPERNOVA, C_WORMHOLE, C_HAWKING, C_DARK,
    N_COUNT
};

struct NodeDef {
    NodeId id;
    const char *name;
    const char *desc;
    double cost;
    NodeId parent;
    Branch branch;
    float x, y;  // позиция на экране дерева (-1..1)
};

extern const std::array<NodeDef, N_COUNT> kNodes;

enum AchId {
    ACH_FIRST, ACH_CLICK100, ACH_CLICK500, ACH_COMBO, ACH_CRIT, ACH_COMET1, ACH_COMET5,
    ACH_MASS1K, ACH_MASS100K, ACH_MASS10M, ACH_ALLGENS, ACH_TREE9, ACH_TREEALL, ACH_FRENZY,
    ACH_COUNT
};

struct AchDef {
    const char *name;
    const char *desc;
};

extern const std::array<AchDef, ACH_COUNT> kAchs;

enum class EvType { Click, Buy, Node, Achievement, Comet, Frenzy, Wave, Rank, CometSpawn, Collapse };

struct Event {
    EvType type;
    double value = 0;
    int index = 0;
    bool crit = false;
};

class Game {
public:
    explicit Game(uint32_t seed = 1);
    void reset(uint32_t seed);

    void update(double dt);

    // Действия игрока. Возвращают true, если что-то произошло.
    bool click();
    bool buyGen(int i);
    bool buyNode(NodeId id);
    bool catchComet();
    bool collapse();

    // Запросы
    double income() const;            // масса/с с учётом всех бонусов
    double baseClick() const;         // клик без крита и комбо
    double comboMult() const;
    double comboMax() const;
    double critChance() const;
    double critMult() const;
    double genCost(int i) const;
    double genMult(int i) const;
    bool genVisible(int i) const;
    bool hasNode(NodeId id) const { return nodes_[id]; }
    bool nodeAvailable(NodeId id) const;  // родитель куплен, сам — нет
    int nodesOwned() const;
    int achCount() const;
    bool hasAch(int a) const { return achs_[a]; }
    int rank() const;
    const char *rankName() const;
    double horizon() const;  // 0..1, растёт с массой

    double mass = 0, total = 0, time = 0;
    long clicks = 0;
    int combo = 0;
    double comboTimer = 0;
    std::array<int, kGenCount> gens{};
    double frenzyLeft = 0;
    double cometLeft = 0;      // > 0 — комета на экране
    double cometTotalTime = 9;
    double cometTimer = 0;
    int cometsCaught = 0;
    int crits = 0;
    bool won = false;

    std::vector<Event> events;

private:
    void gain(double m) { mass += m; total += m; }
    void checkAchievements();
    void unlock(AchId a);
    double rnd() { return std::uniform_real_distribution<double>(0, 1)(rng_); }
    double nextCometDelay();

    std::array<bool, N_COUNT> nodes_{};
    std::array<bool, ACH_COUNT> achs_{};
    int lastRank_ = 0;
    std::mt19937 rng_;
};

std::string fmtNum(double v);

}  // namespace bh
