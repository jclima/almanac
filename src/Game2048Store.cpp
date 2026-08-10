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
  // A board of the wrong length is corrupt, not upgradable: drop it and let
  // the activity start a fresh game rather than restoring half a board.
  if (board.isNull() || board.size() != Game2048::CELLS) {
    LOG_ERR("G2048", "Saved board malformed; discarding");
    for (uint8_t i = 0; i < Game2048::CELLS; ++i) cells[i] = 0;
    score = 0;
    bestScore = doc["best"] | 0u;  // a valid best score is still worth keeping
    return true;
  }

  for (uint8_t i = 0; i < Game2048::CELLS; ++i) {
    cells[i] = board[i] | 0u;
  }
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
