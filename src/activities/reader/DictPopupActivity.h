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
  int scrollLine = 0;
  bool needsRender = true;

  void wrapDefinition(int maxWidth);

 public:
  explicit DictPopupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             std::string headwordIn, std::string definitionIn)
      : Activity("DictPopup", renderer, mappedInput),
        headword(std::move(headwordIn)),
        definition(std::move(definitionIn)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
