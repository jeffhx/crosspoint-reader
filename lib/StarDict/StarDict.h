#pragma once

#include <SdFat.h>

#include <string>
#include <vector>

#include "StarDictTypes.h"

class StarDict {
  std::string basePath;      // Path to dictionary folder (e.g., "/dictionaries/fr-en")
  std::string dictName;      // Dictionary name from .ifo
  std::string ifoPath;       // Full path to .ifo file
  std::string idxPath;       // Full path to .idx file
  std::string dictPath;      // Full path to .dict or .dict.dz file
  bool isCompressed;         // true if .dict.dz exists
  stardict::DictionaryInfo info;
  bool loaded = false;

  // Parse the .ifo metadata file
  bool parseIfoFile();

  // Read a single index entry at given file position
  bool readIndexEntry(FsFile& idxFile, std::string& word, uint32_t& dataOffset, uint32_t& dataSize);

 public:
  explicit StarDict(const std::string& dictFolder);

  // Load dictionary metadata
  bool load();

  // Check if dictionary is loaded
  bool isLoaded() const { return loaded; }

  // Get dictionary name
  const std::string& getName() const { return info.bookname; }

  // Get word count
  uint32_t getWordCount() const { return info.wordcount; }

  // Search for words starting with prefix
  // Returns up to maxResults matches
  bool searchPrefix(const char* prefix, std::vector<stardict::SearchResult>& results, size_t maxResults = 20);

  // Look up exact word
  bool lookupExact(const char* word, stardict::SearchResult& result);

  // Read definition for a search result
  // Returns the definition text, truncated to maxSize bytes
  bool readDefinition(const stardict::SearchResult& entry, std::string& definition, size_t maxSize = 4096);
};
