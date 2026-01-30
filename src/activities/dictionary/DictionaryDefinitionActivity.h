#pragma once

#include <StarDict.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"

class DictionaryDefinitionActivity final : public Activity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  StarDict& dictionary;
  stardict::SearchResult entry;
  std::string definition;
  std::vector<std::string> wrappedLines;  // Pre-wrapped lines for display
  int scrollOffset = 0;                   // Current scroll position (line index)

  const std::function<void()> onBack;
  const std::function<void()> onNewSearch;

  void loadDefinition();
  void wrapText();
  int getVisibleLines() const;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;

 public:
  DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, StarDict& dictionary,
                               const stardict::SearchResult& entry, const std::function<void()>& onBack,
                               const std::function<void()>& onNewSearch);
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
