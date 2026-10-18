
/* h3_check.c
 * Exhaustive H3: verify every returned solution is optimal.
 *
 * Build:
 *   gcc -O2 -std=c99 -Wall -Wextra h3_check.c -o h3_check.exe
 * Run:
 *   .\h3_check.exe
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define main solver_stage2_cli_main
#include "solver_stage2.c"
#undef main
#define H3_STATES (5040u * 729u)

typedef struct {
    uint8_t p[7];
    uint8_t o[7];
} h3_state_t;

static const uint8_t h3_source[3][7] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6}
};

static const uint8_t h3_twist[3][7] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0}
};

static h3_state_t h3_turn(const h3_state_t *s, unsigned f)
{
    h3_state_t n;

    for (unsigned i = 0; i < 7; i++) {
        unsigned j = h3_source[f][i];
        n.p[i] = s->p[j];
        n.o[i] = (uint8_t)((s->o[j] + h3_twist[f][i]) % 3);
    }

    return n;
}

static unsigned h3_rank_perm(const uint8_t *p)
{
    unsigned rank = 0;

    for (unsigned i = 0; i < 7; i++) {
        unsigned smaller = 0;

        for (unsigned j = i + 1; j < 7; j++)
            if (p[j] < p[i])
                smaller++;

        rank = rank * (7 - i) + smaller;
    }

    return rank;
}

static unsigned h3_rank_ori(const uint8_t *o)
{
    unsigned rank = 0;

    for (unsigned i = 0; i < 6; i++)
        rank = rank * 3 + o[i];

    return rank;
}

static void h3_unrank(unsigned rank, h3_state_t *s)
{
    unsigned p = rank / 729u;
    unsigned o = rank % 729u;
    unsigned factorial = 720;
    uint8_t available[7] = {0, 1, 2, 3, 4, 5, 6};
    unsigned sum = 0;

    for (unsigned i = 0; i < 7; i++) {
        unsigned q = p / factorial;
        p %= factorial;
        s->p[i] = available[q];

        for (unsigned j = q; j + 1 < 7 - i; j++)
            available[j] = available[j + 1];

        if (i < 5)
            factorial /= 6 - i;
    }

    for (int i = 5; i >= 0; i--) {
        s->o[i] = (uint8_t)(o % 3);
        sum += s->o[i];
        o /= 3;
    }

    s->o[6] = (uint8_t)((3 - sum % 3) % 3);
}

static uint8_t *h3_build_bfs(void)
{
    uint8_t *dist = malloc(H3_STATES);
    uint32_t *queue =
        malloc((size_t)H3_STATES * sizeof(*queue));

    if (!dist || !queue) {
        fprintf(stderr, "BFS allocation failed\n");
        free(dist);
        free(queue);
        return NULL;
    }

    uint16_t pn[3][5040];
    uint16_t on[3][729];
    h3_state_t s;

    for (unsigned p = 0; p < 5040; p++) {
        h3_unrank(p * 729u, &s);

        for (unsigned f = 0; f < 3; f++) {
            h3_state_t n = h3_turn(&s, f);
            pn[f][p] = (uint16_t)h3_rank_perm(n.p);
        }
    }

    for (unsigned o = 0; o < 729; o++) {
        h3_unrank(o, &s);

        for (unsigned f = 0; f < 3; f++) {
            h3_state_t n = h3_turn(&s, f);
            on[f][o] = (uint16_t)h3_rank_ori(n.o);
        }
    }

    memset(dist, 255, H3_STATES);
    unsigned head = 0, tail = 1;
    queue[0] = 0;
    dist[0] = 0;

    while (head < tail) {
        unsigned here = queue[head++];
        unsigned p = here / 729u;
        unsigned o = here % 729u;

        for (unsigned f = 0; f < 3; f++) {
            unsigned np = p, no = o;

            for (unsigned t = 0; t < 3; t++) {
                np = pn[f][np];
                no = on[f][no];

                unsigned next = np * 729u + no;

                if (dist[next] == 255) {
                    dist[next] = (uint8_t)(dist[here] + 1);
                    queue[tail++] = next;
                }
            }
        }
    }

    free(queue);

    if (tail != H3_STATES) {
        fprintf(stderr, "BFS reached %u of %u states\n",
                tail, H3_STATES);
        free(dist);
        return NULL;
    }

    printf("BFS reference: %u states reached\n", tail);
    return dist;
}

int main(void)
{
    puts("Building exact BFS distances...");
    uint8_t *dist = h3_build_bfs();

    if (!dist)
        return 1;

    unsigned checked = 0;
    unsigned invalid = 0;
    unsigned nonoptimal = 0;
    unsigned failed_search = 0;
    unsigned max_distance = 0;

    for (unsigned rank = 0; rank < H3_STATES; rank++) {
        h3_state_t s;
        uint8_t path[16];
        uint8_t p[7], o[7];

        h3_unrank(rank, &s);

        unsigned exact = dist[rank];
        if (exact == 255) {
            invalid++;
            continue;
        }

        if (exact > max_distance)
            max_distance = exact;

        uint32_t p4, o2, x;
        coords(s.p, s.o, &p4, &o2, &x);

        int n = ida(p4, o2, x, path);

        if (n < 0 || n > 11) {
            failed_search++;
            if (failed_search <= 10)
                printf("Search failed: rank=%u exact=%u\n",
                       rank, exact);
            checked++;
            continue;
        }

        memcpy(p, s.p, 7);
        memcpy(o, s.o, 7);

        if (!verify_path(p, o, path, n)) {
            invalid++;
            if (invalid <= 10)
                printf("Invalid path: rank=%u length=%d\n",
                       rank, n);
        }

        if ((unsigned)n != exact) {
            nonoptimal++;
            if (nonoptimal <= 10)
                printf("Non-optimal: rank=%u got=%d exact=%u\n",
                       rank, n, exact);
        }

        checked++;
    }

    puts("\n--- H3 RESULTS ---");
    printf("States checked: %u\n", checked);
    printf("BFS diameter: %u\n", max_distance);
    printf("Invalid paths: %u\n", invalid);
    printf("Search failures: %u\n", failed_search);
    printf("Non-optimal solutions: %u\n", nonoptimal);

    int pass = checked == H3_STATES &&
               max_distance == 11 &&
               invalid == 0 &&
               failed_search == 0 &&
               nonoptimal == 0;

    puts(pass ? "H3: PASS" : "H3: FAIL");

    free(dist);
    return pass ? 0 : 1;
}
