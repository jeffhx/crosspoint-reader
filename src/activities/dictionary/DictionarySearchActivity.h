#pragma once

#include <StarDict.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"

class DictionarySearchActivity final : public Activity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  StarDict& dictionary;
  std::string searchText;
  std::vector<stardict::SearchResult> suggestions;
  int selectedSuggestion = -1;  // -1 = keyboard focused, 0+ = suggestion focused

  const std::function<void(const std::string&)> onSearch;
  const std::function<void(const stardict::SearchResult&)> onSelectSuggestion;
  const std::function<void()> onCancel;

  // Keyboard state
  int keyboardRow = 0;
  int keyboardCol = 0;
  bool shiftActive = false;

  // Keyboard layout (lowercase only, uppercase computed at runtime)
  static constexpr int NUM_ROWS = 4;
  static constexpr int KEYS_PER_ROW = 10;
  static const char* const keyboard[NUM_ROWS];

  // Special key positions (bottom row)
  static constexpr int SPECIAL_ROW = 3;
  static constexpr int SHIFT_COL = 0;
  static constexpr int SPACE_COL = 3;
  static constexpr int BACKSPACE_COL = 7;
  static constexpr int SEARCH_COL = 9;

  void updateSuggestions();
  char getSelectedChar() const;
  void handleKeyPress();
  int getRowLength(int row) const;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;

 public:
  DictionarySearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, StarDict& dictionary,
                           const std::function<void(const std::string&)>& onSearch,
                           const std::function<void(const stardict::SearchResult&)>& onSelectSuggestion,
                           const std::function<void()>& onCancel, const std::string& initialText = "");
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
