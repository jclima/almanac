#include "HomeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "AlmanacSettings.h"
#include "AlmanacState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/themes/MenuLayout.h"
#include "fontIds.h"

namespace {
// Whether a string would actually show something if drawn. A title of only
// spaces is as useless on a tile as an empty one, and EPUB metadata can
// legitimately carry "<dc:title> </dc:title>".
//
// Compared as unsigned char deliberately: a UTF-8 lead or continuation byte is
// >= 0x80, which is NEGATIVE as a signed char on this target, so a signed
// comparison would judge every non-ASCII title -- Cyrillic, Greek, CJK -- to be
// blank and silently replace it with the generic label. std::isspace is avoided
// for the same reason, plus its locale dependence.
bool hasVisibleCharacter(const std::string& text) {
  return std::any_of(text.begin(), text.end(), [](const char c) { return static_cast<unsigned char>(c) > ' '; });
}
}  // namespace

MenuLayout::HomeComposition HomeActivity::menuComposition() const {
  return MenuLayout::HomeComposition{getMenuItemCount(), hasContinueReadingTile()};
}

bool HomeActivity::hasContinueReadingTile() const {
  return UITheme::getInstance().getMetrics().homeContinueReadingInMenu && !recentBooks.empty();
}

int HomeActivity::getMenuItemCount() const {
  int count = 5;  // File Browser, Recents, File transfer, Nearby Flights, Settings
  if (hasContinueReadingTile()) {
    count++;
  }
  if (hasOpdsServers) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  // MenuLayout::homeTileRect assumes a portrait pageHeight. Readers and the
  // sleep screen reset orientation to Portrait in their own onExit(), but
  // nothing enforces it from Home's side -- assert it here rather than trust
  // every caller upstream to have reset it (a landscape pageHeight would emit
  // tile rects that overlap and fall off-screen, and GfxRenderer logs once
  // per out-of-bounds pixel, i.e. a watchdog reboot, not a cosmetic glitch).
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  // Continue Reading is a single tile (index 0) when present, never one tile
  // per recent book -- see hasContinueReadingTile().
  const int base = hasContinueReadingTile() ? 1 : 0;
  selectorIndex = initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, hasOpdsServers);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() { Activity::onExit(); }

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();

  auto activateSelection = [this] {
    // Continue Reading is always exactly one tile (index 0), never one tile
    // per recent book -- see hasContinueReadingTile(). Gating on the same
    // helper render() and getMenuItemCount() use is what keeps this in step
    // with them even if that condition ever changes.
    if (hasContinueReadingTile() && selectorIndex == 0) {
      onSelectBook(recentBooks[0].path);
      return;
    }
    const int menuIndex = selectorIndex - (hasContinueReadingTile() ? 1 : 0);
    switch (indexToMenuItem(menuIndex, hasOpdsServers)) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::RECENTS:
        onRecentsOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:
        onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::NEARBY_FLIGHTS:
        onNearbyFlightsOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      default:
        break;
    }
  };

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backPressSeen = true;

  // Back is otherwise unused on the home menu: open the most recently read
  // book directly (recentBooks is most-recent-first and already pruned of
  // files missing from the SD card). backPressSeen guards against the stale
  // release of the Back press that closed the previous activity.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && backPressSeen && !recentBooks.empty()) {
    onSelectBook(recentBooks[0].path);
    return;
  }

  // Hit-test against the same tile rects the theme drew from: homeTileRect
  // fed the same UITheme::getInstance().getMetrics() result (metrics, already
  // bound above) and the same composition, so drawn tiles and touch targets
  // cannot drift apart.
  const auto composition = menuComposition();
  const auto tileAt = [&](const int px, const int py, int& index) {
    for (int i = 0; i < composition.tileCount; i++) {
      const Rect t =
          MenuLayout::homeTileRect(metrics, renderer.getScreenWidth(), renderer.getScreenHeight(), composition, i);
      if (px >= t.x && px < t.x + t.width && py >= t.y && py < t.y + t.height) {
        index = i;
        return true;
      }
    }
    return false;
  };

  int tx = 0;
  int ty = 0;
  int touchedIndex = -1;
  if (mappedInput.wasScreenTouchDown(tx, ty) && tileAt(tx, ty, touchedIndex)) {
    if (selectorIndex != touchedIndex) {
      selectorIndex = touchedIndex;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasScreenTapped(tx, ty) && tileAt(tx, ty, touchedIndex)) {
    selectorIndex = touchedIndex;
    activateSelection();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHomeMasthead(renderer, MenuLayout::homeMastheadRect(pageWidth));

  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER),
                                        tr(STR_NEARBY_FLIGHTS), tr(STR_SETTINGS_TITLE)};

  if (hasOpdsServers) {
    menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
  }

  if (hasContinueReadingTile()) {
    // Insert Continue Reading at the top -- same condition getMenuItemCount()
    // (via menuComposition()) uses, so the list built here and the tile count
    // it is indexed with agree by construction.
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
  }

  // Same composition the hit-test in loop() uses, so drawn tiles and touch
  // targets cannot disagree. The bounds check is a second line of defence,
  // not a substitute for that agreement: AlmanacTheme::drawHomeMenu has no
  // way to enforce it itself, so if tileCount and menuItems ever did drift
  // apart, this turns what would be an out-of-bounds read into an empty label.
  GUI.drawHomeMenu(
      renderer, pageWidth, pageHeight, menuComposition(), selectorIndex, [this, &menuItems](int index) -> std::string {
        // Identify WHICH book, not just that one exists -- a book with no
        // usable <dc:title> in its EPUB metadata leaves RecentBook::title
        // empty or blank (Epub::getTitle() has no filename fallback, unlike
        // Xtc/Txt, and does not trim), so fall through to the generic label
        // rather than draw a tile with nothing on it.
        if (index == 0 && hasContinueReadingTile() && hasVisibleCharacter(recentBooks[0].title)) {
          return recentBooks[0].title;
        }
        return index >= 0 && index < static_cast<int>(menuItems.size()) ? std::string(menuItems[index]) : std::string();
      });

  const auto labels = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onNearbyFlightsOpen() { activityManager.goToNearbyFlights(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
