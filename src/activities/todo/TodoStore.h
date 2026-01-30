#pragma once
#include <string>
#include <vector>

struct TodoItem {
  std::string text;
  bool completed;

  TodoItem() : completed(false) {}
  TodoItem(const std::string& text, bool completed = false) : text(text), completed(completed) {}
};

class TodoStore {
  static TodoStore instance;

  std::vector<TodoItem> items;

 public:
  static constexpr size_t MAX_ITEMS = 20;

  ~TodoStore() = default;

  static TodoStore& getInstance() { return instance; }

  // Add a new item (returns false if at max capacity)
  bool addItem(const std::string& text);

  // Remove item at index
  bool removeItem(size_t index);

  // Toggle completion status
  bool toggleItem(size_t index);

  // Edit item text
  bool editItem(size_t index, const std::string& text);

  // Move item from one index to another
  bool moveItem(size_t from, size_t to);

  // Get all items
  const std::vector<TodoItem>& getItems() const { return items; }

  // Get item count
  size_t getCount() const { return items.size(); }

  // Check if at capacity
  bool isFull() const { return items.size() >= MAX_ITEMS; }

  // Persistence
  bool saveToFile() const;
  bool loadFromFile();
};

#define TODO_STORE TodoStore::getInstance()
