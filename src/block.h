#ifndef PERANTI_BLOCK_H
#define PERANTI_BLOCK_H

#include <stdint.h>
#include <stdbool.h>

typedef uint16_t block_id_t;

#define BLOCK_AIR    0
#define BLOCK_IGNORE 1  // unloaded/unknown -- per your Luanti notes, treat as air for culling
#define BLOCK_STONE  2

// True if this block contributes no geometry (air, ignore, and later
// any block you flag non-solid, e.g. glass/water if you special-case those).
bool block_is_air_like(block_id_t id);

#endif
