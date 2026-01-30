#pragma once

#include <cstdint>
#include <string>

namespace stardict {

// Parsed metadata from .ifo file
struct DictionaryInfo {
  std::string bookname;
  std::string version;
  uint32_t wordcount = 0;
  uint32_t idxfilesize = 0;
  std::string sametypesequence;  // Usually "m" for plain text
  bool idxOffsetBits64 = false;  // true if offsets are 64-bit (rare)
};

// Search result - minimal data for display
struct SearchResult {
  std::string word;
  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
};

// Cache file constants
constexpr uint32_t CACHE_MAGIC = 0x53444348;  // "SDCH" in little-endian
constexpr uint8_t CACHE_VERSION = 1;

}  // namespace stardict
