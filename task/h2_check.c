
#include <stdio.h>
#include <stdint.h>
#include "tables.h"

#define NP 5040
#define NO 729
#define NX 64

int main(void)
{
    unsigned errors = 0;
    unsigned max_perm = 0;
    unsigned max_dx = 0;
    unsigned dx_valid = 0;
    unsigned dx_sentinel = 0;

    /* Check PT: packed permutation transition and distance. */
    for (unsigned f = 0; f < 3; f++) {
        for (unsigned p = 0; p < NP; p++) {
            uint32_t entry = PT[f][p];
            unsigned next = entry >> 6;
            unsigned dist = entry & 15u;

            if (next >= NP || dist > 7) {
                errors++;
                continue;
            }

            if (dist > max_perm)
                max_perm = dist;

            /* Four quarter-turns must restore the permutation. */
            unsigned q = p;
            for (unsigned k = 0; k < 4; k++)
                q = PT[f][q] >> 6;

            if (q != p)
                errors++;
        }
    }

    /* Check OT: values are byte offsets to uint16_t entries. */
    for (unsigned f = 0; f < 3; f++) {
        for (unsigned o = 0; o < NO; o++) {
            unsigned offset = OT[f][o];

            if ((offset & 1u) || offset >= 2u * NO) {
                errors++;
                continue;
            }

            unsigned q = o;
            for (unsigned k = 0; k < 4; k++) {
                offset = OT[f][q];

                if ((offset & 1u) || offset >= 2u * NO) {
                    q = NO;
                    break;
                }

                q = offset / 2;
            }

            if (q != o)
                errors++;
        }
    }

    /* Check XT: four quarter-turns restore the position coordinate. */
    for (unsigned f = 0; f < 3; f++) {
        for (unsigned x = 0; x < NX; x++) {
            unsigned q = x;

            for (unsigned k = 0; k < 4; k++) {
                q = XT[f][q];
                if (q >= NX)
                    break;
            }

            if (q != x)
                errors++;
        }
    }

    /* Check DX distances and invalid-coordinate sentinels. */
    for (unsigned o = 0; o < NO; o++) {
        for (unsigned x = 0; x < NX; x++) {
            unsigned d = DX[o * NX + x];
            unsigned a = x >> 3;
            unsigned b = x & 7u;

            if (a == 7 || b == 7 || a == b) {
                dx_sentinel++;
                if (d != 255)
                    errors++;
            } else {
                dx_valid++;

                if (d == 255 || d > 8) {
                    errors++;
                    continue;
                }

                if (d > max_dx)
                    max_dx = d;
            }
        }
    }

    /* Solved abstraction: orientation rank 0, positions 3 and 6. */
    if (DX[30] != 0)
        errors++;

    printf("Permutation maximum: %u (expected 7)\n", max_perm);
    printf("DX maximum: %u (expected 8)\n", max_dx);
    printf("DX solved entry: %u (expected 0)\n", DX[30]);
    printf("DX valid entries: %u\n", dx_valid);
    printf("DX sentinel entries: %u\n", dx_sentinel);
    printf("PT, OT, XT transition checks completed\n");
    printf("Errors: %u\n", errors);

    if (max_perm != 7 || max_dx != 8)
        errors++;

    if (errors == 0) {
        printf("H2: PASS\n");
        return 0;
    }

    printf("H2: FAIL\n");
    return 1;
}
