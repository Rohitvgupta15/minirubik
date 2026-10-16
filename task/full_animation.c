/* cube_ripes.c - optimal 2x2x2 solver (IDA*) + LED-matrix animation for Ripes.
 *
 * What the RV32I build does, in order:
 *   1. parses the 14-digit state given at compile time (-DSTATE=...)
 *   2. draws the scrambled cube as an unfolded net on the LED matrix
 *   3. runs the IDA* search from solver_stage2.c
 *   4. replays the solver's own path on the LED matrix, one redraw per
 *      quarter turn
 *   5. confirms the cube on screen is solved and exits with the length
 *
 * Exit code: solution length 0..11, 254 if a check fails, 255 bad input.
 *
 * Builds (tables.h from gen_tables.c must be in the same directory):
 *   host:   cc -O2 -std=c99 cube_ripes.c -o cube_ripes
 *           ./cube_ripes 21345671111111       (solution only)
 *           ./cube_ripes -v 21345671111111    (also prints every frame)
 *   RV32I:  riscv-none-elf-gcc -O2 -march=rv32i -mabi=ilp32 -ffreestanding \
 *               -nostdlib -fno-builtin -DTARGET -DSTATE=21345671111111 \
 *               -T ref.ld cube_ripes.c -o cube_ripes.elf
 *   Options: -DRENDER=0   compile the renderer out (for --iret measurement)
 *            -DDELAY=n    busy-wait count per frame, 2 instructions each
 *                         (default 100000 = about 0.2 s on RV32_ISS;
 *                         0 = no pauses; use ~1000 on RV32_5S)
 *            -DLED_BASE=0x...  LED matrix address if not 0xF0000000
 *
 * Ripes setup: I/O tab -> LED Matrix, Width 35, Height 25 (the default).
 *
 * LED layout (35 x 25, row-major, index = y*35 + x). Each sticker is
 * 3x3 = 9 LEDs, the four stickers of a face touch (6x6 face), and faces are
 * 1 LED apart. The net (27 x 20) is centred on the matrix:
 *
 *          x: 4      11     18     25
 *   y  2           [ U ]
 *   y  9     [ L ] [ F ] [ R ] [ B ]
 *   y 16           [ D ]
 *
 * Change -DSTK (sticker size), -DSGAP (gap between stickers) or -DFGAP
 * (gap between faces) to reshape it; the net is re-centred automatically.
 */
#include <stdint.h>
#include "tables.h"

#define MAXD 12 /* diameter is 11; one spare frame for the solved child */

/* ------------------------------------------------------------------ lookups */
static inline uint32_t pt(uint32_t f, uint32_t p4)
{
    return *(const uint32_t *) ((const uint8_t *) PT[f] + p4);
}
static inline uint32_t ot(uint32_t f, uint32_t o2)
{
    return *(const uint16_t *) ((const uint8_t *) OT[f] + o2);
}

/* -------------------------------------------------------------------- input */
/* 14 digits: positions 1..7 hold cubies '1'..'7' (each once), then seven
 * twists '1'..'3' whose internal values (digit - 1) sum to 0 mod 3. */
static int parse_state(const char *in, uint8_t *p, uint8_t *o)
{
    uint32_t seen = 0, sum = 0;
    for (int i = 0; i < 7; i++) {
        uint32_t d = (uint32_t) (uint8_t) in[i] - '1';
        if (d > 6 || (seen >> d & 1)) return 0;
        seen |= 1u << d;
        p[i] = (uint8_t) d;
    }
    for (int i = 0; i < 7; i++) {
        uint32_t d = (uint32_t) (uint8_t) in[7 + i] - '1';
        if (d > 2) return 0;
        o[i] = (uint8_t) d;
        sum += d;
    }
    /* sum <= 14, reduced mod 3 without a divide */
    if (sum >= 6) sum -= 6;
    if (sum >= 6) sum -= 6;
    if (sum >= 3) sum -= 3;
    return in[14] == 0 && sum == 0;
}

/* Search coordinates of a parsed state; every multiply is a shift-add. */
static void coords(const uint8_t *p, const uint8_t *o, uint32_t *p4,
                   uint32_t *o2, uint32_t *x)
{
    uint32_t c[7], q = 0, pa = 0, pb = 0;
    for (int i = 0; i < 7; i++) {
        uint32_t n = 0;
        for (int j = i + 1; j < 7; j++) n += p[j] < p[i];
        c[i] = n;
        if (p[i] == 3) pa = (uint32_t) i;
        if (p[i] == 6) pb = (uint32_t) i;
    }
    /* Lehmer rank by Horner's rule: r = r * (7 - i) + c[i] */
    uint32_t r = c[0];
    r = (r << 2) + (r << 1) + c[1]; /* x6 */
    r = (r << 2) + r + c[2];        /* x5 */
    r = (r << 2) + c[3];            /* x4 */
    r = (r << 1) + r + c[4];        /* x3 */
    r = (r << 1) + c[5];            /* x2; c[6] is always 0 */
    for (int i = 0; i < 6; i++) q = (q << 1) + q + o[i]; /* base 3 */
    *p4 = r << 2;
    *o2 = q << 1;
    *x = pa << 3 | pb;
}

/* --------------------------------------------------------------------- IDA* */
typedef struct {
    uint32_t p4, o2, x; /* node at this depth */
    uint32_t last;      /* face of the move into this node, 3 = none */
    uint32_t face;      /* face being expanded */
    uint32_t turn;      /* quarter turns applied so far on that face */
} frame_t;

/* Writes moves (3 * face + turns - 1) to path; returns the length, or -1
 * if no solution of length <= 11 exists (impossible for a valid state). */
static int ida(uint32_t p4, uint32_t o2, uint32_t x, uint8_t *path)
{
    frame_t st[MAXD + 1];
    /* R^4 is the identity, so the 4th R lookup returns to p and its entry
     * carries dP[p]: the root heuristic needs no separate table. */
    uint32_t w = pt(0, p4);
    w = pt(0, w >> 4);
    w = pt(0, w >> 4);
    w = pt(0, w >> 4);
    uint32_t h1 = w & 15, h2 = DX[o2 << 5 | x];
    uint32_t bound = h1 > h2 ? h1 : h2;
    if (bound == 0) return 0; /* already solved */

    for (;; bound++) {
        if (bound > 11) return -1;
        frame_t *fp = st;
        fp->p4 = p4, fp->o2 = o2, fp->x = x, fp->last = 3;
        uint32_t rem = bound - 1; /* largest h a child may have */
    enter:
        fp->face = fp->last == 0 ? 1 : 0; /* never turn the same face twice */
    face:
        {
            uint32_t f = fp->face, t;
            uint32_t a = fp->p4, b = fp->o2, c = fp->x;
            /* children of face f are 1, 2, 3 successive quarter turns */
            for (t = 1; t <= 3; t++) {
                uint32_t e = pt(f, a);
                a = e >> 4;
                b = ot(f, b);
                c = XT[f][c];
                /* g + 1 + h <= bound  <=>  h1 <= rem && h2 <= rem */
                if ((e & 15) > rem) continue;
                uint32_t g = DX[b << 5 | c];
                if (g > rem) continue;
                fp->turn = t;
                if (((e & 15) | g) == 0) { /* h = 0 only at solved */
                    /* IDA* reaches the goal only at depth == bound, so
                     * rem is 0 here. Using bound avoids fp - st, a divide
                     * by sizeof(frame_t) = 24 that GCC turns into
                     * __mulsi3 on RV32I. */
                    if (rem != 0) return -1;
                    int n = (int) bound;
                    for (int i = 0; i < n; i++)
                        path[i] = (uint8_t) (st[i].face * 3 + st[i].turn - 1);
                    return n;
                }
                fp[1].p4 = a, fp[1].o2 = b, fp[1].x = c, fp[1].last = f;
                fp++;
                rem--;
                goto enter;
            resume: /* back from a child: continue with the next turn */
                f = fp->face;
                t = fp->turn;
                a = fp[1].p4, b = fp[1].o2, c = fp[1].x;
            }
        }
        do fp->face++; /* next face, skipping the one just turned */
        while (fp->face == fp->last);
        if (fp->face < 3) goto face;
        if (fp == st) continue; /* bound exhausted: raise it */
        fp--;
        rem++;
        goto resume;
    }
}

/* -------------------------------------------- independent self-check (T5) */
/* Replays the path with the repository's source/twist rule, which shares
 * nothing with the search tables, and confirms the cube ends solved. */
static const uint8_t SRC[3][7] = {
    {1, 4, 2, 0, 3, 5, 6}, {0, 1, 2, 4, 5, 6, 3}, {0, 2, 5, 3, 1, 4, 6}};
static const uint8_t TW[3][7] = {
    {1, 2, 0, 2, 1, 0, 0}, {0, 0, 0, 1, 2, 1, 2}, {0, 0, 0, 0, 0, 0, 0}};

static void quarter(uint8_t *p, uint8_t *o, uint32_t f)
{
    uint8_t np[7], no[7];
    for (int i = 0; i < 7; i++) {
        uint32_t s = SRC[f][i], v = o[s] + TW[f][i];
        np[i] = p[s];
        no[i] = (uint8_t) (v >= 3 ? v - 3 : v); /* v <= 4: one subtract */
    }
    for (int i = 0; i < 7; i++) p[i] = np[i], o[i] = no[i];
}

static int verify_path(uint8_t *p, uint8_t *o, const uint8_t *path, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t m = path[i], f = 0;
        if (m >= 9) return 0;
        while (m >= 3) m -= 3, f++;
        for (uint32_t t = 0; t <= m; t++) quarter(p, o, f);
    }
    for (int i = 0; i < 7; i++)
        if (p[i] != i || o[i]) return 0;
    return 1;
}

static const char NAMES[9][3] = {"R",  "R2", "R'", "B", "B2",
                                 "B'", "D",  "D2", "D'"};


/* ================================================================ renderer */
#ifndef RENDER
#define RENDER 1
#endif
#ifndef DELAY
#define DELAY 100000
#endif
#ifndef TARGET
static int verbose; /* host only: print every frame as text */
#endif

#if RENDER
/* Corner positions as used by the solver (index into p[] / o[]), plus the
 * fixed anchor as position 7:
 *   0 UFR  1 DFR  2 DLF  3 UBR  4 DRB  5 DBL  6 ULB  7 UFL (anchor)
 * CORNER[c] lists the faces of corner c clockwise, U/D sticker first. A
 * cubie with twist t in position i shows, on that position's k-th sticker,
 * the colour CORNER[cubie][(k + t) mod 3]. This was checked against real
 * 3-D rotations of all 24 stickers for R, B and D, so it agrees with the
 * SRC/TW move rule above. */
enum { FACE_U, FACE_L, FACE_F, FACE_R, FACE_B, FACE_D };

static const uint8_t CORNER[8][3] = {
    {FACE_U, FACE_R, FACE_F}, {FACE_D, FACE_F, FACE_R}, {FACE_D, FACE_L, FACE_F}, {FACE_U, FACE_B, FACE_R},
    {FACE_D, FACE_R, FACE_B}, {FACE_D, FACE_B, FACE_L}, {FACE_U, FACE_L, FACE_B}, {FACE_U, FACE_F, FACE_L}};

/* NET[face][cell] = position << 2 | sticker, cells TL, TR, BL, BR as seen
 * looking at that face from outside (U with B at the top, D with F at the
 * top, L/F/R/B with U at the top). Face order U L F R B D. */
#define S(pos, k) ((pos) << 2 | (k))
static const uint8_t NET[6][4] = {
    {S(6, 0), S(3, 0), S(7, 0), S(0, 0)},  /* U */
    {S(6, 1), S(7, 2), S(5, 2), S(2, 1)},  /* L */
    {S(7, 1), S(0, 2), S(2, 2), S(1, 1)},  /* F */
    {S(0, 1), S(3, 2), S(1, 2), S(4, 1)},  /* R */
    {S(3, 1), S(6, 2), S(4, 2), S(5, 1)},  /* B */
    {S(2, 0), S(1, 0), S(5, 0), S(4, 0)}}; /* D */
#undef S

/* Geometry: each sticker is STK x STK LEDs (3x3 = 9 LEDs), stickers of one
 * face touch (SGAP = 0), faces are FGAP apart, and the net is centred on
 * the 35 x 25 matrix. All of this is folded at compile time. */
#ifndef STK
#define STK 3
#endif
#ifndef SGAP
#define SGAP 0
#endif
#ifndef FGAP
#define FGAP 1
#endif
#define FSZ (2 * STK + SGAP)                  /* face size in LEDs   */
#define NETW (4 * FSZ + 3 * FGAP)             /* net width           */
#define NETH (3 * FSZ + 2 * FGAP)             /* net height          */
#define X0 ((35 - NETW) / 2)
#define Y0 ((25 - NETH) / 2)
#define COL(i) (X0 + (i) * (FSZ + FGAP))
#define ROW(i) (Y0 + (i) * (FSZ + FGAP))
static const uint8_t FX[6] = {COL(1), COL(0), COL(1), COL(2), COL(3), COL(1)};
static const uint8_t FY[6] = {ROW(0), ROW(1), ROW(1), ROW(1), ROW(1), ROW(2)};

static const uint32_t COLOR[6] = {
    0xFFFFFF, /* U white  */
    0xFF7000, /* L orange */
    0x00D000, /* F green  */
    0xFF0000, /* R red    */
    0x0040FF, /* B blue   */
    0xFFFF00  /* D yellow */
};


#define LW 35
#define LH 25

#ifdef TARGET
#ifndef LED_BASE
#ifdef LED_MATRIX_0_BASE
#define LED_BASE LED_MATRIX_0_BASE
#else
#define LED_BASE 0xF0000000u
#endif
#endif
#define LEDFB ((volatile uint32_t *) (uintptr_t) (LED_BASE))
#else
static uint32_t fb_host[LW * LH];
#define LEDFB fb_host
#endif

/* y * 35 = y*32 + y*2 + y : no multiply on RV32I */
static void rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c)
{
    volatile uint32_t *row = LEDFB + (y << 5) + (y << 1) + y + x;
    for (; h; h--, row += LW)
        for (uint32_t i = 0; i < w; i++) row[i] = c;
}

static void clear_all(void) { rect(0, 0, LW, LH, 0); }

static void draw_cube(const uint8_t *p, const uint8_t *o)
{
    for (uint32_t f = 0; f < 6; f++)
        for (uint32_t cell = 0; cell < 4; cell++) {
            uint32_t v = NET[f][cell], pos = v >> 2, k = v & 3;
            uint32_t cub = 7, tw = 0;
            if (pos != 7) cub = p[pos], tw = o[pos];
            k += tw;
            if (k >= 3) k -= 3;
            uint32_t x = FX[f] + ((cell & 1) ? STK + SGAP : 0);
            uint32_t y = FY[f] + ((cell & 2) ? STK + SGAP : 0);
            rect(x, y, STK, STK, COLOR[CORNER[cub][k]]);
        }
}

#ifndef TARGET
static void show_frame(void);
#endif

static void pause_frame(uint32_t n)
{
#ifdef TARGET
    /* 2 instructions per count, kept in a register (the old volatile
     * loop went through the stack: 5 instructions per count) */
    if (n) __asm__ volatile("1: addi %0, %0, -1\n\tbnez %0, 1b" : "+r"(n));
#else
    (void) n;
    if (verbose) show_frame();
#endif
}
#else /* !RENDER: every renderer call disappears */
#define clear_all() ((void) 0)
#define draw_cube(p, o) ((void) 0)
#define pause_frame(n) ((void) 0)
#endif

/* Replays the solver's path on p/o, redrawing after each quarter turn.
 * X' is three quarter turns but is drawn once, so it never looks like a
 * 270-degree spin; X2 shows its halfway quarter turn. Returns 1 if the
 * cube ends solved. */
static int animate(uint8_t *p, uint8_t *o, const uint8_t *path, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t m = path[i], f = 0;
        while (m >= 3) m -= 3, f++;
        for (uint32_t q = 0; q <= m; q++) {
            quarter(p, o, f);
            if (m != 2 || q == 2) {
                draw_cube(p, o);
                pause_frame(DELAY);
            }
        }
    }
    for (int i = 0; i < 7; i++)
        if (p[i] != i || o[i]) return 0;
    return 1;
}

/* ------------------------------------------------------------- host build */
#ifndef TARGET
#include <stdio.h>
#include <string.h>

#if RENDER
static void show_frame(void)
{
    static const uint32_t col[7] = {0xFFFFFF, 0xFF7000, 0x00D000, 0xFF0000,
                                    0x0040FF, 0xFFFF00, 0};
    static const char ch[8] = "WOGRBY.";
    for (int y = 0; y < LH; y++) {
        for (int x = 0; x < LW; x++) {
            uint32_t c = fb_host[y * LW + x];
            char out = '?';
            for (int k = 0; k < 7; k++)
                if (col[k] == c) out = ch[k];
            putchar(out);
        }
        putchar('\n');
    }
    putchar('\n');
}
#endif

int main(int argc, char **argv)
{
    uint8_t p[7], o[7], q[7], r[7], path[16];
    uint32_t p4, o2, x;
    int a = 1;
    if (argc == 3 && strcmp(argv[1], "-v") == 0) verbose = 1, a = 2;
    if (argc != a + 1 || !parse_state(argv[a], p, o)) {
        fprintf(stderr, "usage: %s [-v] PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "cube_ripes");
        return 2;
    }
    clear_all();
    draw_cube(p, o);
    pause_frame(DELAY);
    coords(p, o, &p4, &o2, &x);
    int n = ida(p4, o2, x, path);
    memcpy(q, p, 7);
    memcpy(r, o, 7);
    if (n < 0 || !verify_path(q, r, path, n)) {
        fputs("internal error: no verified solution\n", stderr);
        return 1;
    }
    for (int i = 0; i < n; i++) printf("%s%s", i ? " " : "", NAMES[path[i]]);
    putchar('\n');
    if (verbose) fflush(stdout);
    if (!animate(p, o, path, n)) {
        fputs("internal error: animation did not end solved\n", stderr);
        return 1;
    }
    pause_frame(0);
    return fflush(stdout) != 0 || ferror(stdout);
}

/* ---------------------------------------------- RV32I build for Ripes ELF */
#else
/* -DSTATE=21345671111111 (no quotes needed, easy from cmd/PowerShell)
 * or -DINPUT='"21345671111111"'; default is the reporting vector. */
#define STR2(s) #s
#define STR(s) STR2(s)
#ifndef INPUT
#ifdef STATE
#define INPUT STR(STATE)
#else
#define INPUT "21345671111111"
#endif
#endif
static const char input[] = INPUT;

static void ecall2(uint32_t a0, uint32_t a7)
{
    register uint32_t r0 __asm__("a0") = a0, r7 __asm__("a7") = a7;
    __asm__ volatile("ecall" : "+r"(r0) : "r"(r7) : "memory");
}
static void print(const char *s) { ecall2((uint32_t) (uintptr_t) s, 4); }
static void __attribute__((noreturn)) exit_with(uint32_t code)
{
    ecall2(code, 93);
    for (;;) {}
}

void __attribute__((section(".text._start"), noreturn)) _start(void)
{
    uint8_t p[7], o[7], q[7], r[7], path[16];
    uint32_t p4, o2, x;
    clear_all();
    if (!parse_state(input, p, o)) {
        print("invalid input\n");
        exit_with(255);
    }
    /* 1. scrambled cube */
    draw_cube(p, o);
    pause_frame(DELAY << 2);

    /* 2. search */
    coords(p, o, &p4, &o2, &x);
    int n = ida(p4, o2, x, path);
    for (int i = 0; i < 7; i++) q[i] = p[i], r[i] = o[i];
    if (n < 0 || !verify_path(q, r, path, n)) {
        exit_with(254);
    }
    for (int i = 0; i < n; i++) {
        if (i) print(" ");
        print(NAMES[path[i]]);
    }
    print("\n");

    /* 3. replay the solver's path on the LED matrix */
    if (!animate(p, o, path, n)) {
        exit_with(254);
    }
    exit_with((uint32_t) n);
}
#endif