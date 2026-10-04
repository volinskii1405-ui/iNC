#include "Game.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace bh {

double kRingCost = 3.0, kLevelGrowthAdd = 0.5;
float kGrowthK = 0.45f;
float kBiteRatio = 0.5f, kBiteRate = 0.12f;
float kMaxGrowth = 3.0f;
double kCapCost = 50;  // за один заход дыра вырастает не больше чем втрое  // Ядро: с какого размера можно кусать и как быстро  // рост за заход: R = R0·(1 + k·ln(1 + съеденная площадь / R0²))

const std::array<TierDef, kTierCount> kTiers = {{
    {"Космическая пыль",    4,   1},
    {"Астероид",            8,   2.5},
    {"Комета",              11,  6},
    {"Луна",                17,  14},
    {"Планета",             27,  32},
    {"Газовый гигант",      42,  75},
    {"Звезда",              64,  170},
    {"Звёздное скопление",  100, 400},
    {"Галактика",           160, 900},
    {"Сверхскопление",      260, 2100},
    {"Квазар",              420, 5000},
    {"Великая стена",       680, 12000},
    {"Пузырь Хаббла",       1100, 28000},
}};

const std::array<UniverseDef, kUniverseCount> kUniverses = {{
    {"Млечная вселенная",     "Дом, милый дом", 1, 520,  1.0f},
    {"Кристальная вселенная", "Объекты ×3, опасностей больше", 3, 4800,  1.25f},
    {"Огненная вселенная",    "Объекты ×9, Ядро ещё крупнее", 9, 6800, 1.5f},
    {"Квантовая пена",        "Последняя. Объекты ×27", 27, 7800, 1.75f},
}};

namespace {

struct RawNode {
    const char *name, *desc;
    Stat stat;
    double value;
    int param, maxLevel;
    double cost, growth;
    const char *parent;
    Branch branch;
    int ring;
    float offset;
};

// Созвездие прокачки. Цены в массе, у ветки «Тёмная материя» — в тёмной материи.
const RawNode kRaw[] = {
    {"Сингулярность", "Отсюда растёт всё", ST_SIZE, 0, 0, 1, 0, 1, "", Branch::Root, 0, 0},

    // Гравитация — притяжение, скорость, что можно съесть
    {"Гравитация", "+15% радиус притяжения", ST_PULL, 0.15, 0, 5, 10, 1.6, "Сингулярность", Branch::Gravity, 1, 0},
    {"Скорость дрейфа", "+10% скорость дыры", ST_SPEED, 0.10, 0, 5, 25, 1.7, "Гравитация", Branch::Gravity, 2, -12},
    {"Жадный горизонт", "Можно есть объекты на 4% крупнее", ST_EAT, 0.04, 0, 5, 40, 1.8, "Гравитация", Branch::Gravity, 2, 12},
    {"Гравитационный рывок", "Пробел или ПКМ — рывок к курсору", ST_DASH, 1, 0, 1, 150, 1, "Скорость дрейфа", Branch::Gravity, 3, -18},
    {"Сила притяжения", "+25% сила притяжения", ST_PULLSTR, 0.25, 0, 4, 120, 1.8, "Жадный горизонт", Branch::Gravity, 3, 0},
    {"Приливный захват", "+10% радиус притяжения", ST_PULL, 0.10, 0, 5, 400, 1.8, "Жадный горизонт", Branch::Gravity, 3, 18},
    {"Короткий рывок", "Перезарядка рывка −15%", ST_DASHCD, 0.15, 0, 4, 600, 2.0, "Гравитационный рывок", Branch::Gravity, 4, -21},
    {"Инерция", "+8% скорость дыры", ST_SPEED, 0.08, 0, 5, 1500, 1.8, "Сила притяжения", Branch::Gravity, 4, -7},
    {"Горизонт Керра", "Можно есть объекты на 5% крупнее", ST_EAT, 0.05, 0, 4, 3000, 2.0, "Приливный захват", Branch::Gravity, 4, 7},
    {"Гравитационный коллапс", "Q — затянуть всё съедобное на экране", ST_COLLAPSE, 1, 0, 1, 5000, 1, "Приливный захват", Branch::Gravity, 4, 21},
    {"Сверхтекучесть", "+10% скорость дыры", ST_SPEED, 0.10, 0, 5, 3e4, 1.9, "Инерция", Branch::Gravity, 5, -11},
    {"Мега-коллапс", "Коллапс сильнее на 50%, перезарядка −15%", ST_COLLAPSEPOW, 0.5, 0, 4, 4e4, 2.2, "Гравитационный коллапс", Branch::Gravity, 5, 11},
    {"Пожиратель", "Можно есть объекты на 5% крупнее", ST_EAT, 0.05, 0, 3, 1e5, 2.5, "Горизонт Керра", Branch::Gravity, 5, 22},
    {"Сингулярный разгон", "+20% радиус притяжения", ST_PULL, 0.20, 0, 5, 5e5, 2.0, "Сверхтекучесть", Branch::Gravity, 6, -6},
    {"Укус горизонта", "Ядро кусается на 15% быстрее", ST_BITE, 0.15, 0, 5, 2e6, 2.0, "Пожиратель", Branch::Gravity, 6, 18},
    {"Гравитационная линза", "+25% радиус притяжения", ST_PULL, 0.25, 0, 5, 3e6, 2.0, "Сингулярный разгон", Branch::Gravity, 7, -6},

    // Рост — стартовый размер, рост во время захода, спутники
    {"Масса покоя", "+3 к стартовому радиусу", ST_SIZE, 3, 0, 5, 15, 1.6, "Сингулярность", Branch::Growth, 1, 0},
    {"Аппетит", "+15% рост от поглощения", ST_GROWTH, 0.15, 0, 5, 40, 1.7, "Масса покоя", Branch::Growth, 2, -12},
    {"Плотное ядро", "+4 к стартовому радиусу", ST_SIZE, 4, 0, 5, 120, 1.8, "Масса покоя", Branch::Growth, 2, 12},
    {"Метаболизм", "+15% рост от поглощения", ST_GROWTH, 0.15, 0, 5, 500, 1.8, "Аппетит", Branch::Growth, 3, -18},
    {"Аккреционный спутник", "Мини-дыра кружит рядом и ест мелочь", ST_SAT, 1, 0, 1, 800, 1, "Аппетит", Branch::Growth, 3, 0},
    {"Массивный старт", "+8 к стартовому радиусу", ST_SIZE, 8, 0, 5, 2000, 1.9, "Плотное ядро", Branch::Growth, 3, 18},
    {"Ненасытность", "+20% рост от поглощения", ST_GROWTH, 0.20, 0, 5, 1e4, 1.9, "Метаболизм", Branch::Growth, 4, -21},
    {"Второй спутник", "+1 спутник", ST_SAT, 1, 0, 1, 1.5e4, 1, "Аккреционный спутник", Branch::Growth, 4, -7},
    {"Крупные спутники", "+25% размер спутников", ST_SATSIZE, 0.25, 0, 4, 2e4, 2.0, "Аккреционный спутник", Branch::Growth, 4, 7},
    {"Звёздный старт", "+15 к стартовому радиусу", ST_SIZE, 15, 0, 5, 4e4, 2.0, "Массивный старт", Branch::Growth, 4, 21},
    {"Обжорство", "+25% рост от поглощения", ST_GROWTH, 0.25, 0, 4, 3e5, 2.0, "Ненасытность", Branch::Growth, 5, -22},
    {"Третий спутник", "+1 спутник", ST_SAT, 1, 0, 1, 2e5, 1, "Второй спутник", Branch::Growth, 5, -11},
    {"Галактический старт", "+30 к стартовому радиусу", ST_SIZE, 30, 0, 5, 5e5, 2.0, "Звёздный старт", Branch::Growth, 5, 11},
    {"Четвёртый спутник", "+1 спутник", ST_SAT, 1, 0, 1, 3e6, 1, "Третий спутник", Branch::Growth, 6, -6},
    {"Пищеварение звёзд", "+30% рост от поглощения", ST_GROWTH, 0.30, 0, 5, 2e6, 2.0, "Обжорство", Branch::Growth, 6, -18},
    {"Вселенский старт", "+60 к стартовому радиусу", ST_SIZE, 60, 0, 5, 5e6, 2.0, "Галактический старт", Branch::Growth, 6, 18},
    {"Пятый спутник", "+1 спутник", ST_SAT, 1, 0, 1, 3e7, 1, "Четвёртый спутник", Branch::Growth, 7, -6},

    // Время — длительность захода, часы, защита
    {"Стабильный горизонт", "+3 с к заходу", ST_TIME, 3, 0, 5, 12, 1.6, "Сингулярность", Branch::Time, 1, 0},
    {"Квантовые часы", "Чаще попадаются часы (+2 с)", ST_CLOCK, 0.006, 0, 5, 60, 1.8, "Стабильный горизонт", Branch::Time, 2, -12},
    {"Щит Хокинга", "−15% урона от опасностей", ST_ARMOR, 0.15, 0, 5, 200, 1.8, "Стабильный горизонт", Branch::Time, 2, 12},
    {"Застывшее время", "+3 с к заходу", ST_TIME, 3, 0, 5, 800, 1.8, "Квантовые часы", Branch::Time, 3, -18},
    {"Часовщик", "Часы дают на 1 с больше", ST_CLOCKVAL, 1, 0, 3, 1500, 2.0, "Квантовые часы", Branch::Time, 3, 0},
    {"Отражение", "−10% урона от опасностей", ST_ARMOR, 0.10, 0, 3, 2500, 2.0, "Щит Хокинга", Branch::Time, 3, 18},
    {"Замедление", "+4 с к заходу", ST_TIME, 4, 0, 5, 1.2e4, 1.9, "Застывшее время", Branch::Time, 4, -21},
    {"Пожиратель времени", "Звезда и крупнее: +0.5 с", ST_TIMEFEED, 0.5, 0, 3, 2e4, 2.0, "Часовщик", Branch::Time, 4, -7},
    {"Антиматерия на ужин", "Антиматерию можно съесть — она ценная", ST_ANTIEAT, 1, 0, 1, 3e4, 1, "Отражение", Branch::Time, 4, 7},
    {"Петля времени", "+25% шанс второй жизни: +8 с в конце", ST_LOOP, 0.25, 0, 3, 3e5, 2.2, "Отражение", Branch::Time, 4, 21},
    {"Вечность", "+3 с к заходу", ST_TIME, 3, 0, 5, 2e5, 2.0, "Замедление", Branch::Time, 5, -11},
    {"Остановка времени", "+4 с к заходу", ST_TIME, 4, 0, 3, 3e6, 2.5, "Вечность", Branch::Time, 6, -6},
    {"Хроносфера", "Артефакты действуют на 30% дольше", ST_POWERDUR, 0.30, 0, 3, 1e6, 2.2, "Петля времени", Branch::Time, 5, 11},
    {"Бесконечность", "+5 с к заходу", ST_TIME, 5, 0, 3, 3e7, 2.5, "Остановка времени", Branch::Time, 7, -6},

    // Богатство — ценность добычи
    {"Плотная материя", "+25% масса от всего", ST_VALUE, 0.25, 0, 5, 15, 1.6, "Сингулярность", Branch::Wealth, 1, 0},
    {"Каменоломня", "+50% масса астероидов и комет", ST_TIERVAL, 0.5, 1, 5, 50, 1.7, "Плотная материя", Branch::Wealth, 2, -12},
    {"Критическая масса", "+4% шанс крита (×5 массы)", ST_CRIT, 0.04, 0, 5, 150, 1.8, "Плотная материя", Branch::Wealth, 2, 12},
    {"Лунные копи", "+50% масса лун и планет", ST_TIERVAL, 0.5, 3, 5, 600, 1.8, "Каменоломня", Branch::Wealth, 3, -18},
    {"Пиршество", "Предел комбо +0.5", ST_COMBO, 0.5, 0, 5, 900, 1.8, "Каменоломня", Branch::Wealth, 3, 0},
    {"Сверхкрит", "Криты +2× сильнее", ST_CRITMULT, 2, 0, 4, 2000, 2.0, "Критическая масса", Branch::Wealth, 3, 18},
    {"Звёздная кузня", "+50% масса гигантов и звёзд", ST_TIERVAL, 0.5, 5, 5, 1e4, 1.9, "Лунные копи", Branch::Wealth, 4, -21},
    {"Сложный процент", "+2% к накоплениям после захода", ST_INTEREST, 0.02, 0, 5, 1.5e4, 2.0, "Пиршество", Branch::Wealth, 4, -7},
    {"Долгий пир", "Окно комбо +0.2 с", ST_COMBOWIN, 0.2, 0, 3, 2e4, 2.0, "Пиршество", Branch::Wealth, 4, 7},
    {"Золотая лихорадка", "+40% масса от всего", ST_VALUE, 0.40, 0, 5, 5e4, 2.0, "Сверхкрит", Branch::Wealth, 4, 21},
    {"Галактические богатства", "+60% масса скоплений и галактик", ST_TIERVAL, 0.6, 7, 5, 2e5, 2.0, "Звёздная кузня", Branch::Wealth, 5, -11},
    {"Квазарный блеск", "+50% масса от всего", ST_VALUE, 0.50, 0, 5, 8e5, 2.0, "Золотая лихорадка", Branch::Wealth, 5, 11},
    {"Нобелевка Хокинга", "Масса от всего ×2", ST_VALUEX, 2, 0, 3, 5e6, 3.0, "Квазарный блеск", Branch::Wealth, 6, 6},
    {"Вселенские богатства", "+60% масса квазаров, стен и пузырей", ST_TIERVAL, 0.6, 10, 5, 3e6, 2.0, "Галактические богатства", Branch::Wealth, 6, -18},
    {"Мультивселенский капитал", "Масса от всего ×2", ST_VALUEX, 2, 0, 3, 5e7, 3.0, "Нобелевка Хокинга", Branch::Wealth, 7, 6},

    // Космос — что встречается в космосе
    {"Плотная туманность", "+20% объектов вокруг", ST_DENSITY, 0.20, 0, 5, 20, 1.6, "Сингулярность", Branch::Cosmos, 1, 0},
    {"Кометный поток", "Чаще золотые кометы (×20 массы)", ST_GOLD, 0.004, 0, 3, 100, 2.0, "Плотная туманность", Branch::Cosmos, 2, -12},
    {"Богатый сектор", "Крупные объекты попадаются чаще", ST_RICH, 0.06, 0, 5, 250, 1.9, "Плотная туманность", Branch::Cosmos, 2, 12},
    {"Цепная реакция", "+20% шанс: съеденная звезда взрывается в еду", ST_CHAIN, 0.20, 0, 3, 1500, 2.0, "Кометный поток", Branch::Cosmos, 3, -18},
    {"Звёздные ясли", "+20% объектов вокруг", ST_DENSITY, 0.20, 0, 5, 2000, 1.9, "Кометный поток", Branch::Cosmos, 3, 0},
    {"Охота на соперников", "Дыры-соперники дают ×2 массы", ST_RIVAL, 1.0, 0, 3, 4000, 2.2, "Богатый сектор", Branch::Cosmos, 3, 18},
    {"Сверхновые", "+15% шанс цепной реакции", ST_CHAIN, 0.15, 0, 3, 2e4, 2.2, "Цепная реакция", Branch::Cosmos, 4, -21},
    {"Метеоритные рои", "Появляются рои астероидов", ST_SWARM, 1, 0, 3, 2.5e4, 2.0, "Звёздные ясли", Branch::Cosmos, 4, -7},
    {"Магнитное поле", "+50% дальность сбора часов и материи", ST_MAGNET, 0.5, 0, 3, 3e4, 2.0, "Звёздные ясли", Branch::Cosmos, 4, 7},
    {"Богатая Вселенная", "Крупные объекты попадаются чаще", ST_RICH, 0.06, 0, 5, 8e4, 2.0, "Охота на соперников", Branch::Cosmos, 4, 21},
    {"Плотная Вселенная", "+25% объектов вокруг", ST_DENSITY, 0.25, 0, 5, 4e5, 2.0, "Метеоритные рои", Branch::Cosmos, 5, -11},
    {"Космическая паутина", "Крупные объекты попадаются чаще", ST_RICH, 0.08, 0, 3, 1e6, 2.5, "Богатая Вселенная", Branch::Cosmos, 5, 11},
    {"Ядро Вселенной", "В космосе появится Ядро Вселенной — откуси его!", ST_FINAL, 1, 0, 1, 4e6, 1, "Космическая паутина", Branch::Cosmos, 6, 6},
    {"Космические артефакты", "Артефакты попадаются чаще", ST_POWER, 0.004, 0, 5, 5000, 2.0, "Звёздные ясли", Branch::Cosmos, 5, 0},
    {"Охотник за ядрами", "Ядро кусается на 15% быстрее", ST_BITE, 0.15, 0, 3, 5e6, 2.2, "Космическая паутина", Branch::Cosmos, 6, 18},

    // Тёмная материя — особые бонусы за вторую валюту
    {"Тёмный магнит", "Тёмная материя попадается чаще", ST_DARK, 0.004, 0, 3, 2, 1.5, "Сингулярность", Branch::Dark, 1, 0},
    {"Тёмное ускорение", "+20% скорость дыры", ST_SPEED, 0.20, 0, 3, 3, 1.6, "Тёмный магнит", Branch::Dark, 2, -12},
    {"Тёмный рост", "+30% рост от поглощения", ST_GROWTH, 0.30, 0, 3, 3, 1.6, "Тёмный магнит", Branch::Dark, 2, 12},
    {"Фантомный рывок", "Во время рывка опасности не ранят", ST_PHANTOM, 1, 0, 1, 6, 1, "Тёмное ускорение", Branch::Dark, 3, -18},
    {"Тёмная корона", "+3 с к заходу", ST_TIME, 3, 0, 3, 5, 1.7, "Тёмное ускорение", Branch::Dark, 3, 0},
    {"Тёмное богатство", "Масса от всего ×1.5", ST_VALUEX, 1.5, 0, 3, 5, 1.8, "Тёмный рост", Branch::Dark, 3, 18},
    {"Тёмный спутник", "+1 спутник", ST_SAT, 1, 0, 1, 10, 1, "Тёмная корона", Branch::Dark, 4, -7},
    {"Тёмный горизонт", "Можно есть объекты на 8% крупнее", ST_EAT, 0.08, 0, 2, 12, 1.8, "Тёмное богатство", Branch::Dark, 4, 7},
    {"Тёмная энергия", "Масса от всего ×2", ST_VALUEX, 2, 0, 2, 20, 2.0, "Тёмный горизонт", Branch::Dark, 5, 0},
    {"Тёмные артефакты", "Артефакты попадаются чаще", ST_POWER, 0.004, 0, 3, 8, 1.6, "Тёмный спутник", Branch::Dark, 5, -11},
    {"Тёмный укус", "Ядро кусается на 25% быстрее", ST_BITE, 0.25, 0, 2, 15, 1.8, "Тёмный горизонт", Branch::Dark, 5, 11},
    {"Тёмная вселенная", "Масса от всего ×2", ST_VALUEX, 2, 0, 3, 30, 2.0, "Тёмная энергия", Branch::Dark, 6, 0},
};

std::vector<NodeDef> buildNodes()
{
    std::vector<NodeDef> out;
    for (const RawNode &r : kRaw) {
        int parent = 0;
        for (size_t i = 0; i < out.size(); i++)
            if (std::strcmp(out[i].name, r.parent) == 0) parent = (int)i;
        out.push_back({r.name, r.desc, r.stat, r.value, r.param, r.maxLevel, r.cost, r.growth, parent, r.branch, r.ring, r.offset});
    }
    return out;
}

}  // namespace

const std::vector<NodeDef> kNodes = buildNodes();

Game::Game(uint32_t seed) { reset(seed); }

void Game::reset(uint32_t seed)
{
    rng_.seed(seed);
    levels_.assign(kNodes.size(), 0);
    levels_[0] = 1;
    phase = Phase::Tree;
    mass = dark = total = playTime = 0;
    runs = 0;
    bestR = 0;
    universe = 0;
    objs.clear();
    events.clear();
    recompute();
}

void Game::recompute()
{
    Stats s;
    double eatBonus = 0;
    for (size_t i = 1; i < kNodes.size(); i++) {
        int l = levels_[i];
        if (!l) continue;
        const NodeDef &n = kNodes[i];
        double v = n.value * l;
        switch (n.stat) {
        case ST_SIZE: s.size += v; break;
        case ST_SPEED: s.speed += v; break;
        case ST_PULL: s.pull += v; break;
        case ST_PULLSTR: s.pullStr += v; break;
        case ST_EAT: eatBonus += v; break;
        case ST_GROWTH: s.growth += v; break;
        case ST_TIME: s.time += v; break;
        case ST_VALUE: s.value += v; break;
        case ST_VALUEX: break;  // ниже, мультипликативно
        case ST_TIERVAL:
            for (int t = n.param; t < n.param + 2 && t < kTierCount; t++) s.tierVal[t] += v;
            if (n.param == 7) s.tierVal[9] += v;
            if (n.param == 10) s.tierVal[12] += v;
            break;
        case ST_DENSITY: s.density += v; break;
        case ST_RICH: s.rich += v; break;
        case ST_COMBO: s.comboMax += v; break;
        case ST_COMBOWIN: s.comboWin += v; break;
        case ST_CRIT: s.crit += v; break;
        case ST_CRITMULT: s.critMult += v; break;
        case ST_CLOCK: s.clock += v; break;
        case ST_CLOCKVAL: s.clockVal += v; break;
        case ST_ARMOR: s.armor += v; break;
        case ST_GOLD: s.gold += v; break;
        case ST_CHAIN: s.chain += v; break;
        case ST_SAT: s.sats += l; break;
        case ST_SATSIZE: s.satSize += v; break;
        case ST_DASH: s.dash = true; break;
        case ST_DASHCD: s.dashCd *= std::pow(1 - n.value, l); break;
        case ST_COLLAPSE: s.collapse = true; break;
        case ST_COLLAPSEPOW: s.collapsePow += v; s.collapseCd *= std::pow(0.85, l); break;
        case ST_DARK: s.dark += v; break;
        case ST_INTEREST: s.interest += v; break;
        case ST_MAGNET: s.magnet += v; break;
        case ST_TIMEFEED: s.timeFeed += v; break;
        case ST_ANTIEAT: s.antiEat = true; break;
        case ST_LOOP: s.loop += v; break;
        case ST_RIVAL: s.rival += v; break;
        case ST_SWARM: s.swarm += l; break;
        case ST_PHANTOM: s.phantom = true; break;
        case ST_FINAL: s.final = true; break;
        case ST_BITE: s.bite += v; break;
        case ST_POWER: s.power += v; break;
        case ST_POWERDUR: s.powerDur += v; break;
        }
        if (n.stat == ST_VALUEX) s.value *= std::pow(n.value, l);
    }
    s.eat += eatBonus;
    s.armor = std::min(s.armor, 0.8);
    st_ = s;
}

bool Game::nodeVisible(int i) const { return i == 0 || levels_[kNodes[i].parent] > 0; }

int Game::levelCap(int i) const { return kNodes[i].maxLevel + (kNodes[i].maxLevel > 1 ? 2 * universe : 0); }

bool Game::nodeAvailable(int i) const { return i > 0 && nodeVisible(i) && levels_[i] < levelCap(i); }

double Game::nodeCost(int i) const
{
    const NodeDef &n = kNodes[i];
    if (n.branch == Branch::Dark) return std::floor(n.cost * std::pow(n.growth, levels_[i]));
    // Цены в массе дорожают быстрее у дальних колец созвездия.
    double c = n.cost * std::pow(kRingCost, std::max(0, n.ring - 1)) * std::pow(n.growth + kLevelGrowthAdd, levels_[i]);
    // уровни сверх изначального предела (открываются в новых вселенных) — заметно дороже
    int extra = levels_[i] - n.maxLevel + 1;
    if (extra > 0) c *= std::pow(kCapCost, extra);
    return std::floor(c);
}

bool Game::canBuy(int i) const
{
    if (!nodeAvailable(i)) return false;
    return kNodes[i].branch == Branch::Dark ? dark >= nodeCost(i) : mass >= nodeCost(i);
}

bool Game::buyNode(int i)
{
    if (phase != Phase::Tree || !canBuy(i)) return false;
    double c = nodeCost(i);
    if (kNodes[i].branch == Branch::Dark) dark -= c;
    else mass -= c;
    levels_[i]++;
    recompute();
    events.push_back({EvType::NodeBuy, c, i});
    return true;
}

int Game::affordableCount() const
{
    int n = 0;
    for (size_t i = 1; i < kNodes.size(); i++) n += canBuy((int)i);
    return n;
}

// ---------- заход ----------

float Game::zoomFor(float r) const
{
    float screenR = 26 + 14 * std::log(1 + r / 15.0f);
    return screenR / r;
}

float Game::viewRadius() const { return kScreenHalfDiag / zoomFor(R); }

bool Game::kindOnField(Kind k) const
{
    for (auto &o : objs)
        if (o.kind == k && !o.dead) return true;
    return false;
}

bool Game::canEat(const Obj &o) const
{
    switch (o.kind) {
    case K_CLOCK: case K_DARK: case K_MAGNET: case K_NOVA: case K_CHRONO: case K_DOUBLE: return true;
    case K_CORE: return false;  // Ядро не глотают целиком — его кусают
    case K_ANTI: return st_.antiEat && o.size < R * st_.eat;
    case K_PULSAR: return o.size < R * st_.eat * 0.5f;  // пульсар плотный: нужна дыра вдвое крупнее
    default: return o.size < R * st_.eat;
    }
}

double Game::tierValue(int t) const { return kTiers[t].value * st_.tierVal[t] * st_.value * kUniverses[universe].valueMult; }

int Game::pickTier()
{
    // Размеры вокруг текущего радиуса дыры: и еда, и препятствия покрупнее.
    float center = std::log(R * (0.55f + (float)st_.rich));
    double w[kTierCount], sum = 0;
    for (int t = 0; t < kTierCount; t++) {
        float d = std::log(kTiers[t].size) - center;
        w[t] = std::exp(-d * d / (2 * 0.85f * 0.85f)) + (t < 2 ? 0.15 : 0);
        if (kTiers[t].size > R * 4) w[t] = 0;  // совсем гигантов не показываем
        sum += w[t];
    }
    double x = rnd() * sum;
    for (int t = 0; t < kTierCount; t++) {
        x -= w[t];
        if (x <= 0) return t;
    }
    return 0;
}

Obj Game::makeObj(int tier, Vec p)
{
    Obj o;
    o.kind = K_TIER;
    o.tier = tier;
    o.p = p;
    o.size = kTiers[tier].size * rnd(0.85f, 1.15f);
    float a = rnd(0, 6.2832f), sp = rnd(2, 10) * (1 + o.size * 0.02f);
    if (tier == 2) sp *= 8;  // кометы летят быстро
    o.v = {std::cos(a) * sp, std::sin(a) * sp};
    o.rot = rnd(0, 360);
    o.spin = rnd(-40, 40);
    o.id = nextId_++;
    return o;
}

void Game::spawnAround(bool initial)
{
    float vr = viewRadius();
    float area = vr * vr;
    // Сколько объектов держать вокруг: плотность в «экранах», не в мировых единицах.
    int target = (int)(90 * st_.density);
    int count = 0;
    for (auto &o : objs)
        if (!o.dead) count++;
    int need = target - count;
    (void)area;
    float hz = kUniverses[universe].hazard;
    for (int k = 0; k < need; k++) {
        float a = rnd(0, 6.2832f);
        float d = initial ? rnd(R * 3, vr * 1.25f) : rnd(vr * 1.05f, vr * 1.35f);
        Vec p = {pos.x + std::cos(a) * d, pos.y + std::sin(a) * d};
        float roll = rnd();
        Obj o;
        if (roll < st_.clock && clockCd_ <= 0 && !kindOnField(K_CLOCK)) {
            clockCd_ = 4;
            o = makeObj(0, p);
            o.kind = K_CLOCK;
            o.size = std::max(6.0f, R * 0.35f);
        } else if (roll < st_.clock + st_.dark && darkCd_ <= 0 && !kindOnField(K_DARK)) {
            darkCd_ = 5;
            o = makeObj(0, p);
            o.kind = K_DARK;
            o.size = std::max(6.0f, R * 0.35f);
        } else if (roll < st_.clock + st_.dark + st_.gold && goldCd_ <= 0 && !kindOnField(K_GOLD)) {
            goldCd_ = 8;
            o = makeObj(0, p);
            o.kind = K_GOLD;
            o.size = R * 0.4f;
            float sa = rnd(0, 6.2832f);
            float sp = 260 / zoomFor(R);
            o.v = {std::cos(sa) * sp, std::sin(sa) * sp};
        } else if (roll < st_.clock + st_.dark + st_.gold + st_.power && powerCd_ <= 0 && !kindOnField(K_MAGNET) &&
                   !kindOnField(K_NOVA) && !kindOnField(K_CHRONO) && !kindOnField(K_DOUBLE)) {
            // артефакт: магнит, сверхновая, хроносфера или удвоитель
            powerCd_ = 7;
            o = makeObj(0, p);
            static const Kind kinds[] = {K_MAGNET, K_NOVA, K_CHRONO, K_DOUBLE};
            o.kind = kinds[(int)(rnd() * 4) % 4];
            o.size = std::max(7.0f, R * 0.4f);
            o.spin = 60;
        } else if (runs >= 2 && roll < st_.clock + st_.dark + st_.gold + st_.power + 0.035f * hz) {
            o = makeObj(0, p);
            o.kind = K_ANTI;
            o.size = R * rnd(0.35f, 0.7f);
        } else if (runs >= 4 && roll < st_.clock + st_.dark + st_.gold + st_.power + 0.05f * hz) {
            o = makeObj(0, p);
            o.kind = K_PULSAR;
            o.size = R * rnd(0.4f, 1.2f);
            o.v = {0, 0};
            o.beam = rnd(0, 6.2832f);
        } else if (runs >= 3 && roll < st_.clock + st_.dark + st_.gold + st_.power + 0.065f * hz) {
            o = makeObj(0, p);
            o.kind = K_RIVAL;
            o.size = R * rnd(0.5f, 1.5f);
        } else if (st_.swarm > 0 && roll < st_.clock + st_.dark + st_.gold + st_.power + 0.065f * hz + 0.02f * st_.swarm) {
            // рой: несколько объектов помельче кучкой
            int tier = std::max(0, pickTier() - 1);
            for (int s = 0; s < 6; s++) {
                Obj f = makeObj(tier, {p.x + rnd(-1, 1) * R * 2, p.y + rnd(-1, 1) * R * 2});
                objs.push_back(f);
            }
            continue;
        } else {
            o = makeObj(pickTier(), p);
        }
        objs.push_back(o);
    }
}

void Game::startRun()
{
    if (phase != Phase::Tree) return;
    phase = Phase::Run;
    runs++;
    pos = {0, 0};
    vel = {0, 0};
    R = (float)st_.size;
    timeLeft = st_.time;
    runTime = 0;
    runMass = 0;
    runEaten = 0;
    biggestTier = -1;
    combo = 0;
    comboTimer = 0;
    dashCd = dashLeft = collapseCd = collapseLeft = 0;
    loopUsed = false;
    hurtFlash = 0;
    runArea_ = 0;
    timeGained_ = 0;
    clockCd_ = 3;
    darkCd_ = 2;
    goldCd_ = 6;
    powerCd_ = 5;
    biteFx_ = 0;
    magnetLeft = chronoLeft = doubleLeft = 0;
    objs.clear();
    spawnAround(true);
    if (st_.final) {
        // Ядро Вселенной ждёт неподалёку
        Obj core = makeObj(0, {R * 22, -R * 14});
        core.kind = K_CORE;
        core.size = kUniverses[universe].coreSize;
        core.hp = 1;
        core.v = {0, 0};
        core.spin = 6;
        objs.push_back(core);
    }
    events.push_back({EvType::RunStart});
}

void Game::addTime(double s)
{
    double room = st_.time * 0.5 - timeGained_;
    double add = std::max(0.0, std::min(s, room));
    timeGained_ += add;
    timeLeft += add;
}

void Game::hurt(double seconds, Vec at)
{
    if (st_.phantom && dashLeft > 0) return;
    if (chronoLeft > 0) return;  // хроносфера защищает
    double d = seconds * (1 - st_.armor);
    timeLeft -= d;
    hurtFlash = 1;
    events.push_back({EvType::Hurt, d, 0, at});
}

void Game::eat(Obj &target, bool bySat)
{
    target.dead = true;
    const Obj o = target;  // копия: ниже objs может вырасти и ссылка станет недействительной
    Event e{EvType::Eat, 0, o.tier, o.p};
    e.size = o.size;
    switch (o.kind) {
    case K_CLOCK: {
        double add = st_.clockVal;
        addTime(add);
        events.push_back({EvType::Clock, add, 0, o.p});
        return;
    }
    case K_DARK:
        dark += 1;
        events.push_back({EvType::Dark, 1, 0, o.p});
        return;
    case K_MAGNET: case K_NOVA: case K_CHRONO: case K_DOUBLE: {
        double dur = 7 * st_.powerDur;
        if (o.kind == K_MAGNET) magnetLeft = dur;
        if (o.kind == K_CHRONO) chronoLeft = dur * 0.8;
        if (o.kind == K_DOUBLE) doubleLeft = dur;
        if (o.kind == K_NOVA) nova();
        events.push_back({EvType::PowerUp, dur, (int)o.kind, o.p});
        return;
    }
    default: break;
    }

    double v;
    int growthTier = o.tier;
    if (o.kind == K_GOLD) {
        // золотая комета: ×20 к лучшему из того, что сейчас по зубам
        int t = 0;
        for (int k = 0; k < kTierCount; k++)
            if (kTiers[k].size < R * st_.eat) t = k;
        v = tierValue(t) * 20;
        events.push_back({EvType::Gold, v, 0, o.p});
    } else if (o.kind == K_ANTI) {
        int t = 0;
        for (int k = 0; k < kTierCount; k++)
            if (kTiers[k].size < o.size) t = k;
        v = tierValue(t) * 8;
    } else if (o.kind == K_PULSAR) {
        v = tierValue(6) * 3;
    } else if (o.kind == K_RIVAL) {
        v = 40 * std::pow(o.size, 1.6) * st_.value * st_.rival;
        events.push_back({EvType::RivalEaten, v, 0, o.p});
    } else {
        v = tierValue(o.tier);
    }

    if (!bySat) {
        combo++;
        comboTimer = st_.comboWin;
    }
    double cm = std::min(st_.comboMax, 1.0 + combo * 0.03);
    v *= cm;
    if (doubleLeft > 0) v *= 2;
    if (rnd() < st_.crit) {
        v *= st_.critMult;
        e.crit = true;
    }
    mass += v;
    total += v;
    runMass += v;
    runEaten++;
    if (o.kind == K_TIER) biggestTier = std::max(biggestTier, o.tier);
    e.value = v;
    e.index = o.kind == K_TIER ? growthTier : -1 - (int)o.kind;
    if (bySat) e.type = EvType::SatEat;
    events.push_back(e);

    // Рост: площадь съеденного добавляется к площади горизонта.
    if (!bySat) {
        runArea_ += o.size * o.size * (float)st_.growth;
        float R0 = (float)st_.size;
        R = R0 * std::min(kMaxGrowth, 1 + kGrowthK * std::log(1 + runArea_ / (R0 * R0)));
        bestR = std::max(bestR, (double)R);
    }

    // Крупная добыча (не меньше половины дыры): время и цепная реакция.
    if (o.kind == K_TIER && o.size >= R * 0.45f && !bySat) {
        if (st_.timeFeed > 0) addTime(st_.timeFeed);
        if (o.tier >= 3 && rnd() < st_.chain) {
            // Цепная реакция: звезда взрывается и разбрасывает еду
            int ft = std::max(0, o.tier - 1);
            for (int k = 0; k < 9; k++) {
                float a = k * 0.698f + rnd(0, 0.4f);
                Obj f = makeObj(ft, {o.p.x + std::cos(a) * o.size * 1.5f, o.p.y + std::sin(a) * o.size * 1.5f});
                f.v = {std::cos(a) * o.size * 2, std::sin(a) * o.size * 2};
                objs.push_back(f);
            }
            events.push_back({EvType::Chain, 0, o.tier, o.p});
        }
    }
}

void Game::nova()
{
    // Сверхновая: всё крупное поблизости раскалывается на съедобные осколки.
    int ft = 0;
    for (int k = 0; k < kTierCount; k++)
        if (kTiers[k].size < R * st_.eat * 0.8f) ft = k;
    float vr = viewRadius();
    std::vector<Obj> shards;
    for (auto &o : objs) {
        if (o.dead || o.kind == K_CORE || canEat(o)) continue;
        if (o.kind != K_TIER && o.kind != K_ANTI && o.kind != K_PULSAR && o.kind != K_RIVAL) continue;
        float dx = o.p.x - pos.x, dy = o.p.y - pos.y;
        if (dx * dx + dy * dy > vr * vr * 0.6f) continue;
        o.dead = true;
        int n = o.kind == K_TIER ? 7 : 3;
        for (int k = 0; k < n; k++) {
            float a = rnd(0, 6.2832f);
            Obj f = makeObj(ft, {o.p.x + std::cos(a) * o.size * 0.6f, o.p.y + std::sin(a) * o.size * 0.6f});
            f.v = {std::cos(a) * o.size, std::sin(a) * o.size};
            shards.push_back(f);
        }
    }
    for (auto &f : shards) objs.push_back(f);
    events.push_back({EvType::Nova, (double)shards.size(), 0, pos});
}

void Game::nextUniverse()
{
    if (phase != Phase::UniverseClear) return;
    universe = std::min(universe + 1, kUniverseCount - 1);
    phase = Phase::Tree;
}

void Game::update(double dt, Vec target, bool wantDash, bool wantCollapse)
{
    if (phase != Phase::Run) return;
    runTime += dt;
    playTime += dt;
    magnetLeft = std::max(0.0, magnetLeft - dt);
    doubleLeft = std::max(0.0, doubleLeft - dt);
    if (chronoLeft > 0) chronoLeft = std::max(0.0, chronoLeft - dt);  // хроносфера: время стоит
    else timeLeft -= dt;
    hurtFlash = std::max(0.0, hurtFlash - dt * 3);
    if (comboTimer > 0) {
        comboTimer -= dt;
        if (comboTimer <= 0) combo = 0;
    }
    dashCd = std::max(0.0, dashCd - dt);
    dashLeft = std::max(0.0, dashLeft - dt);
    collapseCd = std::max(0.0, collapseCd - dt);
    collapseLeft = std::max(0.0, collapseLeft - dt);

    float zoom = zoomFor(R);
    float speed = 300 * (float)st_.speed / zoom;  // постоянная скорость в пикселях экрана

    // Движение к курсору
    Vec d = {target.x - pos.x, target.y - pos.y};
    float dist = std::sqrt(d.x * d.x + d.y * d.y);
    Vec want = {0, 0};
    if (dist > 1) {
        float k = std::min(1.0f, dist / (R * 3)) * speed / dist;
        want = {d.x * k, d.y * k};
    }
    if (wantDash && st_.dash && dashCd <= 0 && dist > 1) {
        dashCd = st_.dashCd;
        dashLeft = 0.3;
        vel = {d.x / dist * speed * 4, d.y / dist * speed * 4};
        events.push_back({EvType::Dash, 0, 0, pos});
    }
    if (wantCollapse && st_.collapse && collapseCd <= 0) {
        collapseCd = st_.collapseCd;
        collapseLeft = 1.6;
        events.push_back({EvType::Collapse, 0, 0, pos});
    }
    float blend = dashLeft > 0 ? 0.02f : std::min(1.0f, (float)dt * 6);
    vel.x += (want.x - vel.x) * blend;
    vel.y += (want.y - vel.y) * blend;
    pos.x += vel.x * (float)dt;
    pos.y += vel.y * (float)dt;

    // Объекты
    float pullR = R * 2.4f * (float)st_.pull * (magnetLeft > 0 ? 2.5f : 1.0f);
    float vr = viewRadius();
    float pickR = R * 3.0f * (float)st_.magnet;
    satAngle += (float)dt * 2.2f;
    float satR = R * 0.22f * (float)st_.satSize;

    for (size_t i = 0; i < objs.size(); i++) {
        Obj &o = objs[i];
        if (o.dead) continue;
        float dx = pos.x - o.p.x, dy = pos.y - o.p.y;
        float dd = std::sqrt(dx * dx + dy * dy) + 0.001f;
        if (dd > vr * 1.7f && o.kind != K_CORE) { o.dead = true; continue; }
        bool edible = canEat(o);
        float range = (o.kind == K_CLOCK || o.kind == K_DARK) ? std::max(pullR, pickR) : pullR;
        if (collapseLeft > 0 && edible && dd < vr) range = vr;
        o.pulled = edible && dd < range;
        if (o.pulled) {
            float strength = (dd < pullR ? 1 - dd / range : 0.3f) * 9 * (float)st_.pullStr * (magnetLeft > 0 ? 2.0f : 1.0f);
            if (collapseLeft > 0) strength = std::max(strength, 4.0f * (float)st_.collapsePow);
            float acc = strength * speed;
            o.v.x += dx / dd * acc * (float)dt;
            o.v.y += dy / dd * acc * (float)dt;
            // закрутка по спирали
            o.v.x += -dy / dd * acc * 0.25f * (float)dt;
            o.v.y += dx / dd * acc * 0.25f * (float)dt;
            o.v.x *= 1 - std::min(0.9f, (float)dt * 1.5f);
            o.v.y *= 1 - std::min(0.9f, (float)dt * 1.5f);
        }
        if (o.kind == K_RIVAL && !edible) {
            // соперник крупнее — охотится на нас
            float sp = speed * 0.45f;
            o.v.x += (dx / dd * sp - o.v.x) * (float)dt;
            o.v.y += (dy / dd * sp - o.v.y) * (float)dt;
        }
        o.p.x += o.v.x * (float)dt;
        o.p.y += o.v.y * (float)dt;
        o.rot += o.spin * (float)dt;
        o.hurtCd = std::max(0.0f, o.hurtCd - (float)dt);

        if (o.kind == K_PULSAR) {
            o.beam += (float)dt * 1.1f;
            if (!edible && o.hurtCd <= 0) {
                // луч длиной в 9 радиусов пульсара
                float bl = o.size * 6;
                if (dd < bl) {
                    float ang = std::atan2(-dy, -dx);
                    for (float b : {o.beam, o.beam + 3.14159f}) {
                        float diff = std::remainder(ang - b, 6.28318f);
                        if (std::fabs(diff) * dd < R + o.size * 0.25f) {
                            hurt(1.2, pos);
                            o.hurtCd = 0.6f;
                        }
                    }
                }
            }
        }

        if (o.kind == K_CORE) {
            float contact = R + o.size * 0.85f;
            bool canBite = R * st_.eat >= o.size * kBiteRatio;
            if (dd < contact) {
                if (canBite) {
                    // кусаем Ядро: чем мы крупнее, тем быстрее
                    float rate = kBiteRate * (float)st_.bite * (R * (float)st_.eat / o.size);
                    o.hp -= rate * (float)dt;
                    timeLeft += dt * 0.5;  // пока кусаем, испарение вдвое медленнее
                    int t = 0;
                    for (int k = 0; k < kTierCount; k++)
                        if (kTiers[k].size < R * st_.eat) t = k;
                    double v = tierValue(t) * 4 * dt * (doubleLeft > 0 ? 2 : 1);
                    mass += v; total += v; runMass += v;
                    biteFx_ -= dt;
                    if (biteFx_ <= 0) {
                        biteFx_ = 0.08;
                        float a = std::atan2(dy, dx);
                        Vec at = {o.p.x + std::cos(a) * o.size, o.p.y + std::sin(a) * o.size};
                        events.push_back({EvType::CoreBite, o.hp, 0, at});
                    }
                    if (o.hp <= 0) {
                        o.dead = true;
                        if (universe + 1 < kUniverseCount) {
                            phase = Phase::UniverseClear;
                            events.push_back({EvType::CoreEaten, 0, universe, o.p});
                        } else {
                            phase = Phase::Won;
                            events.push_back({EvType::Win, 0, universe, o.p});
                        }
                        break;
                    }
                }
                float push = (contact * (canBite ? 0.97f : 1.0f) - dd) / dd;
                if (push > 0) {
                    pos.x += dx * push;
                    pos.y += dy * push;
                }
            }
            continue;
        }
        if (edible && dd < R * 0.95f) {
            eat(o, false);
            continue;
        }
        if (!edible) {
            float minD = R + o.size * 0.8f;
            if (dd < minD) {
                if (o.kind == K_ANTI) {
                    hurt(2.0, o.p);
                    o.dead = true;
                    continue;
                }
                if (o.kind == K_RIVAL) {
                    if (o.hurtCd <= 0) { hurt(1.5, pos); o.hurtCd = 0.7f; }
                }
                // упираемся в слишком крупный объект
                float push = (minD - dd) / dd;
                pos.x += dx * push * 0.6f;
                pos.y += dy * push * 0.6f;
                o.p.x -= dx * push * 0.4f;
                o.p.y -= dy * push * 0.4f;
            }
        }
        // Спутники
        if (st_.sats > 0 && o.kind == K_TIER && o.size < satR * 1.4f) {
            for (int s = 0; s < st_.sats; s++) {
                float a = satAngle + s * 6.2832f / st_.sats;
                float sx = pos.x + std::cos(a) * R * 2.0f, sy = pos.y + std::sin(a) * R * 2.0f;
                float ex = sx - o.p.x, ey = sy - o.p.y;
                if (ex * ex + ey * ey < satR * satR * 1.6f) {
                    eat(o, true);
                    break;
                }
            }
        }
    }
    objs.erase(std::remove_if(objs.begin(), objs.end(), [](const Obj &o) { return o.dead; }), objs.end());
    if (phase != Phase::Run) return;  // победа

    clockCd_ = std::max(0.0, clockCd_ - dt);
    darkCd_ = std::max(0.0, darkCd_ - dt);
    powerCd_ = std::max(0.0, powerCd_ - dt);
    goldCd_ = std::max(0.0, goldCd_ - dt);
    spawnAcc_ += dt;
    if (spawnAcc_ > 0.15) {
        spawnAcc_ = 0;
        spawnAround(false);
    }

    if (timeLeft <= 0) {
        if (!loopUsed && st_.loop > 0 && rnd() < st_.loop) {
            loopUsed = true;
            timeLeft = 8;
            events.push_back({EvType::LoopSave, 8, 0, pos});
            return;
        }
        timeLeft = 0;
        double interest = mass * st_.interest;
        mass += interest;
        total += interest;
        phase = Phase::RunEnd;
        events.push_back({EvType::RunEnd, interest});
    }
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
