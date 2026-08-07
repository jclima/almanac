#pragma once
#include <functional>
#include <vector>

#include "./FileBrowserActivity.h"
#include "activities/Activity.h"
#include "components/themes/MenuLayout.h"
#include "util/ButtonNavigator.h"

struct RecentBook;

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool firstRenderDone = false;
  bool hasOpdsServers = false;
  // Home can be entered while Back is still held (e.g. leaving Settings with
  // Back): ignore that stale release until a fresh press is seen here.
  bool backPressSeen = false;
  std::vector<RecentBook> recentBooks;
  const HomeMenuItem initialMenuItem;

  // Convert HomeMenuItem to menu index (used in onEnter)
  static int menuItemToIndex(HomeMenuItem item, bool hasOpdsUrl) {
    int i = 0;
    if (item == HomeMenuItem::FILE_BROWSER) return i;
    ++i;
    if (item == HomeMenuItem::RECENTS) return i;
    ++i;
    if (item == HomeMenuItem::OPDS_BROWSER) return hasOpdsUrl ? i : 0;
    if (hasOpdsUrl) ++i;
    if (item == HomeMenuItem::FILE_TRANSFER) return i;
    ++i;
    if (item == HomeMenuItem::NEARBY_FLIGHTS) return i;
    ++i;
    if (item == HomeMenuItem::SETTINGS_MENU) return i;
    return 0;
  }

  // Convert menu index to HomeMenuItem (used in loop)
  static HomeMenuItem indexToMenuItem(int idx, bool hasOpdsUrl) {
    int i = 0;
    if (idx == i++) return HomeMenuItem::FILE_BROWSER;
    if (idx == i++) return HomeMenuItem::RECENTS;
    if (hasOpdsUrl && idx == i++) return HomeMenuItem::OPDS_BROWSER;
    if (idx == i++) return HomeMenuItem::FILE_TRANSFER;
    if (idx == i++) return HomeMenuItem::NEARBY_FLIGHTS;
    if (idx == i) return HomeMenuItem::SETTINGS_MENU;
    return HomeMenuItem::NONE;
  }
  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onRecentsOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();
  void onNearbyFlightsOpen();

  // Shared by render() and loop() so drawn tiles and touch targets agree.
  MenuLayout::HomeComposition menuComposition() const;

  // Single source of truth for whether tile 0 is the Continue Reading tile.
  // getMenuItemCount()'s count, menuComposition()'s leadingWideTile, render()'s
  // menuItems, onEnter()'s initial selectorIndex, and loop()'s
  // activateSelection all have to agree on this or they drift apart --
  // AlmanacTheme::drawHomeMenu indexes menuItems[i] for i in [0, tileCount)
  // with no bounds check, so a count that disagrees with the list is an
  // out-of-bounds read, not just a cosmetic bug. Every one of those sites
  // reads this rather than re-deriving metrics.homeContinueReadingInMenu &&
  // !recentBooks.empty() locally.
  bool hasContinueReadingTile() const;

  int getMenuItemCount() const;
  void loadRecentBooks(int maxBooks);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE)
      : Activity("Home", renderer, mappedInput), initialMenuItem(initialMenuItemValue) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
