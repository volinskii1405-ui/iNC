// Симулятор баланса: жадный бот кликает с заданной частотой, ловит события
// и покупает то, что быстрее всего окупается. Держит прохождение около 9 минут.
//   cmake --build build --target balance_sim && ./build/balance_sim 5 1
#include <cstdio>
#include <cstdlib>
#include "Game.hpp"

using namespace bh;

static double value(const Game &g, double cps)
{
    double click = g.baseClick() * g.comboMult() * (1 + g.critChance() * (g.critMult() - 1));
    if (g.hasNode(G_RESONANCE)) click *= 1 + 0.24 * 2;
    double cometEvery = g.hasNode(C_WANDER) ? 26 : 52;
    double comet = std::max(50.0, g.income() * 20 + g.mass * 0.03) * (g.hasNode(C_MAGNETAR) ? 2 : 1);
    double v = g.income() + cps * click + comet / cometEvery;
    if (g.hasNode(G_WAVE)) v += cps / 50 * g.income() * 15;
    if (g.hasNode(C_INTERCEPT)) v += 0.4 * g.income() * 4 / 10;
    return v;
}

int main(int argc, char **argv)
{
    double cps = argc > 1 ? atof(argv[1]) : 5;
    double skill = argc > 2 ? atof(argv[2]) : 1;  // доля пойманных событий
    bool verbose = argc > 3;
    Game g(7);
    srand(3);
    auto chance = [](double p) { return rand() / (double)RAND_MAX < p; };
    double dt = 0.05, acc = 0, meteorAcc = 0, interAcc = 0;
    int last = -1;
    double src[6] = {0};  // доход, клики, кометы, метеоры, перехват, способности
    auto track = [&](int k, auto f) { double t0 = g.total; f(); src[k] += g.total - t0; };
    while (!g.won && g.time < 3600) {
        track(0, [&] { g.update(dt); });
        acc += cps * dt;
        while (acc >= 1) {
            acc -= 1;
            track(1, [&] { if (g.rivalActive && chance(skill)) g.hitRival(); else g.click(); });
        }
        if (g.cometLeft > 0 && g.cometLeft < 6) { if (chance(skill)) { double m0 = g.mass, i0 = g.income(); track(2, [&] { g.catchComet(); }); if (verbose) printf("  comet t=%.0f mass=%s inc=%s -> +%s\n", g.time, fmtNum(m0).c_str(), fmtNum(i0).c_str(), fmtNum(g.mass - m0).c_str()); } else g.cometLeft = 0; }
        if (g.meteorLeft > 0) { meteorAcc += dt * 1.4 * skill; while (meteorAcc >= 1) { meteorAcc -= 1; track(3, [&] { g.catchMeteor(); }); } }
        if (g.hasNode(C_INTERCEPT)) {
            interAcc += dt * 0.4 * skill;
            while (interAcc >= 1) { interAcc -= 1; int best = 0; for (int i = 0; i < kGenCount; i++) if (g.gens[i]) best = i; track(4, [&] { g.intercept(best); }); }
        }
        for (int a = 0; a < AB_COUNT; a++) if (chance(skill * 0.05)) track(5, [&] { g.useAbility((Ability)a); });
        for (int i = 1; i < N_COUNT; i++) if (kNodes[i].branch == Branch::Dark) g.buyNode((NodeId)i);
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
                if (kNodes[i].branch == Branch::Dark || !g.nodeAvailable((NodeId)i)) continue;
                Game t = g; t.mass += kNodes[i].cost; t.buyNode((NodeId)i);
                double c = kNodes[i].cost, r = (value(t, cps) - cur) / c;
                if (r <= 0) r = 1e-9;
                if (r > best) { best = r; bi = i; node = true; bc = c; }
            }
            if (bi < 0 || g.mass < bc) break;
            double togo = (kGoal - g.mass) / std::max(1.0, cur);
            if (1.0 / best > togo) break;
            if (node) g.buyNode((NodeId)bi); else g.buyGen(bi, 1);
            g.events.clear();
        }
        int t = (int)g.time;
        if (verbose && t % 60 == 0 && t != last) {
            last = t;
            printf("t=%3d mass=%-7s inc=%-7s dm=%2.0f gens:", t, fmtNum(g.mass).c_str(), fmtNum(g.income()).c_str(), g.darkTotal);
            for (int i = 0; i < kGenCount; i++) printf(" %d", g.gens[i]);
            printf("  nodes=%d ach=%d  mult:", g.nodesOwned(), g.achCount()); for (int i = 0; i < kGenCount; i++) printf(" %.0f", g.genIncome(i) / std::max(1, g.gens[i]) / kGens[i].rate); printf("\n");
        }
    }
    if (verbose) printf("доход %s  клики %s  кометы %s  метеоры %s  перехват %s  способности %s\n", fmtNum(src[0]).c_str(), fmtNum(src[1]).c_str(), fmtNum(src[2]).c_str(), fmtNum(src[3]).c_str(), fmtNum(src[4]).c_str(), fmtNum(src[5]).c_str());
    printf("cps=%.0f skill=%.1f -> %d:%02d  (кометы %d, соперники %d, узлы %d, ачивки %d, ТМ %.0f)\n", cps, skill,
           (int)g.time / 60, (int)g.time % 60, g.cometsCaught, g.rivalsKilled, g.nodesOwned(), g.achCount(), g.darkTotal);
}
