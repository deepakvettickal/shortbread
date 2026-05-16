#pragma once

#include <string>
#include <vector>

#include "../Activity.h"

// Modal popup showing a dictionary definition for a single word.
// Renders inside a centered box; Back / Confirm dismiss.
class DictPopupActivity final : public Activity {
  std::string headword;
  std::string definition;
  std::vector<std::string> lines;
  int wordY = 0;  // y of the highlighted word; popup sits in the opposite half
  int scrollLine = 0;
  bool needsRender = true;

  void wrapDefinition(int maxWidth);

 public:
  explicit DictPopupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             std::string headwordIn, std::string definitionIn, int wordYIn)
      : Activity("DictPopup", renderer, mappedInput),
        headword(std::move(headwordIn)),
        definition(std::move(definitionIn)),
        wordY(wordYIn) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
