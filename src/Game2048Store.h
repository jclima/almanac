#pragma once

#include <PersistableStore.h>

#include "Game2048.h"

// Persisted 2048 board, so a game survives leaving the activity and powering
// off. Lives alongside settings.json rather than in AlmanacSettings: transient
// game state does not belong in the user-preferences file or its migration
// path.
class Game2048Store : public PersistableStore<Game2048Store> {
 private:
  Game2048Store() = default;
  friend class PersistableStore<Game2048Store>;

 public:
  // Exponents, matching Game2048's encoding. All zero means "no saved game".
  uint8_t cells[Game2048::CELLS] = {};
  uint32_t score = 0;
  uint32_t bestScore = 0;

  static const char* getFilePath() { return "/.crosspoint/2048.json"; }

  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // True when the stored board has at least one tile.
  bool hasSavedGame() const;
};

#define GAME_2048_STORE Game2048Store::getInstance()
