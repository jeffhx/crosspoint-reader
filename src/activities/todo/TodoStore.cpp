#include "TodoStore.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <algorithm>

namespace {
constexpr uint8_t TODO_FILE_VERSION = 1;
constexpr char TODO_FILE[] = "/.crosspoint/todos.bin";
}  // namespace

TodoStore TodoStore::instance;

bool TodoStore::addItem(const std::string& text) {
  if (text.empty() || items.size() >= MAX_ITEMS) {
    return false;
  }

  items.emplace_back(text, false);
  saveToFile();
  return true;
}

bool TodoStore::removeItem(size_t index) {
  if (index >= items.size()) {
    return false;
  }

  items.erase(items.begin() + index);
  saveToFile();
  return true;
}

bool TodoStore::toggleItem(size_t index) {
  if (index >= items.size()) {
    return false;
  }

  items[index].completed = !items[index].completed;
  saveToFile();
  return true;
}

bool TodoStore::editItem(size_t index, const std::string& text) {
  if (index >= items.size() || text.empty()) {
    return false;
  }

  items[index].text = text;
  saveToFile();
  return true;
}

bool TodoStore::moveItem(size_t from, size_t to) {
  if (from >= items.size() || to >= items.size() || from == to) {
    return false;
  }

  TodoItem item = items[from];
  items.erase(items.begin() + from);
  items.insert(items.begin() + to, item);
  saveToFile();
  return true;
}

bool TodoStore::saveToFile() const {
  SdMan.mkdir("/.crosspoint");

  FsFile outputFile;
  if (!SdMan.openFileForWrite("TDS", TODO_FILE, outputFile)) {
    Serial.printf("[%lu] [TDS] Failed to open file for writing\n", millis());
    return false;
  }

  serialization::writePod(outputFile, TODO_FILE_VERSION);
  const uint8_t count = static_cast<uint8_t>(items.size());
  serialization::writePod(outputFile, count);

  for (const auto& item : items) {
    const uint8_t completed = item.completed ? 1 : 0;
    serialization::writePod(outputFile, completed);
    serialization::writeString(outputFile, item.text);
  }

  outputFile.close();
  Serial.printf("[%lu] [TDS] TODO items saved to file (%d entries)\n", millis(), count);
  return true;
}

bool TodoStore::loadFromFile() {
  FsFile inputFile;
  if (!SdMan.openFileForRead("TDS", TODO_FILE, inputFile)) {
    Serial.printf("[%lu] [TDS] No TODO file found, starting fresh\n", millis());
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version != TODO_FILE_VERSION) {
    Serial.printf("[%lu] [TDS] Unknown file version %u\n", millis(), version);
    inputFile.close();
    return false;
  }

  uint8_t count;
  serialization::readPod(inputFile, count);

  items.clear();
  items.reserve(count);

  for (uint8_t i = 0; i < count && i < MAX_ITEMS; i++) {
    uint8_t completed;
    serialization::readPod(inputFile, completed);

    std::string text;
    serialization::readString(inputFile, text);

    items.emplace_back(text, completed != 0);
  }

  inputFile.close();
  Serial.printf("[%lu] [TDS] TODO items loaded from file (%d entries)\n", millis(), static_cast<int>(items.size()));
  return true;
}
