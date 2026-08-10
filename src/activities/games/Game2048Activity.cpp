#include "Game2048Activity.h"

#include <I18n.h>
#include <esp_random.h>

#include <cstdio>
#include <cstring>

#include "Game2048Store.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/themes/Game2048Layout.h"
#include "fontIds.h"

namespace {

uint32_t hardwareRandom(void*) { return esp_random(); }

// Tiles from 128 up get a dithered wash. On a 1-bit panel the number alone
// carries the value, but the wash gives the heavy end of the board a shape
// that reads without being parsed digit by digit.
constexpr uint8_t DITHER_FROM_EXPONENT = 7;  // 2^7 == 128

// Fewer digits, bigger type. Six digits (131072) still has to fit. These IDs
// are the ones main.cpp actually registers with the renderer (see
// src/main.cpp:243-256); an unregistered ID renders nothing.
int fontForDigits(const int digits) {
  if (digits <= 2) return NOTOSANS_18_FONT_ID;
  if (digits <= 4) return UI_12_FONT_ID;
  return UI_10_FONT_ID;
}

}  // namespace

void Game2048Activity::onEnter() {
  Activity::onEnter();

  GAME_2048_STORE.loadFromFile();  // false simply means no save yet
  if (!GAME_2048_STORE.hasSavedGame() || !game.restore(GAME_2048_STORE.cells, GAME_2048_STORE.score)) {
    startNewGame();
  }
  if (game.status() == Game2048::Status::GameOver) {
    startNewGame();
  }

  requestUpdate();
}

void Game2048Activity::onExit() {
  // One write, on the way out — never per move. SPIFFS/SD sectors have a
  // finite erase budget and an e-ink game would otherwise write on every press.
  if (dirty) {
    for (uint8_t i = 0; i < Game2048::CELLS; ++i) {
      GAME_2048_STORE.cells[i] = game.rawCells()[i];
    }
    GAME_2048_STORE.score = game.score();
    if (game.score() > GAME_2048_STORE.bestScore) GAME_2048_STORE.bestScore = game.score();
    if (!GAME_2048_STORE.saveToFile()) {
      LOG_ERR("G2048", "Failed to save game");  // not worth blocking exit
    }
    dirty = false;
  }
  Activity::onExit();
}

void Game2048Activity::startNewGame() {
  // Fold the outgoing game's score into the best before it's discarded --
  // this is the only place a game ends (Confirm during play, or a restored
  // game-over board on entry), so it's the only place the best can be lost.
  if (game.score() > GAME_2048_STORE.bestScore) GAME_2048_STORE.bestScore = game.score();
  game.reset(hardwareRandom, nullptr);
  // 1, not MOVES_PER_DEGHOST: force the *next* render to de-ghost. A fresh
  // board is the largest possible pixel delta FAST_REFRESH's differential
  // waveform will ever see, so it must not be the one that lands right after
  // a reset countdown.
  movesUntilDeghost = 1;
  dirty = true;
}

void Game2048Activity::applyMove(const Game2048::Direction d) {
  if (!game.move(d, hardwareRandom, nullptr)) return;  // nothing changed: no repaint
  dirty = true;
  requestUpdate();
}

void Game2048Activity::loop() {
  using Button = MappedInputManager::Button;

  if (mappedInput.wasReleased(Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(Button::Confirm)) {
    startNewGame();
    requestUpdate();
    return;
  }

  // Screen* directions are rotated onto physical buttons per orientation by
  // MappedInputManager, so this activity needs no orientation handling at all.
  if (mappedInput.wasReleased(Button::ScreenLeft)) {
    applyMove(Game2048::Direction::Left);
  } else if (mappedInput.wasReleased(Button::ScreenRight)) {
    applyMove(Game2048::Direction::Right);
  } else if (mappedInput.wasReleased(Button::ScreenUp)) {
    applyMove(Game2048::Direction::Up);
  } else if (mappedInput.wasReleased(Button::ScreenDown)) {
    applyMove(Game2048::Direction::Down);
  }

  if (mappedInput.hasTouch()) {
    switch (mappedInput.wasSwipe()) {
      case MappedInputManager::SwipeDir::Left:
        applyMove(Game2048::Direction::Left);
        break;
      case MappedInputManager::SwipeDir::Right:
        applyMove(Game2048::Direction::Right);
        break;
      case MappedInputManager::SwipeDir::Up:
        applyMove(Game2048::Direction::Up);
        break;
      case MappedInputManager::SwipeDir::Down:
        applyMove(Game2048::Direction::Down);
        break;
      case MappedInputManager::SwipeDir::None:
        break;
    }
  }
}

void Game2048Activity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_GAME_2048), nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  // Score line above the board.
  char scoreLine[64];
  snprintf(
      scoreLine, sizeof(scoreLine), "%s %lu    %s %lu", tr(STR_SCORE), static_cast<unsigned long>(game.score()),
      tr(STR_BEST),
      static_cast<unsigned long>(GAME_2048_STORE.bestScore > game.score() ? GAME_2048_STORE.bestScore : game.score()));
  renderer.drawCenteredText(UI_12_FONT_ID, contentTop, scoreLine, true);

  const int scoreHeight = renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;
  const int boardTop = contentTop + scoreHeight;
  const int boardHeight = pageHeight - boardTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const auto geometry = Game2048Layout::boardGeometry(Rect{0, boardTop, pageWidth, boardHeight});

  for (int row = 0; row < Game2048Layout::GRID; ++row) {
    for (int col = 0; col < Game2048Layout::GRID; ++col) {
      const Rect t = Game2048Layout::tileRect(geometry, row, col);
      const uint8_t exponent = game.exponentAt(static_cast<uint8_t>(row), static_cast<uint8_t>(col));

      if (exponent >= DITHER_FROM_EXPONENT) {
        renderer.fillRectDither(t.x, t.y, t.width, t.height, Color::LightGray);
      }
      renderer.drawRoundedRect(t.x, t.y, t.width, t.height, 1, 4, true);

      if (exponent == 0) continue;

      char label[8];
      snprintf(label, sizeof(label), "%lu", 1UL << exponent);
      const int digits = static_cast<int>(strlen(label));
      const int fontId = fontForDigits(digits);
      const int textWidth = renderer.getTextWidth(fontId, label);
      const int textY = t.y + (t.height - renderer.getLineHeight(fontId)) / 2;
      renderer.drawText(fontId, t.x + (t.width - textWidth) / 2, textY, label, true);
    }
  }

  if (game.status() == Game2048::Status::GameOver) {
    const int bannerY = geometry.originY + geometry.tilePitch * 2;
    const int bannerHeight = renderer.getLineHeight(UI_12_FONT_ID);
    // Clear a band behind the banner first -- drawCenteredText only sets
    // glyph pixels, and at game over the board is full (with a dithered
    // wash on tiles at DITHER_FROM_EXPONENT and up), so without this the
    // text would land on top of tile borders and dither speckle. Same
    // pattern as BaseTheme::drawHeader's battery-area clear.
    renderer.fillRect(0, bannerY, pageWidth, bannerHeight, false);
    renderer.drawCenteredText(UI_12_FONT_ID, bannerY, tr(STR_GAME_OVER), true);
  }

  const auto labels = mappedInput.mapDirectionalLabels(tr(STR_BACK), tr(STR_NEW_GAME), "", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // FAST for responsiveness, HALF periodically to clear the ghosting it leaves
  // behind. FULL is deliberately unused: no activity uses it, and its 0xF7
  // waveform blinks.
  const bool deghost = (movesUntilDeghost <= 1) || (game.status() == Game2048::Status::GameOver);
  renderer.displayBuffer(deghost ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  movesUntilDeghost = deghost ? MOVES_PER_DEGHOST : static_cast<uint8_t>(movesUntilDeghost - 1);
}
