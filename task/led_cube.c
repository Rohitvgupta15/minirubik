
#define LED ((volatile unsigned int *)0xF0000000)

#define WIDTH      35
#define FACE_SIZE   6
#define GAP         1

void _start(void) {
    // Rubik's Cube cross layout matching the reference image.
    // Each face is a solid 6x6 LED square.

    const struct {
        unsigned char x, y;
        unsigned int color;
    } faces[6] = {
        // U: Top (White)
        {11,  2, 0xFFFFFF},

        // L: Left (Orange)
        { 4,  9, 0xFFA500},

        // F: Front (Green)
        {11,  9, 0x00FF00},

        // R: Right (Red)
        {18,  9, 0xFF0000},

        // B: Far right (Blue)
        {25,  9, 0x0033FF},

        // D: Bottom (Yellow)
        {11, 16, 0xFFFF00}
    };

    for (int f = 0; f < 6; f++) {
        for (int dy = 0; dy < FACE_SIZE; dy++) {
            for (int dx = 0; dx < FACE_SIZE; dx++) {
                int x = faces[f].x + dx;
                int y = faces[f].y + dy;

                LED[y * WIDTH + x] = faces[f].color;
            }
        }
    }

    // Exit for the RISC-V simulator.
    register int a7 asm("a7") = 93;
    asm volatile("ecall" : : "r"(a7));
}
