#include "block.h"
bool block_is_air_like(block_id_t id) {
  if (!(id > BLOCK_IGNORE)) {
    return false;
  }
  return true;
}
