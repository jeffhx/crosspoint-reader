#pragma once

#include <StarDict.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <memory>
#include <vector>

#include "../ActivityWithSubactivity.h"

class DictionaryActivity final : public ActivityWithSubactivity {
 public:
  enum class Screen { Selection, Search, Results, Definition };

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  Screen currentScreen = Screen::Selection;
  std::unique_ptr<StarDict> currentDict;
  std::vector<std::string> availableDicts;  // Paths to dictionary folders
  std::vector<std::string> dictNames;       // Display names
  int selectedDictIndex = 0;

  std::string lastSearchTerm;
  std::vector<stardict::SearchResult> searchResults;
  int selectedResultIndex = 0;
  stardict::SearchResult selectedEntry;

  const std::function<void()> onGoHome;

  // Dictionary scanning
  void scanForDictionaries();
  void loadSelectedDictionary();

  // Screen transitions
  void showSearch();
  void showResults(const std::string& searchTerm);
  void showDefinition(const stardict::SearchResult& entry);

  // Rendering
  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
  void renderSelection() const;

  // Pagination helpers
  int getPageItems() const;
  int getCurrentPage() const;
  int getTotalPages() const;

 public:
  explicit DictionaryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              const std::function<void()>& onGoHome);
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
