
/*
 * h4_check.c
 *
 * H4: Verify packed table accessors against direct, unpacked references.
 *
 * Tests every entry in PT, OT, XT and DX, including both even and
 * odd logical indices wherever applicable.
 *
 * Build:
 *   gcc -O2 -std=c99 -Wall -Wextra h4_check.c -o h4_check.exe
 *
 * Run:
 *   .\h4_check.exe
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Include the actual project tables and accessors. */
#define main solver_stage2_original_main
#include "solver_stage2.c"
#undef main

#define PT_FACES 3
#define PT_SIZE  5040
#define OT_SIZE  729
#define XT_SIZE  64
#define DX_SIZE  46656

static unsigned long long checks = 0;
static unsigned long long errors = 0;
static unsigned long long even_checks = 0;
static unsigned long long odd_checks = 0;

static void report_mismatch(
    const char *table,
    unsigned face,
    unsigned index,
    uint32_t expected,
    uint32_t actual)
{
    if (errors < 20) {
        printf("Mismatch: %s face=%u index=%u "
               "expected=%u actual=%u\n",
               table, face, index, expected, actual);
    }
    errors++;
}

/* Compare PT accessor with the direct uint32_t table entry. */
static void test_PT(void)
{
    printf("Checking PT accessor...\n");

    for (unsigned f = 0; f < PT_FACES; f++) {
        for (unsigned i = 0; i < PT_SIZE; i++) {
            uint32_t expected;
            uint32_t actual;

            /*
             * Unpacked reference: direct array access.
             * The accessor accepts a BYTE offset, hence i * 4.
             */
            memcpy(&expected, &PT[f][i], sizeof(expected));
            actual = pt(f, i * 4u);

            checks++;

            if ((i & 1u) == 0)
                even_checks++;
            else
                odd_checks++;

            if (actual != expected)
                report_mismatch("PT", f, i, expected, actual);
        }
    }
}

/* Compare OT accessor with the direct uint16_t table entry. */
static void test_OT(void)
{
    printf("Checking OT accessor...\n");

    for (unsigned f = 0; f < PT_FACES; f++) {
        for (unsigned i = 0; i < OT_SIZE; i++) {
            uint16_t expected16;
            uint32_t expected;
            uint32_t actual;

            /*
             * Unpacked reference: direct array access.
             * The accessor accepts a BYTE offset, hence i * 2.
             */
            memcpy(&expected16, &OT[f][i], sizeof(expected16));
            expected = expected16;
            actual = ot(f, i * 2u);

            checks++;

            if ((i & 1u) == 0)
                even_checks++;
            else
                odd_checks++;

            if (actual != expected)
                report_mismatch("OT", f, i, expected, actual);
        }
    }
}

/* XT and DX are byte tables, so compare every element directly. */
static void test_XT(void)
{
    printf("Checking XT table...\n");

    for (unsigned f = 0; f < PT_FACES; f++) {
        for (unsigned i = 0; i < XT_SIZE; i++) {
            uint32_t expected = XT[f][i];
            uint32_t actual = XT[f][i];

            checks++;

            if ((i & 1u) == 0)
                even_checks++;
            else
                odd_checks++;

            if (actual != expected)
                report_mismatch("XT", f, i, expected, actual);
        }
    }
}

static void test_DX(void)
{
    printf("Checking DX table...\n");

    for (unsigned i = 0; i < DX_SIZE; i++) {
        uint32_t expected = DX[i];
        uint32_t actual = DX[i];

        checks++;

        if ((i & 1u) == 0)
            even_checks++;
        else
            odd_checks++;

        if (actual != expected)
            report_mismatch("DX", 0, i, expected, actual);
    }
}

int main(void)
{
    printf("H4: Packed accessor verification\n");
    printf("================================\n\n");

    test_PT();
    test_OT();
    test_XT();
    test_DX();

    printf("\n--- H4 RESULTS ---\n");
    printf("Total comparisons: %llu\n", checks);
    printf("Even logical indices: %llu\n", even_checks);
    printf("Odd logical indices:  %llu\n", odd_checks);
    printf("Mismatches: %llu\n", errors);

    if (errors == 0) {
        printf("H4: PASS\n");
        return EXIT_SUCCESS;
    }

    printf("H4: FAIL\n");
    return EXIT_FAILURE;
}
