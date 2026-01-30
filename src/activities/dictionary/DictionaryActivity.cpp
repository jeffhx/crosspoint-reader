#include "DictionaryActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include "DictionaryDefinitionActivity.h"
#include "DictionaryResultsActivity.h"
#include "DictionarySearchActivity.h"
#include "MappedInputManager.h"
#include "ScreenComponents.h"
#include "fontIds.h"

namespace {
constexpr int TITLE_Y = 15;
constexpr int CONTENT_START_Y = 60;
constexpr int LINE_HEIGHT = 30;
constexpr int LEFT_MARGIN = 20;
constexpr int RIGHT_MARGIN = 40;
constexpr int SKIP_PAGE_MS = 700;
}  // namespace

DictionaryActivity::DictionaryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const std::function<void()>& onGoHome)
    : ActivityWithSubactivity("Dictionary", renderer, mappedInput), onGoHome(onGoHome) {}

void DictionaryActivity::taskTrampoline(void* param) {
  auto* self = static_cast<DictionaryActivity*>(param);
  self->displayTaskLoop();
}

void DictionaryActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  // Scan for available dictionaries
  scanForDictionaries();

  if (availableDicts.empty()) {
    // No dictionaries found - show message
    currentScreen = Screen::Selection;
  } else if (availableDicts.size() == 1) {
    // Only one dictionary - load it directly and go to search
    selectedDictIndex = 0;
    loadSelectedDictionary();
    if (currentDict && currentDict->isLoaded()) {
      showSearch();
    }
  } else {
    // Multiple dictionaries - show selection
    currentScreen = Screen::Selection;
  }

  selectedDictIndex = 0;
  updateRequired = true;

  xTaskCreate(&DictionaryActivity::taskTrampoline, "DictionaryTask", 4096, this, 1, &displayTaskHandle);
}

void DictionaryActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  currentDict.reset();
  availableDicts.clear();
  dictNames.clear();
  searchResults.clear();
}

void DictionaryActivity::scanForDictionaries() {
  availableDicts.clear();
  dictNames.clear();

  const char* dictRoot = "/dictionaries";
  if (!SdMan.exists(dictRoot)) {
    Serial.printf("[DICT] No /dictionaries folder found\n");
    return;
  }

  FsFile root = SdMan.open(dictRoot);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  char name[256];
  FsFile entry;

  while (entry.openNext(&root, O_RDONLY)) {
    if (entry.isDirectory()) {
      entry.getName(name, sizeof(name));
      if (name[0] != '.') {
        const std::string dictPath = std::string(dictRoot) + "/" + name;

        // Check if this directory contains a .ifo file
        FsFile subdir = SdMan.open(dictPath.c_str());
        if (subdir && subdir.isDirectory()) {
          FsFile subentry;
          bool hasIfo = false;
          char subname[256];

          while (subentry.openNext(&subdir, O_RDONLY)) {
            subentry.getName(subname, sizeof(subname));
            subentry.close();

            const std::string fname(subname);
            if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".ifo") {
              hasIfo = true;
              break;
            }
          }
          subdir.close();

          if (hasIfo) {
            availableDicts.push_back(dictPath);
            dictNames.push_back(name);
          }
        }
      }
    }
    entry.close();
  }
  root.close();

  Serial.printf("[DICT] Found %zu dictionaries\n", availableDicts.size());
}

void DictionaryActivity::loadSelectedDictionary() {
  if (selectedDictIndex < 0 || selectedDictIndex >= static_cast<int>(availableDicts.size())) {
    return;
  }

  currentDict.reset(new StarDict(availableDicts[selectedDictIndex]));
  if (!currentDict->load()) {
    currentDict.reset();
  }
}

int DictionaryActivity::getPageItems() const {
  const int screenHeight = renderer.getScreenHeight();
  const int bottomBarHeight = 60;
  const int availableHeight = screenHeight - CONTENT_START_Y - bottomBarHeight;
  int items = availableHeight / LINE_HEIGHT;
  return items > 0 ? items : 1;
}

int DictionaryActivity::getCurrentPage() const {
  const int pageItems = getPageItems();
  return selectedDictIndex / pageItems + 1;
}

int DictionaryActivity::getTotalPages() const {
  const int itemCount = static_cast<int>(availableDicts.size());
  const int pageItems = getPageItems();
  if (itemCount == 0) return 1;
  return (itemCount + pageItems - 1) / pageItems;
}

void DictionaryActivity::showSearch() {
  if (!currentDict || !currentDict->isLoaded()) {
    return;
  }

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  exitActivity();

  currentScreen = Screen::Search;

  enterNewActivity(new DictionarySearchActivity(
      renderer, mappedInput, *currentDict,
      // onSearch
      [this](const std::string& term) {
        exitActivity();
        showResults(term);
      },
      // onSelectSuggestion
      [this](const stardict::SearchResult& entry) {
        exitActivity();
        showDefinition(entry);
      },
      // onCancel
      [this] {
        exitActivity();
        currentScreen = Screen::Selection;
        updateRequired = true;
      },
      lastSearchTerm));

  xSemaphoreGive(renderingMutex);
}

void DictionaryActivity::showResults(const std::string& searchTerm) {
  if (!currentDict || !currentDict->isLoaded()) {
    return;
  }

  lastSearchTerm = searchTerm;
  searchResults.clear();
  currentDict->searchPrefix(searchTerm.c_str(), searchResults, 50);

  if (searchResults.empty()) {
    // No results - go back to search
    showSearch();
    return;
  }

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  exitActivity();

  currentScreen = Screen::Results;

  enterNewActivity(new DictionaryResultsActivity(
      renderer, mappedInput, *currentDict, searchTerm, searchResults,
      // onSelectWord
      [this](const stardict::SearchResult& entry) {
        exitActivity();
        showDefinition(entry);
      },
      // onBack
      [this] {
        exitActivity();
        showSearch();
      },
      // onNewSearch
      [this] {
        exitActivity();
        lastSearchTerm.clear();
        showSearch();
      }));

  xSemaphoreGive(renderingMutex);
}

void DictionaryActivity::showDefinition(const stardict::SearchResult& entry) {
  if (!currentDict || !currentDict->isLoaded()) {
    return;
  }

  selectedEntry = entry;

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  exitActivity();

  currentScreen = Screen::Definition;

  enterNewActivity(new DictionaryDefinitionActivity(
      renderer, mappedInput, *currentDict, entry,
      // onBack
      [this] {
        exitActivity();
        if (!lastSearchTerm.empty()) {
          showResults(lastSearchTerm);
        } else {
          showSearch();
        }
      },
      // onNewSearch
      [this] {
        exitActivity();
        lastSearchTerm.clear();
        showSearch();
      }));

  xSemaphoreGive(renderingMutex);
}

void DictionaryActivity::loop() {
  // Delegate to subactivity if present
  if (subActivity) {
    subActivity->loop();
    return;
  }

  // Handle selection screen input
  if (currentScreen == Screen::Selection) {
    const int dictCount = static_cast<int>(availableDicts.size());
    const int pageItems = getPageItems();
    const bool skipPage = mappedInput.getHeldTime() > SKIP_PAGE_MS;

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (dictCount > 0) {
        loadSelectedDictionary();
        if (currentDict && currentDict->isLoaded()) {
          showSearch();
        }
      }
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Up) && dictCount > 0) {
      if (skipPage) {
        selectedDictIndex = ((selectedDictIndex / pageItems - 1) * pageItems + dictCount) % dictCount;
      } else {
        selectedDictIndex = (selectedDictIndex + dictCount - 1) % dictCount;
      }
      updateRequired = true;
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down) && dictCount > 0) {
      if (skipPage) {
        selectedDictIndex = ((selectedDictIndex / pageItems + 1) * pageItems) % dictCount;
      } else {
        selectedDictIndex = (selectedDictIndex + 1) % dictCount;
      }
      updateRequired = true;
    }
  }
}

void DictionaryActivity::displayTaskLoop() {
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

void DictionaryActivity::render() const {
  if (subActivity) {
    return;  // Subactivity handles its own rendering
  }

  renderer.clearScreen();

  // Title
  renderer.drawCenteredText(UI_12_FONT_ID, TITLE_Y, "Dictionary", true, EpdFontFamily::BOLD);

  if (currentScreen == Screen::Selection) {
    renderSelection();
  }

  renderer.displayBuffer();
}

void DictionaryActivity::renderSelection() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto screenHeight = renderer.getScreenHeight();
  const int pageItems = getPageItems();
  const int dictCount = static_cast<int>(availableDicts.size());

  if (dictCount == 0) {
    // No dictionaries found
    const int msgY = screenHeight / 2 - renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawCenteredText(UI_10_FONT_ID, msgY, "No dictionaries found");
    renderer.drawCenteredText(UI_10_FONT_ID, msgY + LINE_HEIGHT, "Add dictionaries to /dictionaries/");

    const auto labels = mappedInput.mapLabels("Home", "", "", "");
    renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  // Draw subtitle
  renderer.drawCenteredText(UI_10_FONT_ID, TITLE_Y + 25, "Select Dictionary");

  const int pageStartIndex = selectedDictIndex / pageItems * pageItems;

  // Draw selection highlight
  renderer.fillRect(0, CONTENT_START_Y + (selectedDictIndex % pageItems) * LINE_HEIGHT - 2, pageWidth - RIGHT_MARGIN,
                    LINE_HEIGHT);

  // Draw dictionary names
  for (int i = pageStartIndex; i < dictCount && i < pageStartIndex + pageItems; i++) {
    auto item = renderer.truncatedText(UI_10_FONT_ID, dictNames[i].c_str(), pageWidth - LEFT_MARGIN - RIGHT_MARGIN);
    renderer.drawText(UI_10_FONT_ID, LEFT_MARGIN, CONTENT_START_Y + (i % pageItems) * LINE_HEIGHT, item.c_str(),
                      i != selectedDictIndex);
  }

  // Scroll indicator
  const int contentHeight = screenHeight - CONTENT_START_Y - 60;
  ScreenComponents::drawScrollIndicator(renderer, getCurrentPage(), getTotalPages(), CONTENT_START_Y, contentHeight);

  // Button hints
  renderer.drawSideButtonHints(UI_10_FONT_ID, ">", "<");
  const auto labels = mappedInput.mapLabels("Home", "Select", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
