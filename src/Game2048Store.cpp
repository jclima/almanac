#include "Game2048Store.h"

#include <Logging.h>

void Game2048Store::toJson(JsonDocument& doc) const {
  JsonArray board = doc["cells"].to<JsonArray>();
  for (uint8_t i = 0; i < Game2048::CELLS; ++i) board.add(cells[i]);
  doc["score"] = score;
  doc["best"] = bestScore;
}

bool Game2048Store::fromJson(JsonVariantConst doc) {
  JsonArrayConst board = doc["cells"];
  bool corrupt = board.isNull() || board.size() != Game2048::CELLS;

  // Read into a uint32_t before narrowing to uint8_t: a stored value like 260
  // would silently truncate to 4 -- a legal exponent -- if cast straight
  // down, so a corrupt file could otherwise produce a legal-looking board.
  uint32_t parsed[Game2048::CELLS] = {};
  if (!corrupt) {
    for (uint8_t i = 0; i < Game2048::CELLS; ++i) {
      parsed[i] = board[i] | 0u;
      if (parsed[i] > Game2048::MAX_EXPONENT) {
        corrupt = true;
        break;
      }
    }
  }

  if (corrupt) {
    // Wrong length or an out-of-range exponent: either way the board is
    // corrupt, not upgradable. Drop it and let the activity start a fresh
    // game rather than restoring half a board (or a silently-truncated one).
    LOG_ERR("G2048", "Saved board malformed; discarding");
    for (uint8_t i = 0; i < Game2048::CELLS; ++i) cells[i] = 0;
    score = 0;
    bestScore = doc["best"] | 0u;  // a valid best score is still worth keeping
    return true;
  }

  for (uint8_t i = 0; i < Game2048::CELLS; ++i) cells[i] = static_cast<uint8_t>(parsed[i]);
  score = doc["score"] | 0u;
  bestScore = doc["best"] | 0u;
  return true;
}

bool Game2048Store::hasSavedGame() const {
  for (uint8_t i = 0; i < Game2048::CELLS; ++i) {
    if (cells[i] != 0) return true;
  }
  return false;
}
