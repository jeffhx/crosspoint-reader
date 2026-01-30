#include "DictionarySearchActivity.h"

#include <GfxRenderer.h>

#include <cctype>

#include "MappedInputManager.h"
#include "fontIds.h"

// Keyboard layout - only lowercase needed, uppercase computed at runtime
const char* const DictionarySearchActivity::keyboard[NUM_ROWS] = {
    "qwertyuiop",  // Row 0: 10 keys
    "asdfghjkl",   // Row 1: 9 keys
    "zxcvbnm",     // Row 2: 7 keys
    "^  _   <- S"  // Row 3: Shift, space(3), backspace(2), Search
};

DictionarySearchActivity::DictionarySearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   StarDict& dictionary,
                                                   const std::function<void(const std::string&)>& onSearch,
                                                   const std::function<void(const stardict::SearchResult&)>& onSelectSuggestion,
                                                   const std::function<void()>& onCancel, const std::string& initialText)
    : Activity("DictionarySearch", renderer, mappedInput),
      dictionary(dictionary),
      searchText(initialText),
      onSearch(onSearch),
      onSelectSuggestion(onSelectSuggestion),
      onCancel(onCancel) {}

void DictionarySearchActivity::taskTrampoline(void* param) {
  auto* self = static_cast<DictionarySearchActivity*>(param);
  self->displayTaskLoop();
}

void DictionarySearchActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  keyboardRow = 0;
  keyboardCol = 0;
  shiftActive = false;
  selectedSuggestion = -1;

  // Get initial suggestions if we have text
  if (!searchText.empty()) {
    updateSuggestions();
  }

  updateRequired = true;

  xTaskCreate(&DictionarySearchActivity::taskTrampoline, "DictSearchTask", 4096, this, 1, &displayTaskHandle);
}

void DictionarySearchActivity::onExit() {
  Activity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  suggestions.clear();
}

int DictionarySearchActivity::getRowLength(int row) const {
  if (row < 0 || row >= NUM_ROWS) return 0;

  switch (row) {
    case 0:
      return 10;  // qwertyuiop
    case 1:
      return 9;  // asdfghjkl
    case 2:
      return 7;  // zxcvbnm
    case 3:
      return 10;  // Special row (conceptually 10 positions)
    default:
      return 0;
  }
}

char DictionarySearchActivity::getSelectedChar() const {
  if (keyboardRow < 0 || keyboardRow >= NUM_ROWS) return '\0';

  const int len = getRowLength(keyboardRow);
  if (keyboardCol < 0 || keyboardCol >= len) return '\0';

  // Special row handling
  if (keyboardRow == SPECIAL_ROW) {
    return '\0';  // Special keys handled separately
  }

  // Get char from lowercase keyboard, apply toupper if shift active
  char c = keyboard[keyboardRow][keyboardCol];
  return shiftActive ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
}

void DictionarySearchActivity::handleKeyPress() {
  if (keyboardRow == SPECIAL_ROW) {
    // Handle special keys
    if (keyboardCol <= SHIFT_COL) {
      // Shift toggle
      shiftActive = !shiftActive;
    } else if (keyboardCol >= SPACE_COL && keyboardCol < BACKSPACE_COL) {
      // Space
      searchText += ' ';
      updateSuggestions();
    } else if (keyboardCol >= BACKSPACE_COL && keyboardCol < SEARCH_COL) {
      // Backspace
      if (!searchText.empty()) {
        // Handle UTF-8: remove last character properly
        size_t len = searchText.size();
        while (len > 0 && (searchText[len - 1] & 0xC0) == 0x80) {
          --len;  // Skip continuation bytes
        }
        if (len > 0) {
          --len;  // Remove lead byte
        }
        searchText.resize(len);
        updateSuggestions();
      }
    } else {
      // Search button
      if (!searchText.empty()) {
        onSearch(searchText);
      }
    }
  } else {
    // Regular character
    const char c = getSelectedChar();
    if (c != '\0') {
      searchText += c;
      // Auto-disable shift after typing a letter
      if (shiftActive) {
        shiftActive = false;
      }
      updateSuggestions();
    }
  }
}

void DictionarySearchActivity::updateSuggestions() {
  suggestions.clear();
  if (!searchText.empty()) {
    dictionary.searchPrefix(searchText.c_str(), suggestions, 5);
  }
  selectedSuggestion = -1;  // Reset to keyboard
}

void DictionarySearchActivity::loop() {
  const bool upPressed = mappedInput.wasReleased(MappedInputManager::Button::Up);
  const bool downPressed = mappedInput.wasReleased(MappedInputManager::Button::Down);
  const bool leftPressed = mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool rightPressed = mappedInput.wasReleased(MappedInputManager::Button::Right);
  const bool confirmPressed = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  const bool backPressed = mappedInput.wasReleased(MappedInputManager::Button::Back);

  if (backPressed) {
    onCancel();
    return;
  }

  // Handle suggestion selection with Up from keyboard
  if (selectedSuggestion == -1) {
    // Currently on keyboard
    if (upPressed) {
      if (keyboardRow == 0 && !suggestions.empty()) {
        // Move to suggestions
        selectedSuggestion = static_cast<int>(suggestions.size()) - 1;
        updateRequired = true;
        return;
      } else if (keyboardRow > 0) {
        keyboardRow--;
        // Clamp column
        if (keyboardCol >= getRowLength(keyboardRow)) {
          keyboardCol = getRowLength(keyboardRow) - 1;
        }
        updateRequired = true;
        return;
      }
    }

    if (downPressed) {
      if (keyboardRow < NUM_ROWS - 1) {
        keyboardRow++;
        if (keyboardCol >= getRowLength(keyboardRow)) {
          keyboardCol = getRowLength(keyboardRow) - 1;
        }
        updateRequired = true;
      }
      return;
    }

    if (leftPressed) {
      if (keyboardCol > 0) {
        keyboardCol--;
        updateRequired = true;
      }
      return;
    }

    if (rightPressed) {
      if (keyboardCol < getRowLength(keyboardRow) - 1) {
        keyboardCol++;
        updateRequired = true;
      }
      return;
    }

    if (confirmPressed) {
      handleKeyPress();
      updateRequired = true;
      return;
    }
  } else {
    // Currently on suggestions
    if (upPressed) {
      if (selectedSuggestion > 0) {
        selectedSuggestion--;
      }
      updateRequired = true;
      return;
    }

    if (downPressed) {
      if (selectedSuggestion < static_cast<int>(suggestions.size()) - 1) {
        selectedSuggestion++;
      } else {
        // Move back to keyboard
        selectedSuggestion = -1;
      }
      updateRequired = true;
      return;
    }

    if (confirmPressed && selectedSuggestion >= 0 && selectedSuggestion < static_cast<int>(suggestions.size())) {
      onSelectSuggestion(suggestions[selectedSuggestion]);
      return;
    }

    if (leftPressed || rightPressed) {
      // Move back to keyboard
      selectedSuggestion = -1;
      updateRequired = true;
      return;
    }
  }
}

void DictionarySearchActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void DictionarySearchActivity::render() const {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  // Title
  renderer.drawCenteredText(UI_12_FONT_ID, 10, "Dictionary Search", true, EpdFontFamily::BOLD);

  // Search input field
  constexpr int inputY = 45;
  constexpr int inputHeight = 30;
  constexpr int inputMargin = 20;

  renderer.drawRect(inputMargin, inputY, pageWidth - 2 * inputMargin, inputHeight);

  // Display search text with cursor
  std::string displayText = searchText + "_";
  const int maxTextWidth = pageWidth - 2 * inputMargin - 10;
  while (renderer.getTextWidth(UI_10_FONT_ID, displayText.c_str()) > maxTextWidth && displayText.size() > 1) {
    displayText = "..." + displayText.substr(4);
  }
  renderer.drawText(UI_10_FONT_ID, inputMargin + 5, inputY + 5, displayText.c_str());

  // Suggestions area
  constexpr int suggestionStartY = 85;
  constexpr int suggestionHeight = 22;

  if (!suggestions.empty()) {
    renderer.drawText(UI_10_FONT_ID, inputMargin, suggestionStartY - 15, "Suggestions:");

    for (size_t i = 0; i < suggestions.size(); i++) {
      const int y = suggestionStartY + static_cast<int>(i) * suggestionHeight;
      const bool selected = (static_cast<int>(i) == selectedSuggestion);

      if (selected) {
        renderer.fillRect(inputMargin, y - 2, pageWidth - 2 * inputMargin, suggestionHeight);
      }

      auto text = renderer.truncatedText(UI_10_FONT_ID, suggestions[i].word.c_str(), pageWidth - 2 * inputMargin - 10);
      renderer.drawText(UI_10_FONT_ID, inputMargin + 5, y, text.c_str(), !selected);
    }
  }

  // Keyboard
  const int keyboardStartY = pageHeight - 130;
  constexpr int keyWidth = 22;
  constexpr int keyHeight = 24;
  constexpr int keySpacing = 2;

  for (int row = 0; row < NUM_ROWS; row++) {
    const char* keys = keyboard[row];  // Always use lowercase, apply toupper when drawing
    const int rowLen = getRowLength(row);
    const int rowWidth = rowLen * (keyWidth + keySpacing) - keySpacing;
    int startX = (pageWidth - rowWidth) / 2;

    if (row == SPECIAL_ROW) {
      // Special row with shift, space, backspace, search
      const int specialY = keyboardStartY + row * (keyHeight + keySpacing);

      // Shift key
      const bool shiftSelected = (selectedSuggestion == -1 && keyboardRow == row && keyboardCol <= SHIFT_COL);
      const int shiftWidth = keyWidth;
      if (shiftSelected) {
        renderer.fillRect(startX, specialY, shiftWidth, keyHeight);
      } else {
        renderer.drawRect(startX, specialY, shiftWidth, keyHeight);
      }
      const char* shiftLabel = shiftActive ? "^" : "^";
      renderer.drawText(UI_10_FONT_ID, startX + 6, specialY + 4, shiftLabel, !shiftSelected);

      // Space key
      const bool spaceSelected =
          (selectedSuggestion == -1 && keyboardRow == row && keyboardCol >= SPACE_COL && keyboardCol < BACKSPACE_COL);
      const int spaceX = startX + shiftWidth + keySpacing + keyWidth + keySpacing;
      const int spaceWidth = 4 * (keyWidth + keySpacing);
      if (spaceSelected) {
        renderer.fillRect(spaceX, specialY, spaceWidth, keyHeight);
      } else {
        renderer.drawRect(spaceX, specialY, spaceWidth, keyHeight);
      }
      renderer.drawText(UI_10_FONT_ID, spaceX + spaceWidth / 2 - 20, specialY + 4, "SPACE", !spaceSelected);

      // Backspace key
      const bool bsSelected =
          (selectedSuggestion == -1 && keyboardRow == row && keyboardCol >= BACKSPACE_COL && keyboardCol < SEARCH_COL);
      const int bsX = spaceX + spaceWidth + keySpacing;
      const int bsWidth = 2 * keyWidth;
      if (bsSelected) {
        renderer.fillRect(bsX, specialY, bsWidth, keyHeight);
      } else {
        renderer.drawRect(bsX, specialY, bsWidth, keyHeight);
      }
      renderer.drawText(UI_10_FONT_ID, bsX + 8, specialY + 4, "<-", !bsSelected);

      // Search key
      const bool searchSelected = (selectedSuggestion == -1 && keyboardRow == row && keyboardCol >= SEARCH_COL);
      const int searchX = bsX + bsWidth + keySpacing;
      const int searchWidth = 2 * keyWidth;
      if (searchSelected) {
        renderer.fillRect(searchX, specialY, searchWidth, keyHeight);
      } else {
        renderer.drawRect(searchX, specialY, searchWidth, keyHeight);
      }
      renderer.drawText(UI_10_FONT_ID, searchX + 4, specialY + 4, "GO", !searchSelected);

    } else {
      // Regular key row
      for (int col = 0; col < rowLen; col++) {
        const int x = startX + col * (keyWidth + keySpacing);
        const int y = keyboardStartY + row * (keyHeight + keySpacing);
        const bool selected = (selectedSuggestion == -1 && keyboardRow == row && keyboardCol == col);

        if (selected) {
          renderer.fillRect(x, y, keyWidth, keyHeight);
        } else {
          renderer.drawRect(x, y, keyWidth, keyHeight);
        }

        char c = keys[col];
        if (shiftActive) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        char keyStr[2] = {c, '\0'};
        const int textX = x + (keyWidth - renderer.getTextWidth(UI_10_FONT_ID, keyStr)) / 2;
        renderer.drawText(UI_10_FONT_ID, textX, y + 4, keyStr, !selected);
      }
    }
  }

  // Button hints
  const auto labels = mappedInput.mapLabels("Cancel", "Select", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
