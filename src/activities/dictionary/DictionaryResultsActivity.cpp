#include "DictionaryResultsActivity.h"

#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "ScreenComponents.h"
#include "fontIds.h"

namespace {
constexpr int TITLE_Y = 15;
constexpr int CONTENT_START_Y = 55;
constexpr int LINE_HEIGHT = 28;
constexpr int LEFT_MARGIN = 20;
constexpr int RIGHT_MARGIN = 40;
constexpr int SKIP_PAGE_MS = 700;
}  // namespace

DictionaryResultsActivity::DictionaryResultsActivity(
    GfxRenderer& renderer, MappedInputManager& mappedInput, StarDict& dictionary, const std::string& searchTerm,
    std::vector<stardict::SearchResult> results, const std::function<void(const stardict::SearchResult&)>& onSelectWord,
    const std::function<void()>& onBack, const std::function<void()>& onNewSearch)
    : Activity("DictionaryResults", renderer, mappedInput),
      dictionary(dictionary),
      searchTerm(searchTerm),
      results(std::move(results)),
      onSelectWord(onSelectWord),
      onBack(onBack),
      onNewSearch(onNewSearch) {}

void DictionaryResultsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<DictionaryResultsActivity*>(param);
  self->displayTaskLoop();
}

void DictionaryResultsActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  selectedIndex = 0;
  updateRequired = true;

  xTaskCreate(&DictionaryResultsActivity::taskTrampoline, "DictResultsTask", 4096, this, 1, &displayTaskHandle);
}

void DictionaryResultsActivity::onExit() {
  Activity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

int DictionaryResultsActivity::getPageItems() const {
  const int screenHeight = renderer.getScreenHeight();
  const int bottomBarHeight = 60;
  const int availableHeight = screenHeight - CONTENT_START_Y - bottomBarHeight;
  int items = availableHeight / LINE_HEIGHT;
  return items > 0 ? items : 1;
}

int DictionaryResultsActivity::getCurrentPage() const {
  const int pageItems = getPageItems();
  return selectedIndex / pageItems + 1;
}

int DictionaryResultsActivity::getTotalPages() const {
  const int itemCount = static_cast<int>(results.size());
  const int pageItems = getPageItems();
  if (itemCount == 0) return 1;
  return (itemCount + pageItems - 1) / pageItems;
}

void DictionaryResultsActivity::loop() {
  const int resultCount = static_cast<int>(results.size());
  const int pageItems = getPageItems();
  const bool skipPage = mappedInput.getHeldTime() > SKIP_PAGE_MS;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (resultCount > 0 && selectedIndex < resultCount) {
      onSelectWord(results[selectedIndex]);
    }
    return;
  }

  // Left button for new search
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    onNewSearch();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up) && resultCount > 0) {
    if (skipPage) {
      selectedIndex = ((selectedIndex / pageItems - 1) * pageItems + resultCount) % resultCount;
    } else {
      selectedIndex = (selectedIndex + resultCount - 1) % resultCount;
    }
    updateRequired = true;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down) && resultCount > 0) {
    if (skipPage) {
      selectedIndex = ((selectedIndex / pageItems + 1) * pageItems) % resultCount;
    } else {
      selectedIndex = (selectedIndex + 1) % resultCount;
    }
    updateRequired = true;
  }
}

void DictionaryResultsActivity::displayTaskLoop() {
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

void DictionaryResultsActivity::render() const {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto screenHeight = renderer.getScreenHeight();
  const int pageItems = getPageItems();
  const int resultCount = static_cast<int>(results.size());

  // Title with search term
  std::string title = "Results: \"" + searchTerm + "\"";
  if (title.size() > 30) {
    title = title.substr(0, 27) + "...\"";
  }
  renderer.drawCenteredText(UI_12_FONT_ID, TITLE_Y, title.c_str(), true, EpdFontFamily::BOLD);

  // Result count
  std::string countStr = std::to_string(resultCount) + " matches";
  renderer.drawCenteredText(UI_10_FONT_ID, TITLE_Y + 22, countStr.c_str());

  if (resultCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, screenHeight / 2, "No results found");

    const auto labels = mappedInput.mapLabels("Back", "", "New", "");
    renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int pageStartIndex = selectedIndex / pageItems * pageItems;

  // Draw selection highlight
  renderer.fillRect(0, CONTENT_START_Y + (selectedIndex % pageItems) * LINE_HEIGHT - 2, pageWidth - RIGHT_MARGIN,
                    LINE_HEIGHT);

  // Draw results
  for (int i = pageStartIndex; i < resultCount && i < pageStartIndex + pageItems; i++) {
    auto item = renderer.truncatedText(UI_10_FONT_ID, results[i].word.c_str(), pageWidth - LEFT_MARGIN - RIGHT_MARGIN);
    renderer.drawText(UI_10_FONT_ID, LEFT_MARGIN, CONTENT_START_Y + (i % pageItems) * LINE_HEIGHT, item.c_str(),
                      i != selectedIndex);
  }

  // Scroll indicator
  const int contentHeight = screenHeight - CONTENT_START_Y - 60;
  ScreenComponents::drawScrollIndicator(renderer, getCurrentPage(), getTotalPages(), CONTENT_START_Y, contentHeight);

  // Side button hints
  renderer.drawSideButtonHints(UI_10_FONT_ID, ">", "<");

  // Button hints
  const auto labels = mappedInput.mapLabels("Back", "View", "New", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
