/* Shared host model: identical to solver.c's source/twist and ranking. */
#ifndef MODEL_H
#define MODEL_H
#include <stdint.h>
#include <string.h>
enum { CUBIES = 7, NP = 5040, NO = 729, STATES = NP * NO, MOVES = 9 };
typedef struct { uint8_t p[CUBIES], o[CUBIES]; } state_t;
static const uint8_t source[3][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6}, {0, 1, 2, 4, 5, 6, 3}, {0, 2, 5, 3, 1, 4, 6}};
static const uint8_t twist[3][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0}, {0, 0, 0, 1, 2, 1, 2}, {0, 0, 0, 0, 0, 0, 0}};
static const char *const move_names[MOVES] = {"R", "R2", "R'", "B", "B2",
                                              "B'", "D", "D2", "D'"};
static state_t quarter_turn(state_t s, int f)
{
    state_t r;
    for (int i = 0; i < CUBIES; i++) {
        int from = source[f][i];
        r.p[i] = s.p[from];
        r.o[i] = (uint8_t) ((s.o[from] + twist[f][i]) % 3);
    }
    return r;
}
static state_t apply_move(state_t s, int m)
{
    for (int t = 0; t <= m % 3; t++) s = quarter_turn(s, m / 3);
    return s;
}
static uint32_t rank_p(const uint8_t *p)
{
    uint32_t r = 0;
    for (int i = 0; i < CUBIES; i++) {
        int c = 0;
        for (int j = i + 1; j < CUBIES; j++) c += p[j] < p[i];
        r = r * (CUBIES - i) + c;
    }
    return r;
}
static uint32_t rank_o(const uint8_t *o)
{
    uint32_t r = 0;
    for (int i = 0; i < 6; i++) r = r * 3 + o[i];
    return r;
}
static void unrank_p(uint32_t r, uint8_t *p)
{
    uint8_t av[7] = {0, 1, 2, 3, 4, 5, 6};
    uint32_t f = 720;
    for (int i = 0; i < 7; i++) {
        int q = r / f;
        r %= f;
        p[i] = av[q];
        for (int j = q; j + 1 < 7 - i; j++) av[j] = av[j + 1];
        if (i < 6) f /= (6 - i);
    }
}
static void unrank_o(uint32_t r, uint8_t *o)
{
    int s = 0;
    for (int i = 5; i >= 0; i--) { o[i] = r % 3; s += o[i]; r /= 3; }
    o[6] = (3 - s % 3) % 3;
}
static int parse(const char *in, state_t *s)
{
    int sum = 0, seen = 0;
    for (int i = 0; i < 14; i++) {
        int lim = i < 7 ? 7 : 3;
        if (in[i] < '1' || in[i] > '0' + lim) return 0;
        if (i < 7) { s->p[i] = in[i] - '1'; if (seen >> s->p[i] & 1) return 0; seen |= 1 << s->p[i]; }
        else { s->o[i - 7] = in[i] - '1'; sum += s->o[i - 7]; }
    }
    return in[14] == 0 && sum % 3 == 0;
}
#endif