#include "DictPopupActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "fontIds.h"

namespace {
constexpr int PADDING = 8;
constexpr int TITLE_FONT = UI_10_FONT_ID;
constexpr int BODY_FONT = SMALL_FONT_ID;

// Detect POS prefix (e.g. "n.", "v.", "adj.", "adv.") at line start so it can
// be rendered in bold. Returns the length of the prefix (including trailing
// space) or 0 if none matched.
size_t posPrefixLen(const std::string& line) {
  static const char* kPrefixes[] = {"n. ", "v. ", "adj. ", "adv. "};
  for (const char* p : kPrefixes) {
    size_t n = std::strlen(p);
    if (line.size() >= n && line.compare(0, n, p) == 0) return n;
  }
  return 0;
}
}  // namespace

void DictPopupActivity::wrapDefinition(int maxWidth) {
  lines.clear();
  std::string current;
  std::string tok;
  auto flushTok = [&]() {
    if (tok.empty()) return;
    std::string candidate = current.empty() ? tok : current + " " + tok;
    if (renderer.getTextWidth(BODY_FONT, candidate.c_str()) <= maxWidth) {
      current = candidate;
    } else {
      if (!current.empty()) {
        lines.push_back(current);
        current.clear();
      }
      if (renderer.getTextWidth(BODY_FONT, tok.c_str()) <= maxWidth) {
        current = tok;
      } else {
        lines.push_back(tok);
      }
    }
    tok.clear();
  };
  for (char c : definition) {
    if (c == '\n') {
      flushTok();
      if (!current.empty()) {
        lines.push_back(current);
        current.clear();
      }
      lines.push_back("");  // blank separator
    } else if (c == ' ' || c == '\t') {
      flushTok();
    } else {
      tok += c;
    }
  }
  flushTok();
  if (!current.empty()) lines.push_back(current);
}

void DictPopupActivity::onEnter() {
  Activity::onEnter();
  needsRender = true;
  scrollLine = 0;
  requestUpdate(true);
}

void DictPopupActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
    return;
  }
  // Scroll if content overflows.
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    scrollLine++;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) && scrollLine > 0) {
    scrollLine--;
    requestUpdate();
  }
}

void DictPopupActivity::render(RenderLock&&) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  // Popup spans almost the full screen width and exactly half the screen
  // height, in the half opposite the selected word so the word stays visible.
  const int outerMargin = 10;
  const int boxW = screenW - 2 * outerMargin;
  const int boxX = outerMargin;
  const int halfH = screenH / 2;
  const int boxH = halfH - outerMargin;
  const bool wordInTopHalf = wordY < halfH;
  const int boxY = wordInTopHalf ? (screenH - boxH - outerMargin) : outerMargin;

  const int innerX = boxX + PADDING;
  const int innerW = boxW - 2 * PADDING;
  if (lines.empty()) {
    wrapDefinition(innerW);
  }

  const int titleH = renderer.getLineHeight(TITLE_FONT) + 6;
  const int bodyLH = renderer.getLineHeight(BODY_FONT);

  // White fill + double black border so it sits cleanly on top of book text.
  renderer.fillRect(boxX, boxY, boxW, boxH, false);
  renderer.drawRect(boxX, boxY, boxW, boxH);
  renderer.drawRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2);

  // Title bar (black with white text).
  renderer.fillRect(boxX + 2, boxY + 2, boxW - 4, titleH);
  renderer.drawText(TITLE_FONT, innerX, boxY + 4, headword.c_str(),
                    /*black=*/false, EpdFontFamily::BOLD);

  // Body lines with POS-prefix bolding.
  const int bodyTop = boxY + 2 + titleH + PADDING;
  const int bodyBottom = boxY + boxH - PADDING;
  const int visibleLines = std::max(1, (bodyBottom - bodyTop) / bodyLH);
  const int maxScroll = std::max(0, static_cast<int>(lines.size()) - visibleLines);
  if (scrollLine > maxScroll) scrollLine = maxScroll;

  int y = bodyTop;
  for (size_t i = scrollLine; i < lines.size(); ++i) {
    if (y + bodyLH > bodyBottom) break;
    const std::string& line = lines[i];
    size_t plen = posPrefixLen(line);
    if (plen > 0) {
      std::string prefix = line.substr(0, plen);
      std::string rest = line.substr(plen);
      renderer.drawText(BODY_FONT, innerX, y, prefix.c_str(), true, EpdFontFamily::BOLD);
      const int pw = renderer.getTextWidth(BODY_FONT, prefix.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(BODY_FONT, innerX + pw, y, rest.c_str());
    } else {
      renderer.drawText(BODY_FONT, innerX, y, line.c_str());
    }
    y += bodyLH;
  }

  // Scroll indicator.
  if (maxScroll > 0) {
    char hint[24];
    snprintf(hint, sizeof(hint), "%d/%d", scrollLine + 1, maxScroll + 1);
    const int tw = renderer.getTextWidth(BODY_FONT, hint);
    renderer.drawText(BODY_FONT, boxX + boxW - PADDING - tw,
                      boxY + boxH - PADDING - bodyLH + 2, hint);
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  needsRender = false;
}
