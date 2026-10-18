
/* verify_h1.c
 * Exhaustive H1: verify h(s) <= exact_distance(s)
 *
 * Run on the host computer from the task directory:
 *   gcc -O2 -std=c99 -Wall -Wextra verify_h1.c -o verify_h1
 *   .\verify_h1.exe
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tables.h"

#define CUBIES       7
#define PERMUTATIONS 5040
#define ORIENTATIONS 729
#define STATES       (PERMUTATIONS * ORIENTATIONS)

typedef struct {
    uint8_t p[CUBIES];
    uint8_t o[CUBIES];
} state_t;

/* Cube model from the repository's reference solver. */
static const uint8_t source[3][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6}
};

static const uint8_t twist[3][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0}
};

/* Apply one clockwise quarter-turn. */
static state_t quarter_turn(const state_t *s, uint32_t face)
{
    state_t next;

    for (uint32_t i = 0; i < CUBIES; ++i) {
        uint32_t from = source[face][i];
        next.p[i] = s->p[from];

        uint32_t v = s->o[from] + twist[face][i];
        next.o[i] = (uint8_t)(v % 3);
    }

    return next;
}

/* Rank a permutation in [0, 5039]. */
static uint32_t rank_perm(const uint8_t *p)
{
    uint32_t rank = 0;

    for (uint32_t i = 0; i < CUBIES; ++i) {
        uint32_t smaller = 0;

        for (uint32_t j = i + 1; j < CUBIES; ++j)
            if (p[j] < p[i])
                ++smaller;

        rank = rank * (CUBIES - i) + smaller;
    }

    return rank;
}

/* Rank the first six twists in base 3. */
static uint32_t rank_orient(const uint8_t *o)
{
    uint32_t rank = 0;

    for (uint32_t i = 0; i < 6; ++i)
        rank = rank * 3 + o[i];

    return rank;
}

static uint32_t rank_state(const state_t *s)
{
    return rank_perm(s->p) * ORIENTATIONS + rank_orient(s->o);
}

/* Recover a valid state from its dense rank. */
static void unrank_state(uint32_t rank, state_t *s)
{
    uint32_t p = rank / ORIENTATIONS;
    uint32_t o = rank % ORIENTATIONS;
    uint32_t factorial = 720;
    uint8_t available[CUBIES] = {0, 1, 2, 3, 4, 5, 6};
    uint32_t sum = 0;

    for (uint32_t i = 0; i < CUBIES; ++i) {
        uint32_t q = p / factorial;
        p %= factorial;

        s->p[i] = available[q];

        for (uint32_t j = q; j + 1 < CUBIES - i; ++j)
            available[j] = available[j + 1];

        if (i < 5)
            factorial /= 6 - i;
    }

    for (int i = 5; i >= 0; --i) {
        s->o[i] = (uint8_t)(o % 3);
        sum += s->o[i];
        o /= 3;
    }

    s->o[6] = (uint8_t)((3 - sum % 3) % 3);
}

/*
 * Build exact shortest distances by BFS.
 * Each of the 9 moves is one edge in the half-turn metric:
 * R, R2, R', B, B2, B', D, D2, D'.
 */
static uint8_t *build_bfs(uint32_t *reached, uint32_t *diameter)
{
    uint8_t *dist = malloc(STATES * sizeof(*dist));
    uint32_t *queue = malloc((size_t)STATES * sizeof(*queue));

    if (!dist || !queue) {
        fprintf(stderr, "Memory allocation failed.\n");
        free(dist);
        free(queue);
        return NULL;
    }

    uint16_t perm_next[3][PERMUTATIONS];
    uint16_t orient_next[3][ORIENTATIONS];
    state_t s;

    /* Build permutation transitions directly from the cube model. */
    for (uint32_t p = 0; p < PERMUTATIONS; ++p) {
        unrank_state(p * ORIENTATIONS, &s);

        for (uint32_t f = 0; f < 3; ++f) {
            state_t next = quarter_turn(&s, f);
            perm_next[f][p] = (uint16_t)rank_perm(next.p);
        }
    }

    /* Build orientation transitions directly from the cube model. */
    for (uint32_t o = 0; o < ORIENTATIONS; ++o) {
        unrank_state(o, &s);

        for (uint32_t f = 0; f < 3; ++f) {
            state_t next = quarter_turn(&s, f);
            orient_next[f][o] = (uint16_t)rank_orient(next.o);
        }
    }

    memset(dist, 255, STATES);

    uint32_t head = 0, tail = 1;
    queue[0] = 0;
    dist[0] = 0;
    *diameter = 0;

    while (head < tail) {
        uint32_t here = queue[head++];
        uint32_t p = here / ORIENTATIONS;
        uint32_t o = here % ORIENTATIONS;

        for (uint32_t f = 0; f < 3; ++f) {
            uint32_t np = p, no = o;

            for (uint32_t turn = 0; turn < 3; ++turn) {
                np = perm_next[f][np];
                no = orient_next[f][no];

                uint32_t there = np * ORIENTATIONS + no;

                if (dist[there] == UINT8_MAX) {
                    dist[there] = (uint8_t)(dist[here] + 1);
                    queue[tail++] = there;

                    if (dist[there] > *diameter)
                        *diameter = dist[there];
                }
            }
        }
    }

    free(queue);
    *reached = tail;

    if (tail != STATES) {
        fprintf(stderr, "BFS incomplete: reached %u of %u states.\n",
                (unsigned)tail, (unsigned)STATES);
        free(dist);
        return NULL;
    }

    return dist;
}

/*
 * Evaluate the same heuristic used by solver_stage2.c.
 *
 * PT entry format: (next_permutation_rank << 4) | dP.
 * Four R quarter-turn lookups return to the original permutation
 * and expose its permutation-distance nibble.
 */

static uint32_t heuristic(uint32_t rank)
{
    uint32_t p = rank / ORIENTATIONS;
    uint32_t o = rank % ORIENTATIONS;

    /*
     * PT entries encode:
     * (next permutation rank << 6) | dP
     *
     * Four R quarter-turns return to the original permutation.
     * Use direct permutation ranks when indexing PT here.
     */
    uint32_t w = PT[0][p];
    w = PT[0][w >> 6];
    w = PT[0][w >> 6];
    w = PT[0][w >> 6];

    uint32_t dp = w & 15U;

    /* Locate cubies 3 and 6, matching solver_stage2.c. */
    state_t s;
    unrank_state(rank, &s);

    uint32_t pos3 = 0;
    uint32_t pos6 = 0;

    for (uint32_t i = 0; i < CUBIES; ++i) {
        if (s.p[i] == 3)
            pos3 = i;

        if (s.p[i] == 6)
            pos6 = i;
    }

    uint32_t x = (pos3 << 3) | pos6;

    /* o is the orientation rank; solver uses o2 = 2 * o. */
    uint32_t dx = DX[(o << 6) | x];

    return dp > dx ? dp : dx;
}

int main(void)
{
    uint32_t reached = 0, diameter = 0;

    puts("Building independent BFS distances...");
    uint8_t *dist = build_bfs(&reached, &diameter);

    if (!dist)
        return 1;

    printf("BFS reached: %u states\n", (unsigned)reached);
    printf("BFS diameter: %u\n", (unsigned)diameter);

    uint32_t checked = 0;
    uint32_t unvisited = 0;
    uint32_t violations = 0;
    uint32_t equal = 0;
    uint32_t max_h = 0;

    for (uint32_t rank = 0; rank < STATES; ++rank) {
        if (dist[rank] == UINT8_MAX) {
            ++unvisited;
            continue;
        }

        uint32_t h = heuristic(rank);
        uint32_t exact = dist[rank];
        ++checked;

        if (h > max_h)
            max_h = h;

        if (h > exact) {
            if (violations < 20) {
                printf("VIOLATION: rank=%u h=%u exact=%u\n",
                       (unsigned)rank, (unsigned)h, (unsigned)exact);
            }
            ++violations;
        }

        if (h == exact)
            ++equal;
    }

    puts("\n--- H1 RESULTS ---");
    printf("States checked: %u\n", (unsigned)checked);
    printf("Unvisited states: %u\n", (unsigned)unvisited);
    printf("Maximum heuristic: %u\n", (unsigned)max_h);
    printf("States where h equals exact distance: %u\n",
           (unsigned)equal);
    printf("Overestimation violations: %u\n",
           (unsigned)violations);

    int pass = checked == STATES &&
               unvisited == 0 &&
               violations == 0 &&
               diameter == 11;

    puts(pass ? "H1: PASS" : "H1: FAIL");

    free(dist);
    return pass ? 0 : 1;
}
