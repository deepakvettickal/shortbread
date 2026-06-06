#include "AppsMenuActivity.h"

#include <algorithm>

#include <I18n.h>

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <Xtc.h>
#include "AppCategoryActivity.h"
#include "BleScannerActivity.h"
#include "CasinoActivity.h"
#include "ChessActivity.h"
#include "DiceRollerActivity.h"
#include "GameOfLifeActivity.h"
#include "MappedInputManager.h"
#include "MinesweeperActivity.h"
#include "ClockActivity.h"
#include "PasswordManagerActivity.h"
#include "SnakeActivity.h"
#include "SudokuActivity.h"
#include "TetrisActivity.h"
#include "SdFileBrowserActivity.h"
#include "UnitConverterActivity.h"
#include "VoronoiActivity.h"
#include "WifiConnectActivity.h"
#include "WifiScannerActivity.h"
#include "MatrixRainActivity.h"
#include "MazeActivity.h"
#include "CalculatorActivity.h"
#include "TaskManagerActivity.h"
#include "BatteryMonitorActivity.h"
#include "DeviceInfoActivity.h"
#include "BackgroundManagerActivity.h"
#include "ReadingStatsActivity.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/home/RecentBooksActivity.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <WiFi.h>
#include <HalPowerManager.h>
#include <HalStorage.h>


void AppsMenuActivity::scanBooks() {
  books.clear();

  // Most-recently-read books first (so the bottom preview defaults to "continue reading"),
  // then every other EPUB/XTC on the SD root in alphabetical order.
  std::vector<std::string> seen;
  for (const auto& b : RECENT_BOOKS.getBooks()) {
    if (!Storage.exists(b.path.c_str())) continue;
    CarouselBook cb;
    cb.path = b.path;
    cb.title = b.title;
    cb.author = b.author;
    cb.coverBmpPath = b.coverBmpPath;
    cb.metaLoaded = !b.title.empty();  // recent store already has title/cover
    books.push_back(std::move(cb));
    seen.push_back(b.path);
  }

  std::vector<std::string> others;
  auto root = Storage.open("/");
  if (root && root.isDirectory()) {
    root.rewindDirectory();
    char name[512];
    for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
      const bool isDir = file.isDirectory();
      file.getName(name, sizeof(name));
      file.close();
      if (isDir || name[0] == '.') continue;
      std::string_view fn{name};
      if (!FsHelpers::hasEpubExtension(fn) && !FsHelpers::hasXtcExtension(fn)) continue;
      std::string full = std::string("/") + name;
      if (std::find(seen.begin(), seen.end(), full) != seen.end()) continue;
      others.push_back(std::move(full));
    }
  }
  if (root) root.close();
  std::sort(others.begin(), others.end());
  for (auto& p : others) {
    CarouselBook cb;
    cb.path = std::move(p);
    books.push_back(std::move(cb));
  }
}

void AppsMenuActivity::ensureMeta(int i) {
  if (i < 0 || i >= (int)books.size()) return;
  CarouselBook& b = books[i];
  if (b.metaLoaded) return;
  b.metaLoaded = true;

  std::string cachePath;
  if (FsHelpers::hasEpubExtension(b.path)) {
    Epub epub(b.path, "/.crosspoint");
    if (epub.load(true, true)) {  // build cache if missing so title/cover are available
      b.title = epub.getTitle();
      b.author = epub.getAuthor();
      b.coverBmpPath = epub.getCoverBmpPath();
    }
    cachePath = epub.getCachePath();
  } else if (FsHelpers::hasXtcExtension(b.path)) {
    Xtc xtc(b.path, "/.crosspoint");
    if (xtc.load()) {
      b.title = xtc.getTitle();
      b.author = xtc.getAuthor();
      b.coverBmpPath = xtc.getCoverBmpPath();
    }
    cachePath = xtc.getCachePath();
  }

  // Reading progress, read straight from progress.bin (no full book load)
  if (!cachePath.empty()) {
    std::string progressPath = cachePath + "/progress.bin";
    FsFile f;
    if (Storage.openFileForRead("APM", progressPath, f)) {
      uint8_t data[6] = {};
      int n = f.read(data, 6);
      f.close();
      if (n >= 4) {
        int spineIndex = data[0] + (data[1] << 8);
        int curPage = data[2] + (data[3] << 8);
        int pageCount = (n >= 6) ? data[4] + (data[5] << 8) : 0;
        if (pageCount > 0)
          snprintf(b.progress, sizeof(b.progress), "Ch.%d p.%d/%d", spineIndex + 1, curPage + 1, pageCount);
        else
          snprintf(b.progress, sizeof(b.progress), "Ch.%d p.%d", spineIndex + 1, curPage + 1);
      }
    }
  }
}

bool AppsMenuActivity::ensureCover(int i) {
  if (i < 0 || i >= (int)books.size()) return false;
  ensureMeta(i);
  CarouselBook& b = books[i];
  if (b.coverBmpPath.empty() || coverThumbH <= 0) return false;
  std::string thumbPath = UITheme::getCoverThumbPath(b.coverBmpPath, coverThumbH);
  if (Storage.exists(thumbPath.c_str())) return false;  // already generated

  if (FsHelpers::hasEpubExtension(b.path)) {
    Epub epub(b.path, "/.crosspoint");
    epub.load(false, true);
    if (!epub.generateThumbBmp(coverThumbH)) { b.coverBmpPath = ""; return false; }
  } else if (FsHelpers::hasXtcExtension(b.path)) {
    Xtc xtc(b.path, "/.crosspoint");
    if (xtc.load() && !xtc.generateThumbBmp(coverThumbH)) { b.coverBmpPath = ""; return false; }
  }
  return true;
}

void AppsMenuActivity::ensureWindow(int center) {
  bool generated = false;
  for (int i = center - 1; i <= center + 1; i++) {
    if (i < 0 || i >= (int)books.size()) continue;
    ensureMeta(i);
    if (ensureCover(i)) generated = true;
  }
  if (generated) requestUpdate();  // covers appeared — redraw with art
}

void AppsMenuActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  carouselMode = false;
  firstRenderDone = false;
  refreshSystemInfo();
  loadLastUsed();
  scanBooks();
  // Keep the last-selected book in range (the library may have changed)
  if (carouselIndex >= (int)books.size()) carouselIndex = 0;
  requestUpdate();
}

void AppsMenuActivity::loop() {
  const int bookCount = (int)books.size();

  // === Full-screen carousel: Left/Right browse, Confirm opens, Up exits. Down/Back ignored. ===
  if (carouselMode) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (carouselIndex > 0) { carouselIndex--; requestUpdate(); }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      if (carouselIndex < bookCount - 1) { carouselIndex++; requestUpdate(); }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      carouselMode = false;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (carouselIndex < bookCount) {
        activityManager.goToReader(books[carouselIndex].path);
        return;
      }
    }
    return;  // carousel owns all input
  }

  // === Tile grid navigation (2×2) ===
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    int col = getTileCol() + 1;
    int row = getTileRow();
    if (col >= COLS) { col = 0; row = (row + 1) % TILE_ROWS; }
    selectorIndex = row * COLS + col;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    int col = getTileCol() - 1;
    int row = getTileRow();
    if (col < 0) { col = COLS - 1; row = (row - 1 + TILE_ROWS) % TILE_ROWS; }
    selectorIndex = row * COLS + col;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    int row = getTileRow() + 1;
    if (row >= TILE_ROWS) {
      // Past the bottom tile row → open the full-screen book carousel
      if (bookCount > 0) {
        if (carouselIndex >= bookCount) carouselIndex = 0;
        carouselMode = true;
        requestUpdate();
      }
    } else {
      selectorIndex = row * COLS + getTileCol();
      requestUpdate();
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    int row = getTileRow() - 1;
    if (row >= 0) {
      selectorIndex = row * COLS + getTileCol();
      requestUpdate();
    }
  }

  // Periodic info refresh — only redraw if visible values changed
  if (millis() - lastInfoRefresh > INFO_REFRESH_MS) {
    uint32_t oldHeap = freeHeap;
    bool oldWifi = wifiConnected;
    refreshSystemInfo();
    // Only trigger e-ink refresh if KB-level heap changed or wifi status changed
    bool heapChanged = (freeHeap / 1024) != (oldHeap / 1024);
    if (heapChanged || (wifiConnected != oldWifi)) {
      requestUpdate();
    }
  }

  // === CONFIRM: open category ===
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    std::unique_ptr<Activity> app;
    switch (selectorIndex) {
        case 0: {
          std::vector<AppCategoryActivity::AppEntry> e = {
              {tr(STR_WIFI_CONNECT), "Scan, connect, saved networks", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiConnectActivity>(r, m); }},
              {"Bluetooth", "Scan nearby BLE devices", UIIcon::Hotspot, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BleScannerActivity>(r, m); }},
              {tr(STR_WIFI_SCANNER), "APs, signal, channels", UIIcon::Wifi, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<WifiScannerActivity>(r, m); }},
              {"Calculator", "Basic calculator", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CalculatorActivity>(r, m); }},
              {"Clock", "NTP clock / stopwatch / pomodoro", UIIcon::Recent, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ClockActivity>(r, m); }},
              {tr(STR_UNIT_CONVERTER), "Convert between units", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<UnitConverterActivity>(r, m); }},
              {"File Browser", "Browse files on SD card", UIIcon::Folder, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SdFileBrowserActivity>(r, m); }},
              {"WiFi Transfer", "Upload/download via WiFi", UIIcon::Transfer, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CrossPointWebServerActivity>(r, m); }},
          };
          app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Apps", std::move(e), false, 0);
          break;
        }
        case 1: {
          std::vector<AppCategoryActivity::AppEntry> e = {
              {"Casino", "Slots, blackjack, roulette + lootbox", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CasinoActivity>(r, m); }, false, []() -> bool { return Storage.exists("/shortbread/casino.dat"); }},
              {tr(STR_MINESWEEPER), "Classic minesweeper", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MinesweeperActivity>(r, m); }},
              {tr(STR_SUDOKU), "Number puzzle", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SudokuActivity>(r, m); }},
              {tr(STR_CHESS), "Play against the device", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ChessActivity>(r, m); }},
              {tr(STR_SNAKE), "Classic snake game", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SnakeActivity>(r, m); }},
              {tr(STR_TETRIS), "Block stacking", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TetrisActivity>(r, m); }},
              {"Maze", "Navigate random mazes", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MazeActivity>(r, m); }},
              {tr(STR_DICE_ROLLER), "Roll dice with animation", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DiceRollerActivity>(r, m); }},
              {tr(STR_GAME_OF_LIFE), "Conway's cellular automaton", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<GameOfLifeActivity>(r, m); }},
              {tr(STR_VORONOI), "Generate Voronoi patterns", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<VoronoiActivity>(r, m); }},
              {"Matrix Rain", "The Matrix digital rain effect", UIIcon::File, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MatrixRainActivity>(r, m); }},
          };
          app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, tr(STR_GAMES), std::move(e), false, 1);
          break;
        }
        case 2: {
          std::vector<AppCategoryActivity::AppEntry> e = {
              {"Open Book", "Browse and open an ebook", UIIcon::Book, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<FileBrowserActivity>(r, m); }},
              {"Recent Books", "Continue where you left off", UIIcon::Recent, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<RecentBooksActivity>(r, m); }},
              {"OPDS Browser", "Download books from OPDS servers", UIIcon::Library, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<OpdsBookBrowserActivity>(r, m); }},
              {"Reading Stats", "Pages read, streaks, progress", UIIcon::Book, [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ReadingStatsActivity>(r, m); }},
          };
          app = std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Reader", std::move(e), false, 2);
          break;
        }
        case 3: {
          app = std::make_unique<SettingsActivity>(renderer, mappedInput);
          break;
        }
      }
    if (app) activityManager.pushActivity(std::move(app));
  }

  // Back button ignored on main screen — use Power button to sleep
}

void AppsMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  constexpr int sidePad = 14;
  // Cover thumbnails are generated tall enough that even a full-width cover only downscales
  // (drawBitmap never upscales). The same thumb is reused for the bottom preview and the carousel.
  coverThumbH = ((pageWidth - sidePad * 2) * 3) / 2 + 50;

  if (carouselMode) {
    drawCarousel();
    renderer.displayBuffer();
    // Lazily generate metadata + covers for the visible window after painting, so the
    // first frame appears immediately and only redraws once art is ready.
    ensureWindow(carouselIndex);
    return;
  }

  drawStatusBar();

  constexpr int statusBarH = 40;
  constexpr int buttonHintsH = 40;
  constexpr int tileGap = 6;
  constexpr int gridTop = statusBarH + 8;
  const int pageBottom = pageHeight - buttonHintsH - 4;
  const int totalH = pageBottom - gridTop;

  // Top 44% for tiles (2×2)
  const int tileSectionH = totalH * 44 / 100;
  const int tileW = (pageWidth - sidePad * 2 - tileGap) / COLS;
  const int tileH = (tileSectionH - tileGap) / TILE_ROWS;

  for (int i = 0; i < ITEM_COUNT; i++) {
    int row = i / COLS;
    int col = i % COLS;
    int x = sidePad + col * (tileW + tileGap);
    int y = gridTop + row * (tileH + tileGap);
    drawTile(i, x, y, tileW, tileH, i == selectorIndex);
  }

  // Divider
  const int divY = gridTop + tileSectionH + 6;
  renderer.drawLine(sidePad, divY, pageWidth - sidePad, divY, true);

  // Bottom section — last-read book preview (Down expands into the full-screen carousel)
  const int recentsTop = divY + 8;
  const int recentsH = pageBottom - recentsTop;
  drawRecentBooks(sidePad, recentsTop, pageWidth - sidePad * 2, recentsH);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "^", "v");

  renderer.displayBuffer();

  // The bottom preview only needs the one last-selected book; load its meta+cover lazily.
  if (carouselIndex < (int)books.size()) {
    ensureMeta(carouselIndex);
    if (ensureCover(carouselIndex)) requestUpdate();
  }
}

bool AppsMenuActivity::drawCoverScaled(const CarouselBook& book, int x, int y, int boxW, int boxH,
                                      uint8_t opacity, bool cropFill) const {
  if (opacity == CrossPointSettings::COVER_OFF || book.coverBmpPath.empty() || coverThumbH <= 0)
    return false;

  std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverThumbH);
  FsFile coverFile;
  if (!Storage.openFileForRead("APM", thumbPath, coverFile)) return false;

  Bitmap bitmap(coverFile);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    coverFile.close();
    return false;
  }

  float cropY = 0.0f;
  if (cropFill) {
    const int bmpW = bitmap.getWidth();
    const int bmpH = bitmap.getHeight();
    if (bmpW > 0 && bmpH > 0) {
      const float widthScale = (float)boxW / (float)bmpW;
      const float scaledH = bmpH * widthScale;
      if (scaledH > boxH) cropY = 1.0f - ((float)boxH / scaledH);
    }
  }
  renderer.drawBitmap(bitmap, x, y, boxW, boxH, 0.0f, cropY);
  coverFile.close();

  // Fade overlay — clamped to the screen so off-screen "peek" slivers stay safe.
  if (opacity == CrossPointSettings::COVER_LIGHT || opacity == CrossPointSettings::COVER_MEDIUM) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(renderer.getScreenWidth(), x + boxW);
    const int y1 = std::min(renderer.getScreenHeight(), y + boxH);
    for (int fy = y0; fy < y1; fy++)
      for (int fx = x0; fx < x1; fx++) {
        const bool keep = (opacity == CrossPointSettings::COVER_LIGHT) ? (fx % 2 == 0 && fy % 2 == 0)
                                                                       : ((fx + fy) % 2 == 0);
        if (!keep) renderer.drawPixel(fx, fy, false);
      }
  }
  return true;
}

void AppsMenuActivity::drawRecentBooks(int x, int y, int w, int h) const {
  // Section header + a small "browse" cue (Down expands into the carousel)
  renderer.drawText(SMALL_FONT_ID, x, y, "CONTINUE READING", true, EpdFontFamily::BOLD);
  if (!books.empty()) {
    const char* hint = "v browse";
    const int hintW = renderer.getTextWidth(SMALL_FONT_ID, hint);
    renderer.drawText(SMALL_FONT_ID, x + w - hintW, y, hint);
  }
  const int headerH = renderer.getLineHeight(SMALL_FONT_ID) + 4;

  if (books.empty()) {
    renderer.drawText(SMALL_FONT_ID, x, y + headerH + 4, "No books on SD card");
    return;
  }

  const int booksTop = y + headerH;
  const int booksH = h - headerH;
  const int titleLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int authorLineH = renderer.getLineHeight(SMALL_FONT_ID);
  constexpr int pad = 8;

  // Preview shows the last-selected book (whatever was last centered in the carousel)
  const int showIdx = (carouselIndex < (int)books.size()) ? carouselIndex : 0;
  const CarouselBook& shown = books[showIdx];

  renderer.drawRect(x, booksTop, w, booksH, true);

  // Cover fills the section
  drawCoverScaled(shown, x + 1, booksTop + 1, w - 2, booksH - 2, SETTINGS.coverOpacity, true);

  // Book info — pinned to bottom of section
  const int infoH = titleLineH + authorLineH + 6;
  const int infoY = booksTop + booksH - infoH - pad;
  const std::string& title = shown.title.empty() ? shown.path : shown.title;
  std::string displayTitle = title;
  const int maxW = w - pad * 2;
  while (displayTitle.size() > 4 && renderer.getTextWidth(UI_10_FONT_ID, displayTitle.c_str()) > maxW)
    displayTitle.resize(displayTitle.size() - 4);
  if (displayTitle.size() < title.size()) displayTitle += "...";

  renderer.drawText(UI_10_FONT_ID, x + pad, infoY, displayTitle.c_str(), true, EpdFontFamily::BOLD);

  if (!shown.author.empty())
    renderer.drawText(SMALL_FONT_ID, x + pad, infoY + titleLineH + 2, shown.author.c_str(), true);

  if (shown.progress[0] != '\0') {
    int pW = renderer.getTextWidth(SMALL_FONT_ID, shown.progress);
    renderer.drawText(SMALL_FONT_ID, x + w - pad - pW, infoY + titleLineH + 2, shown.progress, true);
  }

  // Book index indicator (e.g. "2 / 12") top-right
  if ((int)books.size() > 1) {
    char idxStr[12];
    snprintf(idxStr, sizeof(idxStr), "%d / %d", showIdx + 1, (int)books.size());
    int idxW = renderer.getTextWidth(SMALL_FONT_ID, idxStr);
    renderer.drawText(SMALL_FONT_ID, x + w - pad - idxW, booksTop + pad, idxStr, true);
  }
}

void AppsMenuActivity::drawCarousel() const {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight();

  drawStatusBar();

  constexpr int statusBarH = 40;
  constexpr int buttonHintsH = 40;
  const int top = statusBarH + 10;
  const int bottom = H - buttonHintsH - 4;

  const int bookCount = (int)books.size();
  if (bookCount == 0 || carouselIndex >= bookCount) {
    renderer.drawCenteredText(UI_12_FONT_ID, (top + bottom) / 2, "No books on SD card", true);
    GUI.drawSideButtonHints(renderer, "^ home", "");
    return;
  }

  const CarouselBook& book = books[carouselIndex];

  // Reserve space below the cover for title / author / progress
  const int titleLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int authorLineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int infoH = titleLineH + authorLineH + 14;

  // Centered cover, as large as possible while leaving an edge gap for the adjacent-book peeks.
  // Width is the usual binding constraint on a portrait screen, so push it to the peek margins.
  constexpr int peek = 26;
  const int maxCoverW = W - 2 * peek - 12;
  int coverW = maxCoverW;
  int coverH = coverW * 3 / 2;
  const int maxCoverH = (bottom - top) - infoH;
  if (coverH > maxCoverH) {
    coverH = maxCoverH;
    coverW = coverH * 2 / 3;
  }
  const int coverX = (W - coverW) / 2;
  const int coverY = top + ((bottom - top - infoH) - coverH) / 2;

  // Adjacent books peeking in from the screen edges (same scale, clipped by the screen)
  if (carouselIndex > 0)
    drawCoverScaled(books[carouselIndex - 1], peek - coverW, coverY, coverW, coverH,
                    CrossPointSettings::COVER_FULL, false);
  if (carouselIndex < bookCount - 1)
    drawCoverScaled(books[carouselIndex + 1], W - peek, coverY, coverW, coverH,
                    CrossPointSettings::COVER_FULL, false);

  // Center cover (full opacity) with a border frame
  renderer.drawRect(coverX - 2, coverY - 2, coverW + 4, coverH + 4, true);
  const bool drew =
      drawCoverScaled(book, coverX, coverY, coverW, coverH, CrossPointSettings::COVER_FULL, true);
  if (!drew) {
    // No cover available — show the title centered inside the frame as a fallback
    renderer.fillRect(coverX, coverY, coverW, coverH, false);
    renderer.drawRect(coverX, coverY, coverW, coverH, true);
    const std::string& t = book.title.empty() ? book.path : book.title;
    renderer.drawCenteredText(UI_10_FONT_ID, coverY + coverH / 2, t.c_str(), true);
  }

  // Title / author / progress, centered below the cover
  int infoY = coverY + coverH + 8;
  const std::string& title = book.title.empty() ? book.path : book.title;
  std::string displayTitle = title;
  const int maxW = W - 40;
  while (displayTitle.size() > 4 && renderer.getTextWidth(UI_12_FONT_ID, displayTitle.c_str()) > maxW)
    displayTitle.resize(displayTitle.size() - 4);
  if (displayTitle.size() < title.size()) displayTitle += "...";
  renderer.drawCenteredText(UI_12_FONT_ID, infoY, displayTitle.c_str(), true, EpdFontFamily::BOLD);

  int below = infoY + titleLineH + 2;
  if (!book.author.empty()) {
    renderer.drawCenteredText(SMALL_FONT_ID, below, book.author.c_str(), true);
    below += authorLineH + 2;
  }
  if (book.progress[0] != '\0')
    renderer.drawCenteredText(SMALL_FONT_ID, below, book.progress, true);

  // Position indicator "2 / 12" just under the status bar
  if (bookCount > 1) {
    char idxStr[12];
    snprintf(idxStr, sizeof(idxStr), "%d / %d", carouselIndex + 1, bookCount);
    renderer.drawCenteredText(SMALL_FONT_ID, top - 2, idxStr, true);
  }

  // Button hints: Up = home, Left/Right = browse, Confirm = open
  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "^ home", "");
}

void AppsMenuActivity::refreshSystemInfo() {
  freeHeap = esp_get_free_heap_size();
  uptimeSeconds = (unsigned long)(esp_timer_get_time() / 1000000LL);
  batteryPercent = (uint8_t)powerManager.getBatteryPercentage();
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  wifiRssi = wifiConnected ? (int8_t)WiFi.RSSI() : 0;
  lastInfoRefresh = millis();

  unsigned long hrs = uptimeSeconds / 3600;
  unsigned long mins = (uptimeSeconds % 3600) / 60;
  if (hrs > 0) {
    snprintf(uptimeStr, sizeof(uptimeStr), "%luh%02lum", hrs, mins);
  } else {
    snprintf(uptimeStr, sizeof(uptimeStr), "%lum", mins);
  }

}

void AppsMenuActivity::loadLastUsed() {
  for (int i = 0; i < ITEM_COUNT; i++) {
    lastUsedName[i][0] = '\0';
    char path[40];
    snprintf(path, sizeof(path), "/shortbread/lastused_%d.txt", i);
    FsFile file;
    if (Storage.openFileForRead("APPS", path, file)) {
      int len = file.read((uint8_t*)lastUsedName[i], 31);
      if (len > 0) {
        lastUsedName[i][len] = '\0';
        // Strip trailing newline
        if (len > 0 && lastUsedName[i][len - 1] == '\n') {
          lastUsedName[i][len - 1] = '\0';
        }
      }
      file.close();
    }
  }
}

void AppsMenuActivity::drawStatusBar() const {
  const auto pageWidth = renderer.getScreenWidth();
  constexpr int pad = 14;

  // Left: branding
  renderer.drawText(UI_12_FONT_ID, pad, 10, "shortbread.", true, EpdFontFamily::BOLD);

  // Right side: build right-to-left to avoid overlap
  constexpr int sep = 8;  // extra gap between items
  const int pipeW = renderer.getTextWidth(SMALL_FONT_ID, " - ") + sep;
  int rightX = pageWidth - pad;

  // Uptime (rightmost)
  int uptimeW = renderer.getTextWidth(SMALL_FONT_ID, uptimeStr);
  renderer.drawText(SMALL_FONT_ID, rightX - uptimeW, 14, uptimeStr);
  rightX -= uptimeW + sep;

  // Separator
  renderer.drawText(SMALL_FONT_ID, rightX - pipeW + sep, 14, " - ");
  rightX -= pipeW;

  // Heap as % free
  char heapStr[8];
  uint32_t totalHeap = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  uint8_t heapPct = totalHeap ? (uint8_t)(freeHeap * 100 / totalHeap) : 0;
  snprintf(heapStr, sizeof(heapStr), "%u%%", heapPct);
  int heapW = renderer.getTextWidth(SMALL_FONT_ID, heapStr);
  renderer.drawText(SMALL_FONT_ID, rightX - heapW, 14, heapStr);
  rightX -= heapW + sep;

  // Separator
  renderer.drawText(SMALL_FONT_ID, rightX - pipeW + sep, 14, " - ");
  rightX -= pipeW;

  // WiFi: show "WiFi" text when connected, omit entirely when not
  if (wifiConnected) {
    constexpr const char* wifiLabel = "WiFi";
    int wifiLabelW = renderer.getTextWidth(SMALL_FONT_ID, wifiLabel);
    renderer.drawText(SMALL_FONT_ID, rightX - wifiLabelW, 14, wifiLabel);
    rightX -= wifiLabelW + sep;
    renderer.drawText(SMALL_FONT_ID, rightX - pipeW + sep, 14, " - ");
    rightX -= pipeW;
  }

  // Battery — drawBatteryRight draws percentage text at rect.y, icon at rect.y+6
  GUI.drawBatteryRight(renderer, Rect{rightX - 16, 14, 15, 12});

  // Separator line
  renderer.drawLine(pad, 38, pageWidth - pad, 38, true);
}

void AppsMenuActivity::drawTile(int index, int x, int y, int w, int h, bool selected) const {
  if (selected) {
    renderer.fillRect(x, y, w, h, true);
  } else {
    renderer.drawRect(x, y, w, h, true);
  }

  constexpr int pad = 10;

  // --- Zone 1: Top — category name + subtitle ---
  int nameY = y + pad;
  const char* name = "";
  const char* subtitle = "";
  int appCount = 0;

  switch (index) {
    case 0: name = "APPS";     subtitle = "Network & Utilities"; appCount = 8;  break;
    case 1: name = "GAMES";    subtitle = "Entertainment";      appCount = 11; break;
    case 2: name = "READER";   subtitle = "Books & OPDS";       appCount = 4;  break;
    case 3: name = "SETTINGS"; subtitle = "Display · Reader · Controls"; appCount = 0; break;
  }

  renderer.drawText(UI_12_FONT_ID, x + pad, nameY, name, !selected, EpdFontFamily::BOLD);
  nameY += renderer.getLineHeight(UI_12_FONT_ID) + 2;
  renderer.drawText(SMALL_FONT_ID, x + pad, nameY, subtitle, !selected);

  // --- Zone 2: Bottom-right — app count (skip for modules with 0) ---
  int countY = y + h - pad - renderer.getLineHeight(SMALL_FONT_ID);
  if (appCount > 0) {
    char countStr[16];
    snprintf(countStr, sizeof(countStr), "%d apps", appCount);
    int countW = renderer.getTextWidth(SMALL_FONT_ID, countStr);
    renderer.drawText(SMALL_FONT_ID, x + w - pad - countW, countY, countStr, !selected);
  }

  // --- Badge indicator (top-right corner of tile) ---
  int badge = 0;
  bool showBang = false;
  switch (index) {
    default: break;
  }

  if (badge > 0 || showBang) {
    int badgeX = x + w - 24;
    int badgeY = y + 6;
    // Draw badge background (inverted relative to tile)
    renderer.fillRect(badgeX, badgeY, 16, 16, !selected);
    // Draw badge text
    char badgeStr[4];
    if (showBang) {
      snprintf(badgeStr, sizeof(badgeStr), "!");
    } else {
      snprintf(badgeStr, sizeof(badgeStr), "%d", badge);
    }
    int bw = renderer.getTextWidth(SMALL_FONT_ID, badgeStr);
    renderer.drawText(SMALL_FONT_ID, badgeX + 8 - bw / 2, badgeY + 1, badgeStr, selected);
  }

  // --- Zone 3: Bottom-left — live status (selected tile only) ---
  if (selected) {
    char statusStr[48] = "";
    switch (index) {
      case 0:  // NETWORK
        snprintf(statusStr, sizeof(statusStr), wifiConnected ? "WiFi: on" : "WiFi: off");
        break;
      case 1:  // TOOLS
        snprintf(statusStr, sizeof(statusStr), "Heap: %luK", (unsigned long)(freeHeap / 1024));
        break;
      case 2:  // GAMES
      case 3:  // READER
        if (lastUsedName[index][0] != '\0') {
          snprintf(statusStr, sizeof(statusStr), "Last: %s", lastUsedName[index]);
        }
        break;
      default:
        break;
    }
    if (statusStr[0] != '\0') {
      int statusY = countY - renderer.getLineHeight(SMALL_FONT_ID) - 4;
      renderer.drawText(SMALL_FONT_ID, x + pad, statusY, statusStr, !selected);
    }
  }
}

