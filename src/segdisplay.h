#ifndef PERANTI_SEGDISPLAY_H
#define PERANTI_SEGDISPLAY_H

#include <stddef.h>
#include <stdint.h>

// Segment index -> bit position in a glyph's 16-bit mask.
// Node names refer to the 3x3 grid: T/M/B (top/mid/bottom row) x L/M/R (col).
typedef enum {
    SEG_TL_TM = 0,  // top-left horizontal
    SEG_TM_TR = 1,  // top-right horizontal
    SEG_TL_ML = 2,  // upper-left vertical
    SEG_TR_MR = 3,  // upper-right vertical
    SEG_TL_MM = 4,  // upper-left diagonal
    SEG_TM_MM = 5,  // upper-center vertical
    SEG_TR_MM = 6,  // upper-right diagonal
    SEG_ML_MM = 7,  // middle-left horizontal
    SEG_MM_MR = 8,  // middle-right horizontal
    SEG_MM_BL = 9,  // lower-left diagonal
    SEG_MM_BM = 10, // lower-center vertical
    SEG_MM_BR = 11, // lower-right diagonal
    SEG_ML_BL = 12, // lower-left vertical
    SEG_MR_BR = 13, // lower-right vertical
    SEG_BL_BM = 14, // bottom-left horizontal
    SEG_BM_BR = 15, // bottom-right horizontal
} SegmentIndex;

typedef struct {
    char character;
    uint16_t segments; // bit i set = segment (SegmentIndex)i is lit
} GlyphEntry;

typedef struct {
    float x, y;
} Vec2;

extern const Vec2 segment_nodes[9];
extern const uint8_t segment_endpoints[16][2];

extern const GlyphEntry glyph_table[];
extern const size_t glyph_table_count;

#endif
