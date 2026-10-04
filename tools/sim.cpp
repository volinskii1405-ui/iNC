// Симулятор баланса: жадный бот кликает с заданной частотой и покупает
// то, что быстрее всего окупается. Помогает держать прохождение ~5 минут.
//   g++ -O2 -std=c++17 -Isrc tools/sim.cpp src/Game.cpp -o sim && ./sim 6 1
#include <cstdio>
#include <cstdlib>
#include "Game.hpp"

using namespace bh;

static double value(const Game &g, double cps)
{
    double click = g.baseClick() * g.comboMult() * (1 + g.critChance() * (g.critMult() - 1));
    double cometEvery = g.hasNode(C_WANDER) ? 26 : 52;
    double comet = std::max(50.0, g.income() * 25 + g.mass * 0.08) * (g.hasNode(C_MAGNETAR) ? 2 : 1);
    double v = g.income() + cps * click + comet / cometEvery;
    if (g.hasNode(G_WAVE)) v += cps / 40 * g.income() * 20;
    return v;
}

int main(int argc, char **argv)
{
    double cps = argc > 1 ? atof(argv[1]) : 6;
    double catchp = argc > 2 ? atof(argv[2]) : 1;
    bool verbose = argc > 3;
    Game g(7);
    srand(3);
    double dt = 0.05, acc = 0;
    int last = -1;
    while (!g.won && g.time < 3600) {
        g.update(dt);
        acc += cps * dt;
        while (acc >= 1) { g.click(); acc -= 1; }
        if (g.cometLeft > 0 && g.cometLeft < 7) {
            if (rand() / (double)RAND_MAX < catchp) g.catchComet(); else g.cometLeft = 0;
        }
        g.events.clear();
        if (g.collapse()) break;
        for (;;) {
            double cur = value(g, cps), best = 0, bc = 0;
            int bi = -1; bool node = false;
            for (int i = 0; i < kGenCount; i++) {
                if (!g.genVisible(i)) continue;
                Game t = g; t.gens[i]++;
                double c = g.genCost(i), r = (value(t, cps) - cur) / c;
                if (r > best) { best = r; bi = i; node = false; bc = c; }
            }
            for (int i = 1; i < N_COUNT; i++) {
                if (!g.nodeAvailable((NodeId)i)) continue;
                Game t = g; t.mass += kNodes[i].cost; t.buyNode((NodeId)i);
                double c = kNodes[i].cost, r = (value(t, cps) - cur) / c;
                if (r <= 0) r = 1e-9;  // узлы без прямой пользы открывают ветку
                if (r > best) { best = r; bi = i; node = true; bc = c; }
            }
            if (bi < 0 || g.mass < bc) break;
            double togo = (kGoal - g.mass) / std::max(1.0, cur);
            if (1.0 / best > togo) break;
            if (node) g.buyNode((NodeId)bi); else g.buyGen(bi);
            g.events.clear();
        }
        int t = (int)g.time;
        if (verbose && t % 30 == 0 && t != last) {
            last = t;
            printf("t=%3d mass=%-8s inc=%-8s gens:", t, fmtNum(g.mass).c_str(), fmtNum(g.income()).c_str());
            for (int i = 0; i < kGenCount; i++) printf(" %d", g.gens[i]);
            printf("  nodes:");
            for (int i = 1; i < N_COUNT; i++) printf("%d", g.hasNode((NodeId)i));
            printf(" ach=%d\n", g.achCount());
        }
    }
    printf("cps=%.0f catch=%.1f -> %d:%02d  (кометы %d, узлы %d, ачивки %d)\n", cps, catchp,
           (int)g.time / 60, (int)g.time % 60, g.cometsCaught, g.nodesOwned(), g.achCount());
}
