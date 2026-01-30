#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include "../ActivityWithSubactivity.h"
#include "TodoStore.h"

class TodoActivity final : public ActivityWithSubactivity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  int selectedIndex = 0;
  bool deleteConfirmPending = false;
  unsigned long deleteConfirmTime = 0;

  const std::function<void()> onGoBack;

  // Pagination helpers
  int getPageItems() const;
  int getCurrentPage() const;
  int getTotalPages() const;

  // Actions
  void showAddKeyboard();
  void showEditKeyboard();
  void handleAddComplete(const std::string& text);
  void handleEditComplete(const std::string& text);

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;

 public:
  explicit TodoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        const std::function<void()>& onGoBack)
      : ActivityWithSubactivity("TodoList", renderer, mappedInput), onGoBack(onGoBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
};
