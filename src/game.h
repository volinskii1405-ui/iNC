#ifndef BH_GAME_H
#define BH_GAME_H

#define GEN_COUNT 7
#define UPG_COUNT 10

typedef struct {
    const char *name;
    const char *desc;
    double base_cost;
    double rate;        /* масса в секунду за единицу */
    int count;
} Generator;

typedef enum { UPG_CLICK, UPG_GEN, UPG_ALL, UPG_JETS } UpgKind;

typedef struct {
    const char *name;
    const char *desc;
    double cost;
    UpgKind kind;
    int target;         /* индекс генератора для UPG_GEN */
    double value;
    int bought;
} Upgrade;

typedef struct {
    double mass;        /* текущая масса (валюта) */
    double total;       /* всего поглощено */
    double time;        /* секунд с начала */
    long clicks;
    Generator gens[GEN_COUNT];
    Upgrade upgs[UPG_COUNT];
    /* блуждающая звезда */
    double star_timer;  /* до появления */
    double star_left;   /* сколько ещё видна (0 = нет) */
    double star_reward;
    int stars_caught;
    /* сообщение в строке статуса */
    char msg[160];
    double msg_left;
    int won;
} Game;

extern const double GOAL_COST;

void game_init(Game *g, unsigned seed);
void game_tick(Game *g, double dt);
double game_income(const Game *g);
double game_click_value(const Game *g);
double gen_cost(const Generator *gen);
int game_click(Game *g);
int game_buy_gen(Game *g, int i);
int game_buy_upg(Game *g, int i);
int game_catch_star(Game *g);
int game_collapse(Game *g);
int upg_visible(const Game *g, int i);
int gen_visible(const Game *g, int i);
const char *game_rank(const Game *g);

#endif
