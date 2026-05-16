#include "DictPopupActivity.h"

#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "fontIds.h"

namespace {
constexpr int MARGIN = 20;
constexpr int PADDING = 10;
constexpr int TITLE_FONT = UI_10_FONT_ID;
constexpr int BODY_FONT = SMALL_FONT_ID;
}  // namespace

void DictPopupActivity::wrapDefinition(int maxWidth) {
  lines.clear();
  std::string current;
  std::string tok;
  auto flushWord = [&]() {
    if (tok.empty()) return;
    std::string candidate = current.empty() ? tok : current + " " + tok;
    if (renderer.getTextWidth(BODY_FONT, candidate.c_str()) <= maxWidth) {
      current = candidate;
    } else {
      if (!current.empty()) {
        lines.push_back(current);
        current.clear();
      }
      // If even single word too wide, push as-is.
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
      flushWord();
      if (!current.empty()) {
        lines.push_back(current);
        current.clear();
      }
      lines.push_back("");  // blank line separator between senses
    } else if (c == ' ' || c == '\t') {
      flushWord();
    } else {
      tok += c;
    }
  }
  flushWord();
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
  const int boxH = renderer.getScreenHeight() - 2 * MARGIN;
  const int bodyTop = MARGIN + PADDING + renderer.getLineHeight(TITLE_FONT) + 6;
  const int bodyBottom = MARGIN + boxH - PADDING;
  const int bodyLineHeight = renderer.getLineHeight(BODY_FONT);
  const int visibleLines = (bodyBottom - bodyTop) / bodyLineHeight;
  const int maxScroll = std::max(0, static_cast<int>(lines.size()) - visibleLines);

  if (mappedInput.wasReleased(MappedInputManager::Button::Down) && scrollLine < maxScroll) {
    scrollLine++;
    needsRender = true;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) && scrollLine > 0) {
    scrollLine--;
    needsRender = true;
    requestUpdate();
  }
}

void DictPopupActivity::render(RenderLock&&) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int boxX = MARGIN;
  const int boxY = MARGIN;
  const int boxW = screenW - 2 * MARGIN;
  const int boxH = screenH - 2 * MARGIN;
  const int innerX = boxX + PADDING;
  const int innerW = boxW - 2 * PADDING;

  if (lines.empty()) {
    wrapDefinition(innerW);
  }

  // White box with black border.
  renderer.fillRect(boxX, boxY, boxW, boxH, false);
  renderer.drawRect(boxX, boxY, boxW, boxH);
  renderer.drawRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2);

  // Title bar.
  const int titleH = renderer.getLineHeight(TITLE_FONT) + 4;
  renderer.fillRect(boxX + 2, boxY + 2, boxW - 4, titleH);  // black bar
  renderer.drawText(TITLE_FONT, innerX, boxY + PADDING, headword.c_str(),
                    /*black=*/false);  // white text on black bar

  // Body.
  const int bodyTop = boxY + PADDING + titleH;
  const int bodyBottom = boxY + boxH - PADDING;
  const int bodyLH = renderer.getLineHeight(BODY_FONT);
  int y = bodyTop;
  for (size_t i = scrollLine; i < lines.size(); ++i) {
    if (y + bodyLH > bodyBottom) break;
    renderer.drawText(BODY_FONT, innerX, y, lines[i].c_str());
    y += bodyLH;
  }

  // Scroll hint.
  const int maxScroll = std::max(0, static_cast<int>(lines.size()) -
                                       (bodyBottom - bodyTop) / bodyLH);
  if (maxScroll > 0) {
    char hint[32];
    snprintf(hint, sizeof(hint), "%d/%d", scrollLine + 1, maxScroll + 1);
    const int tw = renderer.getTextWidth(BODY_FONT, hint);
    renderer.drawText(BODY_FONT, boxX + boxW - PADDING - tw,
                      boxY + boxH - PADDING - bodyLH + 2, hint);
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  needsRender = false;
}
