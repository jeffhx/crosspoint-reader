#include "DictionaryDefinitionActivity.h"

#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "ScreenComponents.h"
#include "fontIds.h"

namespace {
constexpr int TITLE_Y = 15;
constexpr int CONTENT_START_Y = 50;
constexpr int LINE_HEIGHT = 22;
constexpr int LEFT_MARGIN = 15;
constexpr int RIGHT_MARGIN = 15;
constexpr int BOTTOM_MARGIN = 60;
}  // namespace

DictionaryDefinitionActivity::DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                           StarDict& dictionary, const stardict::SearchResult& entry,
                                                           const std::function<void()>& onBack,
                                                           const std::function<void()>& onNewSearch)
    : Activity("DictionaryDefinition", renderer, mappedInput),
      dictionary(dictionary),
      entry(entry),
      onBack(onBack),
      onNewSearch(onNewSearch) {}

void DictionaryDefinitionActivity::taskTrampoline(void* param) {
  auto* self = static_cast<DictionaryDefinitionActivity*>(param);
  self->displayTaskLoop();
}

void DictionaryDefinitionActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  scrollOffset = 0;

  loadDefinition();
  wrapText();

  updateRequired = true;

  xTaskCreate(&DictionaryDefinitionActivity::taskTrampoline, "DictDefTask", 4096, this, 1, &displayTaskHandle);
}

void DictionaryDefinitionActivity::onExit() {
  Activity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  definition.clear();
  wrappedLines.clear();
}

void DictionaryDefinitionActivity::loadDefinition() {
  definition.clear();
  dictionary.readDefinition(entry, definition, 8192);

  // Clean up definition text - remove control characters
  for (char& c : definition) {
    if (c < 32 && c != '\n' && c != '\t') {
      c = ' ';
    }
  }
}

void DictionaryDefinitionActivity::wrapText() {
  wrappedLines.clear();

  if (definition.empty()) {
    wrappedLines.push_back("(No definition available)");
    return;
  }

  const int maxWidth = renderer.getScreenWidth() - LEFT_MARGIN - RIGHT_MARGIN;
  std::string currentLine;
  std::string currentWord;

  for (size_t i = 0; i <= definition.size(); i++) {
    const char c = (i < definition.size()) ? definition[i] : '\0';

    if (c == '\n' || c == '\0') {
      // End of line or text
      if (!currentWord.empty()) {
        if (!currentLine.empty()) {
          const std::string testLine = currentLine + " " + currentWord;
          if (renderer.getTextWidth(UI_10_FONT_ID, testLine.c_str()) <= maxWidth) {
            currentLine = testLine;
          } else {
            wrappedLines.push_back(currentLine);
            currentLine = currentWord;
          }
        } else {
          currentLine = currentWord;
        }
        currentWord.clear();
      }

      if (!currentLine.empty() || c == '\n') {
        wrappedLines.push_back(currentLine);
        currentLine.clear();
      }

      if (c == '\0') break;

    } else if (c == ' ' || c == '\t') {
      // Word boundary
      if (!currentWord.empty()) {
        if (!currentLine.empty()) {
          const std::string testLine = currentLine + " " + currentWord;
          if (renderer.getTextWidth(UI_10_FONT_ID, testLine.c_str()) <= maxWidth) {
            currentLine = testLine;
          } else {
            wrappedLines.push_back(currentLine);
            currentLine = currentWord;
          }
        } else {
          currentLine = currentWord;
        }
        currentWord.clear();
      }

    } else {
      // Regular character
      currentWord += c;

      // Check if single word is too long
      if (renderer.getTextWidth(UI_10_FONT_ID, currentWord.c_str()) > maxWidth) {
        // Word too long, break it
        if (!currentLine.empty()) {
          wrappedLines.push_back(currentLine);
          currentLine.clear();
        }

        // Find break point
        std::string part;
        for (size_t j = 0; j < currentWord.size(); j++) {
          part += currentWord[j];
          if (renderer.getTextWidth(UI_10_FONT_ID, part.c_str()) > maxWidth - 10) {
            wrappedLines.push_back(part.substr(0, part.size() - 1) + "-");
            part = currentWord.substr(j);
          }
        }
        currentWord = part;
      }
    }
  }
}

int DictionaryDefinitionActivity::getVisibleLines() const {
  const int screenHeight = renderer.getScreenHeight();
  const int availableHeight = screenHeight - CONTENT_START_Y - BOTTOM_MARGIN;
  return availableHeight / LINE_HEIGHT;
}

void DictionaryDefinitionActivity::loop() {
  const int totalLines = static_cast<int>(wrappedLines.size());
  const int visibleLines = getVisibleLines();
  const int maxScroll = totalLines > visibleLines ? totalLines - visibleLines : 0;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    onNewSearch();
    return;
  }

  // Scroll with Up/Down
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (scrollOffset > 0) {
      scrollOffset--;
      updateRequired = true;
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (scrollOffset < maxScroll) {
      scrollOffset++;
      updateRequired = true;
    }
  }

  // Page scroll with PageForward/PageBack if available
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    scrollOffset = std::min(scrollOffset + visibleLines, maxScroll);
    updateRequired = true;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    scrollOffset = std::max(scrollOffset - visibleLines, 0);
    updateRequired = true;
  }
}

void DictionaryDefinitionActivity::displayTaskLoop() {
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

void DictionaryDefinitionActivity::render() const {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto screenHeight = renderer.getScreenHeight();

  // Word title
  auto title = renderer.truncatedText(UI_12_FONT_ID, entry.word.c_str(), pageWidth - 2 * LEFT_MARGIN);
  renderer.drawCenteredText(UI_12_FONT_ID, TITLE_Y, title.c_str(), true, EpdFontFamily::BOLD);

  // Separator line
  renderer.drawLine(LEFT_MARGIN, CONTENT_START_Y - 8, pageWidth - RIGHT_MARGIN, CONTENT_START_Y - 8);

  // Definition text
  const int visibleLines = getVisibleLines();
  const int totalLines = static_cast<int>(wrappedLines.size());

  for (int i = 0; i < visibleLines && (scrollOffset + i) < totalLines; i++) {
    const int lineIndex = scrollOffset + i;
    renderer.drawText(UI_10_FONT_ID, LEFT_MARGIN, CONTENT_START_Y + i * LINE_HEIGHT, wrappedLines[lineIndex].c_str());
  }

  // Scroll indicator
  if (totalLines > visibleLines) {
    const int currentPage = scrollOffset / visibleLines + 1;
    const int totalPages = (totalLines + visibleLines - 1) / visibleLines;
    const int contentHeight = screenHeight - CONTENT_START_Y - BOTTOM_MARGIN;
    ScreenComponents::drawScrollIndicator(renderer, currentPage, totalPages, CONTENT_START_Y, contentHeight);

    // Visual scroll position indicator (small bar on right side)
    const int scrollBarHeight = 60;
    const int scrollBarY = CONTENT_START_Y + (contentHeight - scrollBarHeight) * scrollOffset / (totalLines - visibleLines);
    renderer.fillRect(pageWidth - 5, scrollBarY, 3, scrollBarHeight);
  }

  // Side button hints for scrolling
  renderer.drawSideButtonHints(UI_10_FONT_ID, ">", "<");

  // Button hints
  const auto labels = mappedInput.mapLabels("Back", "", "New", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
