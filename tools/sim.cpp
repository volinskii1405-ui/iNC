// Симулятор баланса: бот водит дыру к самой выгодной съедобной цели, уворачивается
// от опасностей и между заходами покупает самые дешёвые узлы дерева.
//   cmake --build build --target balance_sim && ./build/balance_sim 1.0 v
// Первый аргумент — «мастерство» бота (0..1): насколько точно он выбирает цели.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "Game.hpp"

using namespace bh;

int main(int argc, char **argv)
{
    double skill = argc > 1 ? atof(argv[1]) : 1.0;
    bool verbose = argc > 2 && argv[2][0] == 'v';
    if (argc > 3) kRingCost = atof(argv[3]);
    if (argc > 4) kLevelGrowthAdd = atof(argv[4]);
    if (argc > 5) kGrowthK = (float)atof(argv[5]);
    if (getenv("CAPCOST")) kCapCost = atof(getenv("CAPCOST"));
    const double treeTime = 12 + 4;  // дерево + выбор карт в заходе  // сколько живой игрок проводит в дереве между заходами
    unsigned seed = getenv("SEED") ? atoi(getenv("SEED")) : 11;
    Game g(seed);
    srand(seed);
    double human = 0;
    const double dt = 1.0 / 30;
    while (g.phase != Phase::Won && g.runs < 300) {
        // Покупки: самое дешёвое, что можно купить, пока есть деньги
        for (;;) {
            int best = -1;
            double bc = 1e300;
            for (size_t i = 1; i < kNodes.size(); i++)
                if (g.canBuy((int)i) && g.nodeCost((int)i) < bc) { bc = g.nodeCost((int)i); best = (int)i; }
            for (size_t i = 1; i < kNodes.size(); i++)
                if (kNodes[i].stat == ST_FINAL && g.canBuy((int)i)) best = (int)i;  // игрок берёт Ядро сразу
            if (best < 0) break;
            g.buyNode(best);
        }
        g.events.clear();
        human += treeTime;
        g.startRun();
        Vec target = g.pos;
        double retarget = 0;
        while (g.phase == Phase::Run) {
            retarget -= dt;
            if (retarget <= 0) {
                retarget = 0.25 + (1 - skill) * 0.5;
                double bestScore = -1;
                Vec avoid = {0, 0};
                for (auto &o : g.objs) {
                    float dx = o.p.x - g.pos.x, dy = o.p.y - g.pos.y;
                    float d = std::sqrt(dx * dx + dy * dy) + 1;
                    bool edible = g.canEat(o);
                    if (!edible && (o.kind == K_ANTI || o.kind == K_PULSAR || o.kind == K_RIVAL) && d < g.R * 6) {
                        avoid.x -= dx / d * (g.R * 6 - d);
                        avoid.y -= dy / d * (g.R * 6 - d);
                    }
                    if (o.kind == K_CORE && g.R * g.stats().eat >= o.size * kBiteRatio) { target = o.p; bestScore = 1e300; break; }
                    if (!edible) continue;
                    double v = o.kind == K_TIER ? kTiers[o.tier].value : o.kind == K_CLOCK ? 500 : 50;
                    if (o.kind == K_CLOCK) v = kTiers[std::min(9, std::max(0, (int)std::log2(g.R / 4) + 1))].value * 8;
                    double score = v / (d + g.R * 2) * (0.5 + skill * (rand() / (double)RAND_MAX));
                    if (score > bestScore) { bestScore = score; target = o.p; }
                }
                target.x += avoid.x * skill;
                target.y += avoid.y * skill;
            }
            if (g.choosing) g.chooseCard(rand() % (int)g.choices.size());  // эволюция: случайная карта
            g.update(dt, target, skill > 0.5, true);
            if (getenv("DBG")) for (auto &e : g.events) if (e.type == EvType::CoreEaten || e.type == EvType::Win) printf("  core eaten t=%.1f R=%.0f univ=%d\n", g.runTime, g.R, g.universe);
            g.events.clear();
        }
        if (g.phase == Phase::RunEnd) g.finishRunScreen();
        if (g.phase == Phase::UniverseClear) {
            if (verbose) printf("--- вселенная %d пройдена за %.0f с\n", g.universe + 1, human + g.playTime);
            g.nextUniverse();
        }
        if (verbose)
            printf("[%d] заход %2d  t=%5.0f  R0=%3.0f  Rmax=%4.0f  время=%4.0f  +%-8s  банк=%-8s  ТМ=%2.0f  узлов=%d\n", g.universe + 1, g.runs, human + g.playTime,
                   g.stats().size, g.bestR, g.stats().time, fmtNum(g.runMass).c_str(), fmtNum(g.mass).c_str(), g.dark, [&] {
                       int n = 0;
                       for (size_t i = 1; i < kNodes.size(); i++) n += g.level((int)i);
                       return n;
                   }());
    }
    double t = human + g.playTime;
    printf("skill=%.1f -> %s за %d:%02d (%d заходов, в заходах %d:%02d)\n", skill, g.phase == Phase::Won ? "победа" : "НЕ ПРОШЁЛ",
           (int)t / 60, (int)t % 60, g.runs, (int)g.playTime / 60, (int)g.playTime % 60);
}
