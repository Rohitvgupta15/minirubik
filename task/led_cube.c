#define LED ((volatile unsigned int *)0xF0000000)

void _start(void) {
    // 6 faces: {X, Y, Color}
    // Packed tight cross net: faces separated by just 1 pixel
    const struct { unsigned char x, y; unsigned int c; } faces[6] = {
        { 16,  8, 0xFFFFFF}, // U: White  (Top)
        { 13, 11, 0xFFA500}, // L: Orange (Left)
        { 16, 11, 0x00FF00}, // F: Green  (Center)
        { 19, 11, 0xFF0000}, // R: Red    (Right)
        { 22, 11, 0x0033FF}, // B: Blue   (Far Right)
        { 16, 14, 0xFFFF00}  // D: Yellow (Bottom)
    };

    // Draw each 2x2 face
    for (int f = 0; f < 6; f++) {
        for (int dy = 0; dy < 2; dy++) {
            for (int dx = 0; dx < 2; dx++) {
                LED[(faces[f].y + dy) * 35 + (faces[f].x + dx)] = faces[f].c;
            }
        }
    }

    // Exit
    register int a7 asm("a7") = 93;
    asm volatile("ecall" : : "r"(a7));
}