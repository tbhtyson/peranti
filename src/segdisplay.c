#include "segdisplay.h"
#include <stddef.h>

#define SEG(x) (1u << SEG_##x)

const GlyphEntry glyph_table[] = {
    {'0', SEG(TL_TM)|SEG(TM_TR)|SEG(TL_ML)|SEG(TR_MR)|SEG(ML_BL)|SEG(MR_BR)|SEG(BL_BM)|SEG(BM_BR)},
    {'1', SEG(TR_MR)|SEG(MR_BR)},
    {'2', SEG(TL_TM)|SEG(TM_TR)|SEG(TR_MR)|SEG(ML_MM)|SEG(MM_MR)|SEG(ML_BL)|SEG(BL_BM)|SEG(BM_BR)},
    {'3', SEG(TL_TM)|SEG(TM_TR)|SEG(TR_MR)|SEG(MM_MR)|SEG(MR_BR)|SEG(BL_BM)|SEG(BM_BR)},
    {'4', SEG(TL_ML)|SEG(TR_MR)|SEG(ML_MM)|SEG(MM_MR)|SEG(MR_BR)},
    {'5', SEG(TL_TM)|SEG(TM_TR)|SEG(TL_ML)|SEG(ML_MM)|SEG(MM_MR)|SEG(MR_BR)|SEG(BL_BM)|SEG(BM_BR)},
    {'6', SEG(TL_TM)|SEG(TM_TR)|SEG(TL_ML)|SEG(ML_MM)|SEG(MM_MR)|SEG(ML_BL)|SEG(MR_BR)|SEG(BL_BM)|SEG(BM_BR)},
    {'7', SEG(TL_TM)|SEG(TM_TR)|SEG(TR_MR)|SEG(MR_BR)},
    {'8', SEG(TL_TM)|SEG(TM_TR)|SEG(TL_ML)|SEG(TR_MR)|SEG(ML_MM)|SEG(MM_MR)|SEG(ML_BL)|SEG(MR_BR)|SEG(BL_BM)|SEG(BM_BR)},
    {'9', SEG(TL_TM)|SEG(TM_TR)|SEG(TL_ML)|SEG(TR_MR)|SEG(ML_MM)|SEG(MM_MR)|SEG(MR_BR)|SEG(BL_BM)|SEG(BM_BR)},
    {'F', SEG(TL_TM)|SEG(TM_TR)|SEG(TL_ML)|SEG(ML_MM)|SEG(MM_MR)|SEG(ML_BL)},
    {'P', SEG(TL_TM)|SEG(TM_TR)|SEG(TL_ML)|SEG(TR_MR)|SEG(ML_MM)|SEG(MM_MR)|SEG(ML_BL)},
    {'S', SEG(TL_TM)|SEG(TM_TR)|SEG(TL_ML)|SEG(ML_MM)|SEG(MM_MR)|SEG(MR_BR)|SEG(BL_BM)|SEG(BM_BR)},
};
const size_t glyph_table_count = sizeof(glyph_table) / sizeof(glyph_table[0]);

// 3x3 node grid, local unit-cell space: x in [0,1], y in [0,2] (0=bottom, 2=top).
const Vec2 segment_nodes[9] = {
    {0.0f, 2.0f}, {0.5f, 2.0f}, {1.0f, 2.0f}, // TL, TM, TR
    {0.0f, 1.0f}, {0.5f, 1.0f}, {1.0f, 1.0f}, // ML, MM, MR
    {0.0f, 0.0f}, {0.5f, 0.0f}, {1.0f, 0.0f}, // BL, BM, BR
};

// Node index pairs for each SegmentIndex, in enum order.
const uint8_t segment_endpoints[16][2] = {
    {0,1},{1,2},{0,3},{2,5},{0,4},{1,4},{2,4},
    {3,4},{4,5},{4,6},{4,7},{4,8},{3,6},{5,8},{6,7},{7,8},
};
