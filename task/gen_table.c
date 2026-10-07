/* gen_tables.c - host generator for every read-only table the RV32I solver
 * links. Writes tables.s (Ripes assembler) and tables.h (C reference build).
 *
 *   PT  3 x 5040 words     (p' << 6) | dP[p']      quarter turn on perm rank
 *   OT  3 x  729 halfwords  2 * o'                  quarter turn, byte offset
 *   XT  3 x   64 bytes      x'                      positions of cubies 3, 6
 *   DX  729 x 64 bytes      exact distance of the (o, x) abstraction
 *   FL  24 facelets         renderer: position, normal slot, pixel x, y
 *   HC  8 x 3 bytes         renderer: home color of each cubie sticker
 *
 * x = 8 * pos(cubie 3) + pos(cubie 6); 22 of the 64 slots are unreachable
 * and hold 0xFF, which no search ever indexes.
 *
 * The sticker model is derived from 3D rotations and checked against the
 * repository's source[][] / twist[][] before anything is written.
 */
#include <stdio.h>
#include <stdlib.h>
#include "model.h"
#include <stdint.h>

enum { NX = 64, CA = 3, CB = 6 };
static uint16_t PTr[3][NP], OTr[3][NO];
static uint8_t XTr[3][NX], dP[NP], DX[NO * NX];

static int xcoord(const uint8_t *p)
{
    int pa = -1, pb = -1;
    for (int i = 0; i < CUBIES; i++) {
        if (p[i] == CA) pa = i;
        if (p[i] == CB) pb = i;
    }
    return pa * 8 + pb;
}

static void build(void)
{
    state_t s;
    for (uint32_t r = 0; r < NP; r++) {
        unrank_p(r, s.p);
        memset(s.o, 0, 7);
        for (int f = 0; f < 3; f++) {
            state_t n = quarter_turn(s, f);
            PTr[f][r] = (uint16_t) rank_p(n.p);
        }
    }
    for (uint32_t r = 0; r < NO; r++) {
        for (int i = 0; i < 7; i++) s.p[i] = (uint8_t) i;
        unrank_o(r, s.o);
        for (int f = 0; f < 3; f++) {
            state_t n = quarter_turn(s, f);
            OTr[f][r] = (uint16_t) rank_o(n.o);
        }
    }
    /* XT: a cubie at position src moves to the i with source[f][i] == src */
    for (int x = 0; x < NX; x++)
        for (int f = 0; f < 3; f++) {
            int a = x >> 3, b = x & 7, na = 0, nb = 0;
            for (int i = 0; i < 7; i++) {
                if (source[f][i] == a) na = i;
                if (source[f][i] == b) nb = i;
            }
            XTr[f][x] = (uint8_t) ((a < 7 && b < 7 && a != b) ? na * 8 + nb : x);
        }
    /* level-by-level BFS on each abstraction, all 9 moves */
    memset(dP, 255, NP);
    dP[0] = 0;
    for (int d = 0, ch = 1; ch; d++) {
        ch = 0;
        for (uint32_t r = 0; r < NP; r++)
            if (dP[r] == d)
                for (int f = 0; f < 3; f++)
                    for (uint32_t n = r, k = 0; k < 3; k++)
                        if (dP[n = PTr[f][n]] == 255) dP[n] = d + 1, ch = 1;
    }
    memset(DX, 255, sizeof DX);
    for (int i = 0; i < 7; i++) s.p[i] = (uint8_t) i;
    DX[xcoord(s.p)] = 0; /* o = 0 */
    for (int d = 0, ch = 1; ch; d++) {
        ch = 0;
        for (int r = 0; r < NO * NX; r++)
            if (DX[r] == d)
                for (int f = 0; f < 3; f++) {
                    int o = r >> 6, x = r & 63;
                    for (int k = 0; k < 3; k++) {
                        o = OTr[f][o];
                        x = XTr[f][x];
                        if (DX[o << 6 | x] == 255) DX[o << 6 | x] = d + 1, ch = 1;
                    }
                }
    }
}

/* ---- sticker geometry ------------------------------------------------- */
/* x right, y up, z toward the viewer (front). Face ids double as colors. */
enum { U, R, F, D, L, B };
static const int normal[6][3] = {{0, 1, 0}, {1, 0, 0}, {0, 0, 1},
                                 {0, -1, 0}, {-1, 0, 0}, {0, 0, -1}};
static const int corner[8][3] = {{-1, 1, 1},  {1, 1, 1},   {1, -1, 1},
                                 {-1, -1, 1}, {1, 1, -1},  {1, -1, -1},
                                 {-1, -1, -1}, {-1, 1, -1}};
static const int turn_axis[3] = {R, B, D};
static int slot_face[8][3]; /* faces around a corner, U/D first, then cw */

static int face_of(const int *n)
{
    for (int f = 0; f < 6; f++)
        if (!memcmp(normal[f], n, sizeof normal[f])) return f;
    return -1;
}
static int corner_of(const int *v)
{
    for (int c = 0; c < 8; c++)
        if (!memcmp(corner[c], v, sizeof corner[c])) return c;
    return -1;
}
/* rotate u by -90 degrees about outward axis a: -(a x u) + a (a . u) */
static void rot(const int *a, const int *u, int *out)
{
    int cx = a[1] * u[2] - a[2] * u[1], cy = a[2] * u[0] - a[0] * u[2],
        cz = a[0] * u[1] - a[1] * u[0], dot = a[0] * u[0] + a[1] * u[1] + a[2] * u[2];
    out[0] = -cx + a[0] * dot;
    out[1] = -cy + a[1] * dot;
    out[2] = -cz + a[2] * dot;
}
static void corner_slots(int sense)
{
    for (int c = 0; c < 8; c++) {
        const int *v = corner[c];
        int n[3][3] = {{0, v[1], 0}, {v[0], 0, 0}, {0, 0, v[2]}};
        /* order the two side faces so that (n0 x n1) . v has sign `sense` */
        int a[3] = {n[0][1] * n[1][2] - n[0][2] * n[1][1],
                    n[0][2] * n[1][0] - n[0][0] * n[1][2],
                    n[0][0] * n[1][1] - n[0][1] * n[1][0]};
        int s = a[0] * v[0] + a[1] * v[1] + a[2] * v[2];
        int i1 = (s * sense > 0) ? 1 : 2, i2 = 3 - i1;
        slot_face[c][0] = face_of(n[0]);
        slot_face[c][1] = face_of(n[i1]);
        slot_face[c][2] = face_of(n[i2]);
    }
}
/* sticker[c][f] = color seen on face f at corner c (or -1) */
static void physical_turn(int sticker[8][6], int t)
{
    int out[8][6];
    memcpy(out, sticker, sizeof out);
    const int *a = normal[turn_axis[t]];
    for (int c = 0; c < 8; c++) {
        const int *v = corner[c];
        if (v[0] * a[0] + v[1] * a[1] + v[2] * a[2] <= 0) continue;
        int nv[3];
        rot(a, v, nv);
        int nc = corner_of(nv);
        for (int f = 0; f < 6; f++) out[nc][f] = -1;
    }
    for (int c = 0; c < 8; c++) {
        const int *v = corner[c];
        if (v[0] * a[0] + v[1] * a[1] + v[2] * a[2] <= 0) continue;
        int nv[3];
        rot(a, v, nv);
        int nc = corner_of(nv);
        for (int f = 0; f < 6; f++)
            if (sticker[c][f] >= 0) {
                int nn[3];
                rot(a, normal[f], nn);
                out[nc][face_of(nn)] = sticker[c][f];
            }
    }
    memcpy(sticker, out, sizeof out);
}
/* read (p, o) back from stickers; cubie identity = its home corner */
static int read_state(int sticker[8][6], state_t *s)
{
    for (int c = 1; c < 8; c++) {
        int home = -1, ref = -1;
        for (int k = 0; k < 3; k++) {
            int col = sticker[c][slot_face[c][k]];
            if (col == U || col == D) ref = k;
        }
        for (int h = 1; h < 8; h++) {
            int match = 1;
            for (int k = 0; k < 3; k++) {
                int col = slot_face[h][k], found = 0;
                for (int j = 0; j < 3; j++)
                    if (sticker[c][slot_face[c][j]] == col) found = 1;
                match &= found;
            }
            if (match) home = h;
        }
        if (home < 0 || ref < 0) return 0;
        s->p[c - 1] = (uint8_t) (home - 1);
        s->o[c - 1] = (uint8_t) ref;
    }
    return 1;
}
static int geometry_matches(void)
{
    srand(12345);
    for (int trial = 0; trial < 2000; trial++) {
        int st[8][6];
        for (int c = 0; c < 8; c++)
            for (int f = 0; f < 6; f++) st[c][f] = -1;
        for (int c = 0; c < 8; c++)
            for (int k = 0; k < 3; k++) st[c][slot_face[c][k]] = slot_face[c][k];
        state_t m = {{0, 1, 2, 3, 4, 5, 6}, {0}};
        for (int step = 0; step < 25; step++) {
            int t = rand() % 3;
            physical_turn(st, t);
            m = quarter_turn(m, t);
            state_t got;
            if (!read_state(st, &got) || memcmp(&got, &m, sizeof m)) return 0;
        }
    }
    return 1;
}

/* ---- net layout ------------------------------------------------------- */
/* report.md section 2: exterior unfold, faces as seen from outside */
static const int net_pos[6][4] = {
    [U] = {7, 4, 0, 1}, [L] = {7, 0, 6, 3}, [F] = {0, 1, 3, 2},
    [R] = {1, 4, 2, 5}, [B] = {4, 7, 5, 6}, [D] = {3, 2, 6, 5}};
static const int net_col[6] = {[U] = 1, [L] = 0, [F] = 1, [R] = 2, [B] = 3, [D] = 1};
static const int net_row[6] = {[U] = 0, [L] = 1, [F] = 1, [R] = 1, [B] = 1, [D] = 2};
enum { FW = 4, FH = 3 }; /* pixels per facelet */

int main(void)
{
    build();
    int sense;
    for (sense = 1; sense >= -1; sense -= 2) {
        corner_slots(sense);
        if (geometry_matches()) break;
    }
    if (sense < -1) {
        fputs("geometry: no sticker convention reproduces source/twist\n", stderr);
        return 1;
    }
    fprintf(stderr, "geometry: sense %d reproduces source/twist on 50000 turns\n",
            sense);

    int maxP = 0, maxX = 0;
    for (int i = 0; i < NP; i++) maxP = dP[i] > maxP ? dP[i] : maxP;
    for (int i = 0; i < NO * NX; i++)
        if (DX[i] != 255) maxX = DX[i] > maxX ? DX[i] : maxX;
    fprintf(stderr, "dP max %d, DX max %d\n", maxP, maxX);

    FILE *s = fopen("tables.s", "w"), *h = fopen("tables.h", "w");
    if (!s || !h) return 1;
    fprintf(s, "# generated by gen_tables.c - do not edit\n.data\n");
    fprintf(h, "/* generated by gen_tables.c - do not edit */\n#include <stdint.h>\n");
    /* largest-alignment first so no padding is inserted */
    fprintf(s, ".align 2\nPT:\n");
    fprintf(h, "static const uint32_t PT[3][%d] = {\n", NP);
    for (int f = 0; f < 3; f++) {
        fprintf(h, "{");
        for (int r = 0; r < NP; r++) {
            uint32_t w = (uint32_t) PTr[f][r] << 6 | dP[PTr[f][r]];
            if (r % 12 == 0) fprintf(s, "\n.word ");
            fprintf(s, "%s%u", r % 12 ? "," : "", w);
            fprintf(h, "%u,", w);
        }
        fprintf(h, "},\n");
    }
    fprintf(h, "};\n");
    fprintf(s, "\nOT:");
    fprintf(h, "static const uint16_t OT[3][%d] = {\n", NO);
    for (int f = 0; f < 3; f++) {
        fprintf(h, "{");
        for (int r = 0; r < NO; r++) {
            if (r % 16 == 0) fprintf(s, "\n.half ");
            fprintf(s, "%s%u", r % 16 ? "," : "", 2u * OTr[f][r]);
            fprintf(h, "%u,", 2u * OTr[f][r]);
        }
        fprintf(h, "},\n");
    }
    fprintf(h, "};\n");
    fprintf(s, "\nXT:");
    fprintf(h, "static const uint8_t XT[3][%d] = {\n", NX);
    for (int f = 0; f < 3; f++) {
        fprintf(h, "{");
        for (int r = 0; r < NX; r++) {
            if (r % 16 == 0) fprintf(s, "\n.byte ");
            fprintf(s, "%s%u", r % 16 ? "," : "", XTr[f][r]);
            fprintf(h, "%u,", XTr[f][r]);
        }
        fprintf(h, "},\n");
    }
    fprintf(h, "};\n");
    fprintf(s, "\nDX:");
    fprintf(h, "static const uint8_t DX[%d] = {\n", NO * NX);
    for (int r = 0; r < NO * NX; r++) {
        if (r % 32 == 0) fprintf(s, "\n.byte ");
        fprintf(s, "%s%u", r % 32 ? "," : "", DX[r]);
        fprintf(h, "%u,%s", DX[r], r % 32 == 31 ? "\n" : "");
    }
    fprintf(h, "};\n");

    /* renderer tables: FL = {position, slot, px, py} per facelet */
    fprintf(s, "\nFL:");
    fprintf(h, "static const uint8_t FL[24][4] = {\n");
    for (int f = 0; f < 6; f++)
        for (int k = 0; k < 4; k++) {
            int pos = net_pos[f][k], slot = -1;
            for (int j = 0; j < 3; j++)
                if (slot_face[pos][j] == f) slot = j;
            int px = net_col[f] * (2 * FW + 1) + (k & 1) * FW;
            int py = net_row[f] * (2 * FH + 1) + (k >> 1) * FH;
            fprintf(s, "\n.byte %d,%d,%d,%d", pos, slot, px, py);
            fprintf(h, "{%d,%d,%d,%d},\n", pos, slot, px, py);
        }
    fprintf(h, "};\n");
    fprintf(s, "\nHC:");
    fprintf(h, "static const uint8_t HC[8][4] = {\n");
    for (int c = 0; c < 8; c++) {
        fprintf(s, "\n.byte %d,%d,%d,0", slot_face[c][0], slot_face[c][1],
                slot_face[c][2]);
        fprintf(h, "{%d,%d,%d,0},\n", slot_face[c][0], slot_face[c][1],
                slot_face[c][2]);
    }
    fprintf(h, "};\n");
    int total = 3 * NP * 4 + 3 * NO * 2 + 3 * NX + NO * NX + 96 + 32;
    if (total & 3) fprintf(s, "\n.zero %d   # pad: solver.s data starts word-aligned", 4 - (total & 3));
    fprintf(s, "\n");
    fclose(s);
    fclose(h);
    fprintf(stderr, "static data: PT %d + OT %d + XT %d + DX %d + FL 96 + HC 32\n",
            3 * NP * 4, 3 * NO * 2, 3 * NX, NO * NX);
    return 0;
}