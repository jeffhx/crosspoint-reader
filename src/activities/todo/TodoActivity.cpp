#include "TodoActivity.h"

#include <GfxRenderer.h>

#include "../util/KeyboardEntryActivity.h"
#include "MappedInputManager.h"
#include "ScreenComponents.h"
#include "fontIds.h"

namespace {
constexpr int TITLE_Y = 15;
constexpr int CONTENT_START_Y = 55;
constexpr int LINE_HEIGHT = 30;
constexpr int LEFT_MARGIN = 20;
constexpr int CHECKBOX_SIZE = 16;
constexpr int CHECKBOX_MARGIN = 10;
constexpr int RIGHT_MARGIN = 40;
constexpr int SKIP_PAGE_MS = 700;
constexpr unsigned long DELETE_CONFIRM_TIMEOUT_MS = 2000;
}  // namespace

void TodoActivity::taskTrampoline(void* param) {
  auto* self = static_cast<TodoActivity*>(param);
  self->displayTaskLoop();
}

void TodoActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  // Load TODOs from file
  TODO_STORE.loadFromFile();

  renderingMutex = xSemaphoreCreateMutex();
  selectedIndex = 0;
  deleteConfirmPending = false;
  updateRequired = true;

  xTaskCreate(&TodoActivity::taskTrampoline, "TodoTask", 4096, this, 1, &displayTaskHandle);
}

void TodoActivity::onExit() {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  ActivityWithSubactivity::onExit();
}

int TodoActivity::getPageItems() const {
  const int screenHeight = renderer.getScreenHeight();
  const int bottomBarHeight = 60;
  const int availableHeight = screenHeight - CONTENT_START_Y - bottomBarHeight;
  int items = availableHeight / LINE_HEIGHT;
  return items > 0 ? items : 1;
}

int TodoActivity::getCurrentPage() const {
  const int pageItems = getPageItems();
  return selectedIndex / pageItems + 1;
}

int TodoActivity::getTotalPages() const {
  const int itemCount = static_cast<int>(TODO_STORE.getCount());
  const int pageItems = getPageItems();
  if (itemCount == 0) return 1;
  return (itemCount + pageItems - 1) / pageItems;
}

void TodoActivity::showAddKeyboard() {
  enterNewActivity(new KeyboardEntryActivity(
      renderer, mappedInput, "Add TODO", "", 10, 200, false,
      [this](const std::string& text) { handleAddComplete(text); },
      [this]() {
        exitActivity();
        updateRequired = true;
      }));
}

void TodoActivity::showEditKeyboard() {
  const auto& items = TODO_STORE.getItems();
  if (selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < items.size()) {
    enterNewActivity(new KeyboardEntryActivity(
        renderer, mappedInput, "Edit TODO", items[selectedIndex].text, 10, 200, false,
        [this](const std::string& text) { handleEditComplete(text); },
        [this]() {
          exitActivity();
          updateRequired = true;
        }));
  }
}

void TodoActivity::handleAddComplete(const std::string& text) {
  exitActivity();
  if (!text.empty()) {
    TODO_STORE.addItem(text);
    // Select the newly added item (last in list)
    selectedIndex = static_cast<int>(TODO_STORE.getCount()) - 1;
  }
  updateRequired = true;
}

void TodoActivity::handleEditComplete(const std::string& text) {
  exitActivity();
  if (!text.empty() && selectedIndex >= 0) {
    TODO_STORE.editItem(selectedIndex, text);
  }
  updateRequired = true;
}

void TodoActivity::loop() {
  // Handle subactivity
  if (subActivity) {
    subActivity->loop();
    return;
  }

  const int itemCount = static_cast<int>(TODO_STORE.getCount());
  const int pageItems = getPageItems();
  const bool skipPage = mappedInput.getHeldTime() > SKIP_PAGE_MS;

  // Check for delete confirmation timeout
  if (deleteConfirmPending && (millis() - deleteConfirmTime > DELETE_CONFIRM_TIMEOUT_MS)) {
    deleteConfirmPending = false;
    updateRequired = true;
  }

  // Back button
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (deleteConfirmPending) {
      deleteConfirmPending = false;
      updateRequired = true;
    } else {
      onGoBack();
    }
    return;
  }

  // Confirm button - toggle completion
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    deleteConfirmPending = false;
    if (itemCount > 0 && selectedIndex >= 0 && selectedIndex < itemCount) {
      TODO_STORE.toggleItem(selectedIndex);
      updateRequired = true;
    }
    return;
  }

  // Left button - add new item
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    deleteConfirmPending = false;
    if (!TODO_STORE.isFull()) {
      showAddKeyboard();
    }
    return;
  }

  // Right button - delete item (with confirmation)
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (itemCount > 0 && selectedIndex >= 0 && selectedIndex < itemCount) {
      if (deleteConfirmPending) {
        // Second press - confirm delete
        TODO_STORE.removeItem(selectedIndex);
        deleteConfirmPending = false;
        // Adjust selection if needed
        if (selectedIndex >= static_cast<int>(TODO_STORE.getCount()) && selectedIndex > 0) {
          selectedIndex--;
        }
      } else {
        // First press - show confirmation
        deleteConfirmPending = true;
        deleteConfirmTime = millis();
      }
      updateRequired = true;
    }
    return;
  }

  // Navigation
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) && itemCount > 0) {
    deleteConfirmPending = false;
    if (skipPage) {
      selectedIndex = ((selectedIndex / pageItems - 1) * pageItems + itemCount) % itemCount;
    } else {
      selectedIndex = (selectedIndex + itemCount - 1) % itemCount;
    }
    updateRequired = true;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down) && itemCount > 0) {
    deleteConfirmPending = false;
    if (skipPage) {
      selectedIndex = ((selectedIndex / pageItems + 1) * pageItems) % itemCount;
    } else {
      selectedIndex = (selectedIndex + 1) % itemCount;
    }
    updateRequired = true;
  }
}

void TodoActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void TodoActivity::render() const {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto screenHeight = renderer.getScreenHeight();
  const int pageItems = getPageItems();
  const auto& items = TODO_STORE.getItems();
  const int itemCount = static_cast<int>(items.size());

  // Title
  renderer.drawCenteredText(UI_12_FONT_ID, TITLE_Y, "TODO List", true, EpdFontFamily::BOLD);

  // Item count
  std::string countStr = std::to_string(itemCount) + " / " + std::to_string(TodoStore::MAX_ITEMS) + " items";
  renderer.drawCenteredText(UI_10_FONT_ID, TITLE_Y + 22, countStr.c_str());

  // Empty state
  if (itemCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, screenHeight / 2 - 10, "No tasks yet.");
    renderer.drawCenteredText(UI_10_FONT_ID, screenHeight / 2 + 10, "Press Add to create one.");

    const auto labels = mappedInput.mapLabels("Back", "", "Add", "");
    renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int pageStartIndex = selectedIndex / pageItems * pageItems;
  const int textStartX = LEFT_MARGIN + CHECKBOX_SIZE + CHECKBOX_MARGIN;
  const int maxTextWidth = pageWidth - textStartX - RIGHT_MARGIN;

  // Draw selection highlight
  renderer.fillRect(0, CONTENT_START_Y + (selectedIndex % pageItems) * LINE_HEIGHT - 2, pageWidth - RIGHT_MARGIN,
                    LINE_HEIGHT);

  // Draw items
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int y = CONTENT_START_Y + (i % pageItems) * LINE_HEIGHT;
    const bool isSelected = (i == selectedIndex);
    const auto& item = items[i];

    // Draw checkbox
    const int checkboxY = y + (LINE_HEIGHT - CHECKBOX_SIZE) / 2 - 5;
    renderer.drawRect(LEFT_MARGIN, checkboxY, CHECKBOX_SIZE, CHECKBOX_SIZE, !isSelected);

    // Draw checkmark if completed
    if (item.completed) {
      // Draw X inside checkbox for completed items
      renderer.drawLine(LEFT_MARGIN + 3, checkboxY + 3, LEFT_MARGIN + CHECKBOX_SIZE - 3, checkboxY + CHECKBOX_SIZE - 3,
                        !isSelected);
      renderer.drawLine(LEFT_MARGIN + CHECKBOX_SIZE - 3, checkboxY + 3, LEFT_MARGIN + 3, checkboxY + CHECKBOX_SIZE - 3,
                        !isSelected);
    }

    // Draw text (truncated if needed)
    auto text = renderer.truncatedText(UI_10_FONT_ID, item.text.c_str(), maxTextWidth);
    renderer.drawText(UI_10_FONT_ID, textStartX, y, text.c_str(), !isSelected);
  }

  // Scroll indicator
  const int contentHeight = screenHeight - CONTENT_START_Y - 60;
  ScreenComponents::drawScrollIndicator(renderer, getCurrentPage(), getTotalPages(), CONTENT_START_Y, contentHeight);

  // Side button hints
  renderer.drawSideButtonHints(UI_10_FONT_ID, ">", "<");

  // Button hints
  const char* rightLabel = deleteConfirmPending ? "Confirm?" : "Delete";
  const char* addLabel = TODO_STORE.isFull() ? "" : "Add";
  const auto labels = mappedInput.mapLabels("Back", "Toggle", addLabel, rightLabel);
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
