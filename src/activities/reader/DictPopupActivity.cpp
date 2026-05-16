#include "DictPopupActivity.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "fontIds.h"

namespace {
constexpr int PADDING = 10;
constexpr int HEADWORD_FONT = SMALL_FONT_ID;
constexpr int BODY_FONT = SMALL_FONT_ID;
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
      lines.push_back("");  // blank separator between senses
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

  // Popup spans nearly full width and exactly half the height, in the half
  // opposite the highlighted word so the word remains visible.
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

  // White fill, double border for visibility on top of book content.
  renderer.fillRect(boxX, boxY, boxW, boxH, false);
  renderer.drawRect(boxX, boxY, boxW, boxH);
  renderer.drawRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2);

  // Headword in italic at top-left, small. Separator line below.
  const int headLH = renderer.getLineHeight(HEADWORD_FONT);
  const int headY = boxY + PADDING;
  renderer.drawText(HEADWORD_FONT, innerX, headY, headword.c_str(), true, EpdFontFamily::ITALIC);
  const int sepY = headY + headLH + 2;
  renderer.drawLine(boxX + PADDING, sepY, boxX + boxW - PADDING, sepY, true);

  // Body lines.
  const int bodyTop = sepY + 6;
  const int bodyBottom = boxY + boxH - PADDING;
  const int bodyLH = renderer.getLineHeight(BODY_FONT);
  const int visibleLines = std::max(1, (bodyBottom - bodyTop) / bodyLH);
  const int maxScroll = std::max(0, static_cast<int>(lines.size()) - visibleLines);
  if (scrollLine > maxScroll) scrollLine = maxScroll;

  int y = bodyTop;
  for (size_t i = scrollLine; i < lines.size(); ++i) {
    if (y + bodyLH > bodyBottom) break;
    renderer.drawText(BODY_FONT, innerX, y, lines[i].c_str());
    y += bodyLH;
  }

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
