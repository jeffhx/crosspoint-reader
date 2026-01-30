#pragma once

#include <StarDict.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"

class DictionaryResultsActivity final : public Activity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  StarDict& dictionary;
  std::string searchTerm;
  std::vector<stardict::SearchResult> results;
  int selectedIndex = 0;

  const std::function<void(const stardict::SearchResult&)> onSelectWord;
  const std::function<void()> onBack;
  const std::function<void()> onNewSearch;

  // Pagination helpers
  int getPageItems() const;
  int getCurrentPage() const;
  int getTotalPages() const;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;

 public:
  DictionaryResultsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, StarDict& dictionary,
                            const std::string& searchTerm, std::vector<stardict::SearchResult> results,
                            const std::function<void(const stardict::SearchResult&)>& onSelectWord,
                            const std::function<void()>& onBack, const std::function<void()>& onNewSearch);
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
