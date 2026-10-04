/*
 * ГОРИЗОНТ СОБЫТИЙ — инкрементальная игра про чёрную дыру.
 * Терминальный интерфейс: ANSI-цвета, неблокирующий ввод.
 */
#include "game.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <conio.h>
#include <wchar.h>
#include <windows.h>
#else
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#endif

/* ---------- платформа ---------- */

#ifdef _WIN32
static double now_sec(void) { return GetTickCount64() / 1000.0; }
static void sleep_ms(int ms) { Sleep(ms); }

static void term_setup(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    SetConsoleOutputCP(CP_UTF8);
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */);
}
static void term_restore(void) {}

/* Возвращает код символа (Unicode) или -1, если ввода нет. */
static int read_key(void)
{
    if (!_kbhit()) return -1;
    wint_t c = _getwch();
    if (c == 0 || c == 0xE0) { _getwch(); return -1; } /* стрелки и F-клавиши */
    return (int)c;
}
#else
static struct termios orig_tio;
static int tio_saved = 0;

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void sleep_ms(int ms)
{
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static void term_restore(void)
{
    if (tio_saved) tcsetattr(STDIN_FILENO, TCSANOW, &orig_tio);
    fputs("\033[0m\033[?25h\033[?1049l", stdout);
    fflush(stdout);
}

static void on_signal(int sig)
{
    (void)sig;
    term_restore();
    _exit(0);
}

/*
 * Если игру запустили двойным щелчком из файлового менеджера, терминала нет
 * и ничего не видно. Тогда перезапускаемся внутри эмулятора терминала.
 */
static void ensure_terminal(void)
{
    if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) return;
    if (getenv("BH_RELAUNCHED")) return;

    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) return;
    self[n] = '\0';
    setenv("BH_RELAUNCHED", "1", 1);

    static const char *TERMS[][2] = {
        {"x-terminal-emulator", "-e"}, {"gnome-terminal", "--"}, {"konsole", "-e"},
        {"xfce4-terminal", "-x"},      {"mate-terminal", "-x"},  {"lxterminal", "-e"},
        {"tilix", "-e"},               {"terminator", "-x"},     {"kitty", NULL},
        {"alacritty", "-e"},           {"foot", NULL},           {"xterm", "-e"},
    };
    for (size_t i = 0; i < sizeof TERMS / sizeof TERMS[0]; i++) {
        if (TERMS[i][1]) execlp(TERMS[i][0], TERMS[i][0], TERMS[i][1], self, (char *)NULL);
        else execlp(TERMS[i][0], TERMS[i][0], self, (char *)NULL);
    }
    fprintf(stderr, "Не найден эмулятор терминала. Запустите игру из терминала: %s\n", self);
    exit(1);
}

static void term_setup(void)
{
    ensure_terminal();
    if (tcgetattr(STDIN_FILENO, &orig_tio) == 0) {
        struct termios t = orig_tio;
        tio_saved = 1;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
}

/* Читает один символ UTF-8 и возвращает его код, или -1. */
static int read_key(void)
{
    unsigned char b[4];
    if (read(STDIN_FILENO, b, 1) != 1) return -1;
    if (b[0] == 27) { /* ESC-последовательности (стрелки) — глотаем */
        unsigned char junk[8];
        if (read(STDIN_FILENO, junk, sizeof junk) > 0) return -1;
        return 27;
    }
    if (b[0] < 0x80) return b[0];
    int extra = (b[0] & 0xE0) == 0xC0 ? 1 : (b[0] & 0xF0) == 0xE0 ? 2 : 3;
    int cp = b[0] & (0x3F >> extra);
    for (int i = 0; i < extra; i++) {
        if (read(STDIN_FILENO, b + 1, 1) != 1) return -1;
        cp = (cp << 6) | (b[1] & 0x3F);
    }
    return cp;
}
#endif

/* Русская раскладка -> латинская клавиша на том же месте (ЙЦУКЕН -> QWERTY). */
static int normalize_key(int c)
{
    static const struct { int cyr; char lat; } MAP[] = {
        {0x0444, 'a'}, {0x0438, 'b'}, {0x0441, 'c'}, {0x0432, 'd'}, {0x0443, 'e'},
        {0x0430, 'f'}, {0x043F, 'g'}, {0x0440, 'h'}, {0x0448, 'i'}, {0x043E, 'j'},
        {0x0439, 'q'}, {0x044F, 'z'}, {0x043A, 'r'},
    };
    if (c >= 0x0410 && c <= 0x042F) c += 0x20;  /* заглавные кириллические */
    if (c == 0x0401) c = 0x0451;
    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++)
        if (MAP[i].cyr == c) return MAP[i].lat;
    if (c >= 'A' && c <= 'Z') c += 32;
    return c;
}

/* ---------- вывод ---------- */

static char *out;
static size_t out_len, out_cap;

static void put(const char *fmt, ...)
{
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int n = vsnprintf(out + out_len, out_cap - out_len, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        if (out_len + (size_t)n < out_cap) { out_len += n; return; }
        out_cap = (out_cap + n) * 2;
        out = realloc(out, out_cap);
    }
}

/* Длина строки UTF-8 в символах (без ANSI-кодов). */
static int vis_len(const char *s)
{
    int n = 0;
    while (*s) {
        if (*s == '\033') { while (*s && *s != 'm') s++; if (*s) s++; continue; }
        if (((unsigned char)*s & 0xC0) != 0x80) n++;
        s++;
    }
    return n;
}

static void put_pad(const char *s, int width)
{
    put("%s", s);
    for (int i = vis_len(s); i < width; i++) put(" ");
}

static const char *fmt_num(double v, char *buf, size_t n)
{
    static const char *SUF[] = {"", "K", "M", "B", "T", "Qa"};
    int i = 0;
    if (v < 10 && v != floor(v)) { snprintf(buf, n, "%.1f", v); return buf; }
    while (v >= 1000 && i < 5) { v /= 1000; i++; }
    if (i == 0) snprintf(buf, n, "%.0f", floor(v));
    else snprintf(buf, n, v < 10 ? "%.2f%s" : v < 100 ? "%.1f%s" : "%.0f%s", v, SUF[i]);
    return buf;
}

#define C_RESET  "\033[0m"
#define C_BOLD   "\033[1m"
#define C_DIM    "\033[2m"
#define C_RED    "\033[31m"
#define C_GREEN  "\033[32m"
#define C_YELLOW "\033[33m"
#define C_BLUE   "\033[34m"
#define C_MAG    "\033[35m"
#define C_CYAN   "\033[36m"
#define C_WHITE  "\033[97m"
#define C_BYEL   "\033[93m"
#define C_BRED   "\033[91m"

/* ---------- анимация чёрной дыры ---------- */

#define HOLE_W 30
#define HOLE_H 15
#define MAX_PART 96

typedef struct { double ang, r, speed; int alive; } Particle;
static Particle parts[MAX_PART];

static double horizon_radius(const Game *g)
{
    double r = 1.7 + 0.25 * log10(g->total + 1);
    return r > 3.7 ? 3.7 : r;
}

static void spawn_particle(const Game *g)
{
    for (int i = 0; i < MAX_PART; i++) {
        if (parts[i].alive) continue;
        parts[i].alive = 1;
        parts[i].ang = rand() / (double)RAND_MAX * 6.2832;
        parts[i].r = horizon_radius(g) + 5 + rand() % 3;
        parts[i].speed = 3 + rand() % 3;
        return;
    }
}

static void update_particles(const Game *g, double dt)
{
    double rh = horizon_radius(g);
    for (int i = 0; i < MAX_PART; i++) {
        Particle *p = &parts[i];
        if (!p->alive) continue;
        p->r -= p->speed * dt;
        p->ang += dt * 6.0 / (p->r > 0.5 ? p->r : 0.5);
        if (p->r < rh) p->alive = 0;
    }
}

static unsigned hash2(int x, int y)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/* Рисует одну строку картинки шириной HOLE_W. */
static void draw_hole_row(const Game *g, int y, double t, char grid[HOLE_H][HOLE_W])
{
    const double cx = (HOLE_W - 1) / 2.0, cy = (HOLE_H - 1) / 2.0;
    double rh = horizon_radius(g);
    for (int x = 0; x < HOLE_W; x++) {
        double dx = (x - cx) / 2.0, dy = y - cy;
        double r = sqrt(dx * dx + dy * dy);
        /* аккреционный диск — наклонённое кольцо */
        double ex = dx, ey = dy * 3.0;
        double re = sqrt(ex * ex + ey * ey);
        double outer = fmin(rh + 3.6, 7.4);
        int in_disk = re > rh + 0.9 && re < outer;
        int front = dy >= 0;

        if (grid[y][x]) {               /* частица или звезда */
            if (grid[y][x] == 'S') put(C_BOLD C_CYAN "*" C_RESET);
            else put(C_WHITE "." C_RESET);
        } else if (in_disk && (r >= rh || front)) {
            double ang = atan2(ey, ex);
            double v = sin(ang * 2 + re * 0.9 - t * 4.0 / sqrt(re));
            const char *ch = v > 0.6 ? "@" : v > 0.2 ? "*" : v > -0.2 ? "+" : v > -0.6 ? ":" : ".";
            double k = (re - rh) / (outer - rh);
            const char *col = k < 0.25 ? C_BOLD C_WHITE : k < 0.5 ? C_BYEL : k < 0.75 ? C_YELLOW : C_RED;
            put("%s%s" C_RESET, col, ch);
        } else if (r < rh) {
            put(" ");
        } else if (r < rh + 0.9) {      /* фотонная сфера */
            int phase = (int)(t * 8 + atan2(dy, dx) * 4) & 3;
            put("%s" C_RESET, phase == 0 ? C_BOLD C_BYEL "o" : C_BYEL "o");
        } else {
            unsigned h = hash2(x, y);
            if (h % 23 == 0) put(C_DIM "%s" C_RESET, (h >> 8) % 3 ? "." : "+");
            else put(" ");
        }
    }
}

static void build_overlay(const Game *g, char grid[HOLE_H][HOLE_W])
{
    const double cx = (HOLE_W - 1) / 2.0, cy = (HOLE_H - 1) / 2.0;
    memset(grid, 0, HOLE_H * HOLE_W);
    for (int i = 0; i < MAX_PART; i++) {
        if (!parts[i].alive) continue;
        int x = (int)lround(cx + cos(parts[i].ang) * parts[i].r * 2.0);
        int y = (int)lround(cy + sin(parts[i].ang) * parts[i].r);
        if (x >= 0 && x < HOLE_W && y >= 0 && y < HOLE_H) grid[y][x] = '.';
    }
    if (g->star_left > 0) {
        /* блуждающая звезда пролетает по дуге слева направо */
        double k = 1 - g->star_left / 8.0;
        int x = (int)lround(1 + k * (HOLE_W - 3));
        int y = (int)lround(1 + 1.5 * sin(k * 3.1416));
        if (x >= 0 && x < HOLE_W && y >= 0 && y < HOLE_H) grid[y][x] = 'S';
    }
}

/* ---------- экран ---------- */

static void bar(double frac, int width)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int full = (int)(frac * width);
    put(C_MAG);
    for (int i = 0; i < width; i++) put(i < full ? "█" : C_DIM "░" C_RESET C_MAG);
    put(C_RESET);
}

static void render(const Game *g, double t)
{
    char a[32], b[32], c[32];
    char grid[HOLE_H][HOLE_W];
    build_overlay(g, grid);

    out_len = 0;
    put("\033[H");
    put(C_BOLD C_MAG "  ◉ ГОРИЗОНТ СОБЫТИЙ" C_RESET C_DIM "  — инкрементальная игра про чёрную дыру" C_RESET "\033[K\n");

    int sec = (int)g->time;
    double inc = game_income(g);
    for (int y = 0; y < HOLE_H; y++) {
        put(" ");
        draw_hole_row(g, y, t, grid);
        put("  ");
        switch (y) {
        case 0: put(C_BOLD "%s" C_RESET, game_rank(g)); break;
        case 1: put("Масса:  " C_BOLD C_BYEL "%s" C_RESET, fmt_num(g->mass, a, sizeof a)); break;
        case 2: put("Доход:  " C_GREEN "%s/с" C_RESET "   Клик: " C_CYAN "+%s" C_RESET,
                    fmt_num(inc, a, sizeof a), fmt_num(game_click_value(g), b, sizeof b)); break;
        case 3: put(C_DIM "Время %02d:%02d   Поглощено всего %s" C_RESET,
                    sec / 60, sec % 60, fmt_num(g->total, c, sizeof c)); break;
        case 4: put("Коллапс Вселенной: "); bar(g->mass / GOAL_COST, 18);
                put(" %3.0f%%", fmin(100, g->mass / GOAL_COST * 100)); break;
        case 6: put(C_BOLD "ОБЪЕКТЫ" C_RESET C_DIM "         кол-во    цена      доход" C_RESET); break;
        default:
            if (y >= 7 && y < 7 + GEN_COUNT) {
                int i = y - 7;
                const Generator *gen = &g->gens[i];
                if (!gen_visible(g, i)) { put(C_DIM "[%d] ???" C_RESET, i + 1); break; }
                double cost = gen_cost(gen);
                int ok = g->mass >= cost;
                put("%s[%d] " C_RESET, ok ? C_BOLD C_GREEN : C_DIM, i + 1);
                put_pad(gen->name, 17);
                put(" %4d  %s", gen->count, ok ? C_GREEN : C_RED);
                snprintf(a, sizeof a, "%s", fmt_num(cost, b, sizeof b));
                put_pad(a, 8);
                put(C_RESET C_DIM " +%s/с" C_RESET, fmt_num(gen->rate, c, sizeof c));
            }
        }
        put("\033[K\n");
    }

    put("\033[K\n" C_BOLD " УЛУЧШЕНИЯ" C_RESET "\033[K\n");
    int shown = 0;
    for (int i = 0; i < UPG_COUNT && shown < 4; i++) {
        if (!upg_visible(g, i)) continue;
        const Upgrade *up = &g->upgs[i];
        int ok = g->mass >= up->cost;
        put("  %s[%c] " C_RESET, ok ? C_BOLD C_GREEN : C_DIM, 'A' + i);
        put_pad(up->name, 24);
        put(" %s", ok ? C_GREEN : C_RED);
        put_pad(fmt_num(up->cost, a, sizeof a), 8);
        put(C_RESET C_DIM " %s" C_RESET "\033[K\n", up->desc);
        shown++;
    }
    if (shown == 0) put(C_DIM "  пока нечего улучшать — поглощай дальше" C_RESET "\033[K\n");
    for (; shown < 4; shown++) put("\033[K\n");

    put("\033[K\n");
    if (g->mass >= GOAL_COST)
        put(C_BOLD C_BRED "  >>> Масса достаточна! Жми 0, чтобы схлопнуть Вселенную <<<" C_RESET);
    else if (g->star_left > 0)
        put(C_BOLD C_CYAN "  ★ Блуждающая звезда! Жми Z, пока не улетела (%.0f с) — +%s" C_RESET,
            ceil(g->star_left), fmt_num(g->star_reward, a, sizeof a));
    else if (g->msg_left > 0)
        put(C_YELLOW "  %s" C_RESET, g->msg);
    put("\033[K\n\033[K\n");
    put(C_DIM "  ПРОБЕЛ" C_RESET " поглотить   " C_DIM "1-7" C_RESET " объекты   "
        C_DIM "A-J" C_RESET " улучшения   " C_DIM "Z" C_RESET " звезда   "
        C_DIM "0" C_RESET " коллапс   " C_DIM "Q" C_RESET " выход\033[K\n");
    put("\033[J");
    fwrite(out, 1, out_len, stdout);
    fflush(stdout);
}

static void render_win(const Game *g, double t)
{
    static const char *ART[] = {
        "   ██████╗ ██████╗ ██╗     ██╗      █████╗ ██████╗ ███████╗███████╗",
        "  ██╔════╝██╔═══██╗██║     ██║     ██╔══██╗██╔══██╗██╔════╝██╔════╝",
        "  ██║     ██║   ██║██║     ██║     ███████║██████╔╝███████╗█████╗  ",
        "  ██║     ██║   ██║██║     ██║     ██╔══██║██╔═══╝ ╚════██║██╔══╝  ",
        "  ╚██████╗╚██████╔╝███████╗███████╗██║  ██║██║     ███████║███████╗",
        "   ╚═════╝ ╚═════╝ ╚══════╝╚══════╝╚═╝  ╚═╝╚═╝     ╚══════╝╚══════╝",
    };
    static const char *COL[] = {C_BRED, C_BYEL, C_WHITE, C_CYAN, C_MAG};
    char a[32];
    int sec = (int)g->time;
    out_len = 0;
    put("\033[H\033[J\n\n");
    for (int i = 0; i < 6; i++)
        put(C_BOLD "%s%s" C_RESET "\n", COL[((int)(t * 4) + i) % 5], ART[i]);
    put("\n" C_BOLD "  Вселенная схлопнулась в точку бесконечной плотности." C_RESET "\n");
    put("  Ничего не осталось. Даже света. Даже времени.\n\n");
    put("  Время прохождения:   " C_BOLD C_BYEL "%02d:%02d" C_RESET "\n", sec / 60, sec % 60);
    put("  Поглощено массы:     " C_BOLD "%s" C_RESET "\n", fmt_num(g->total, a, sizeof a));
    put("  Нажатий пробела:     %ld\n", g->clicks);
    put("  Пойманных звёзд:     %d\n", g->stars_caught);
    put("\n" C_DIM "  ...а может, из сингулярности родится новый Большой взрыв?" C_RESET "\n\n");
    put("  " C_BOLD "R" C_RESET " — начать заново   " C_BOLD "Q" C_RESET " — выход\n");
    fwrite(out, 1, out_len, stdout);
    fflush(stdout);
}

/* ---------- главный цикл ---------- */

int main(void)
{
    Game g;
    game_init(&g, (unsigned)time(NULL));
    memset(parts, 0, sizeof parts);

    term_setup();
#ifndef _WIN32
    atexit(term_restore);
#endif
    fputs("\033[?1049h\033[?25l\033[2J", stdout);

    double last = now_sec(), start = last;
    double click_tokens = 12;   /* ограничение автоповтора пробела */
    int running = 1;

    while (running) {
        double cur = now_sec();
        double dt = cur - last;
        last = cur;
        if (dt > 0.25) dt = 0.25;

        click_tokens += dt * 12;
        if (click_tokens > 12) click_tokens = 12;

        int key;
        while ((key = read_key()) != -1) {
            key = normalize_key(key);
            if (key == 'q' || key == 27) { running = 0; break; }
            if (g.won) {
                if (key == 'r') { game_init(&g, (unsigned)time(NULL)); memset(parts, 0, sizeof parts); }
                continue;
            }
            if (key == ' ' || key == '\r' || key == '\n') {
                if (click_tokens >= 1) {
                    click_tokens -= 1;
                    game_click(&g);
                    spawn_particle(&g);
                }
            } else if (key >= '1' && key <= '0' + GEN_COUNT) {
                if (game_buy_gen(&g, key - '1'))
                    for (int i = 0; i < 4; i++) spawn_particle(&g);
            } else if (key >= 'a' && key < 'a' + UPG_COUNT) {
                game_buy_upg(&g, key - 'a');
            } else if (key == 'z') {
                game_catch_star(&g);
            } else if (key == '0') {
                game_collapse(&g);
            }
        }

        game_tick(&g, dt);
        update_particles(&g, dt);
        /* фоновое падение материи — тем гуще, чем больше доход */
        if (rand() / (double)RAND_MAX < dt * fmin(6, 0.5 + log10(game_income(&g) + 1)))
            spawn_particle(&g);

        if (g.won) render_win(&g, cur - start);
        else render(&g, cur - start);
        sleep_ms(33);
    }

#ifdef _WIN32
    fputs("\033[0m\033[?25h\033[?1049l", stdout);
#endif
    return 0;
}
