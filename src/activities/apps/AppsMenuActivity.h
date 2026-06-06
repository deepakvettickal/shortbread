#pragma once
#include <string>
#include <vector>
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct RadarNode;
struct RadarHomeStatus;

class AppsMenuActivity final : public Activity {
 public:
  explicit AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AppsMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int selectorIndex = 0;
  static constexpr int ITEM_COUNT = 4;
  static constexpr int COLS = 2;
  static constexpr int TILE_ROWS = 2;

  // Grid navigation helpers (tile zone only)
  int getTileRow() const { return selectorIndex / COLS; }
  int getTileCol() const { return selectorIndex % COLS; }

  // Full-screen book carousel (entered with Down from the bottom tile row)
  bool carouselMode = false;
  int carouselIndex = 0;  // last-selected book; persists across enter/exit of the carousel

  // The carousel shows every book on the SD card. Path list is built cheaply on enter;
  // per-book metadata (title/author/cover/progress) and cover thumbnails are generated
  // lazily for the currently visible window only, so a large library stays responsive.
  struct CarouselBook {
    std::string path;
    std::string title;
    std::string author;
    std::string coverBmpPath;
    char progress[24] = {};
    bool metaLoaded = false;
  };
  std::vector<CarouselBook> books;
  void scanBooks();        // enumerate SD-root books, most-recently-read first
  void ensureMeta(int i);  // lazy: title/author/cover path + progress for one book
  bool ensureCover(int i);  // lazy: generate the cover thumb; returns true if newly generated
  void ensureWindow(int center);  // meta+cover for center-1..center+1; requests redraw if covers grew

  bool firstRenderDone = false;
  int coverThumbH = 0;

  // Cached system info (refreshed on enter + periodically)
  uint32_t freeHeap = 0;
  uint8_t batteryPercent = 0;
  unsigned long uptimeSeconds = 0;
  bool wifiConnected = false;
  int8_t wifiRssi = 0;
  unsigned long lastInfoRefresh = 0;
  static constexpr unsigned long INFO_REFRESH_MS = 30000;
  char uptimeStr[16] = "";

  void refreshSystemInfo();

  // Last-used activity per category (read from SD on enter)
  char lastUsedName[ITEM_COUNT][32] = {};
  void loadLastUsed();

  // Rendering
  void drawTile(int index, int x, int y, int w, int h, bool selected) const;
  void drawStatusBar() const;
  void drawRecentBooks(int x, int y, int w, int h) const;
  void drawCarousel() const;
  // Draws a book cover thumbnail scaled into the given box. When cropFill is true the cover
  // fills the box width and is top-cropped; otherwise it is fitted (used for edge "peek" slivers
  // that rely on screen clipping). Returns false if no cover could be drawn.
  bool drawCoverScaled(const CarouselBook& book, int x, int y, int boxW, int boxH, uint8_t opacity,
                       bool cropFill) const;
};
