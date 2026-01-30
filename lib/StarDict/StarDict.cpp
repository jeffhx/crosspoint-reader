#include "StarDict.h"

#include <SDCardManager.h>
#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <cstring>

// Define STARDICT_DEBUG to enable debug output
#ifdef STARDICT_DEBUG
#define DICT_LOG(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
#define DICT_LOG(fmt, ...) ((void)0)
#endif

namespace {
// Helper to trim whitespace from string
std::string trim(const std::string& str) {
  size_t start = 0;
  size_t end = str.size();
  while (start < end && std::isspace(static_cast<unsigned char>(str[start]))) {
    ++start;
  }
  while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
    --end;
  }
  return str.substr(start, end - start);
}

// Case-insensitive string comparison for prefix matching
int strcasecmpPrefix(const char* str, const char* prefix, size_t prefixLen) {
  for (size_t i = 0; i < prefixLen; ++i) {
    const int c1 = std::tolower(static_cast<unsigned char>(str[i]));
    const int c2 = std::tolower(static_cast<unsigned char>(prefix[i]));
    if (c1 != c2) {
      return c1 - c2;
    }
    if (c1 == '\0') {
      return 0;  // str ended before prefix
    }
  }
  return 0;  // prefix matches
}
}  // namespace

StarDict::StarDict(const std::string& dictFolder) : basePath(dictFolder) {
  // Ensure path doesn't end with /
  if (!basePath.empty() && basePath.back() == '/') {
    basePath.pop_back();
  }
}

bool StarDict::load() {
  if (loaded) {
    return true;
  }

  // Find .ifo file in the dictionary folder
  FsFile dir = SdMan.open(basePath.c_str());
  if (!dir || !dir.isDirectory()) {
    DICT_LOG("[DICT] Failed to open directory: %s\n", basePath.c_str());
    if (dir) dir.close();
    return false;
  }

  char filename[256];
  bool foundIfo = false;
  FsFile entry;

  while (entry.openNext(&dir, O_RDONLY)) {
    entry.getName(filename, sizeof(filename));
    entry.close();

    const std::string fname(filename);
    if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".ifo") {
      ifoPath = basePath + "/" + fname;
      dictName = fname.substr(0, fname.size() - 4);
      foundIfo = true;
      break;
    }
  }
  dir.close();

  if (!foundIfo) {
    DICT_LOG("[DICT] No .ifo file found in: %s\n", basePath.c_str());
    return false;
  }

  // Parse the .ifo file
  if (!parseIfoFile()) {
    return false;
  }

  // Determine paths for .idx and .dict files
  idxPath = basePath + "/" + dictName + ".idx";
  dictPath = basePath + "/" + dictName + ".dict.dz";
  isCompressed = SdMan.exists(dictPath.c_str());

  if (!isCompressed) {
    dictPath = basePath + "/" + dictName + ".dict";
    if (!SdMan.exists(dictPath.c_str())) {
      DICT_LOG("[DICT] Neither .dict nor .dict.dz found for: %s\n", dictName.c_str());
      return false;
    }
  }

  if (!SdMan.exists(idxPath.c_str())) {
    DICT_LOG("[DICT] Index file not found: %s\n", idxPath.c_str());
    return false;
  }

  loaded = true;
  DICT_LOG("[DICT] Loaded dictionary: %s (%u words)\n", info.bookname.c_str(), info.wordcount);
  return true;
}

bool StarDict::parseIfoFile() {
  FsFile file;
  if (!SdMan.openFileForRead("DICT", ifoPath, file)) {
    DICT_LOG("[DICT] Failed to open .ifo file: %s\n", ifoPath.c_str());
    return false;
  }

  char buffer[512];
  std::string line;

  // Read first line - should be "StarDict's dict ifo file"
  if (file.fgets(buffer, sizeof(buffer)) <= 0) {
    file.close();
    return false;
  }
  line = trim(buffer);
  if (line.find("StarDict") == std::string::npos) {
    DICT_LOG("[DICT] Invalid .ifo header: %s\n", line.c_str());
    file.close();
    return false;
  }

  // Parse key=value pairs
  while (file.fgets(buffer, sizeof(buffer)) > 0) {
    line = trim(buffer);
    if (line.empty()) {
      continue;
    }

    const size_t eqPos = line.find('=');
    if (eqPos == std::string::npos) {
      continue;
    }

    const std::string key = trim(line.substr(0, eqPos));
    const std::string value = trim(line.substr(eqPos + 1));

    if (key == "bookname") {
      info.bookname = value;
    } else if (key == "version") {
      info.version = value;
    } else if (key == "wordcount") {
      info.wordcount = std::strtoul(value.c_str(), nullptr, 10);
    } else if (key == "idxfilesize") {
      info.idxfilesize = std::strtoul(value.c_str(), nullptr, 10);
    } else if (key == "sametypesequence") {
      info.sametypesequence = value;
    } else if (key == "idxoffsetbits") {
      info.idxOffsetBits64 = (value == "64");
    }
  }

  file.close();

  if (info.bookname.empty()) {
    info.bookname = dictName;  // Fall back to filename
  }

  return info.wordcount > 0;
}

bool StarDict::readIndexEntry(FsFile& idxFile, std::string& word, uint32_t& dataOffset, uint32_t& dataSize) {
  word.clear();

  // Read null-terminated word
  char c;
  while (idxFile.read(&c, 1) == 1) {
    if (c == '\0') {
      break;
    }
    word += c;
    if (word.size() > 256) {
      // Word too long, probably corrupted
      return false;
    }
  }

  if (word.empty()) {
    return false;  // EOF or error
  }

  // Read offset (4 bytes, big-endian)
  uint8_t buf[4];
  if (idxFile.read(buf, 4) != 4) {
    return false;
  }
  dataOffset = (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
               (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);

  // Read size (4 bytes, big-endian)
  if (idxFile.read(buf, 4) != 4) {
    return false;
  }
  dataSize = (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
             (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);

  return true;
}

bool StarDict::searchPrefix(const char* prefix, std::vector<stardict::SearchResult>& results, size_t maxResults) {
  results.clear();

  if (!loaded || prefix == nullptr) {
    return false;
  }

  const size_t prefixLen = strlen(prefix);
  if (prefixLen == 0) {
    return false;
  }

  FsFile idxFile;
  if (!SdMan.openFileForRead("DICT", idxPath, idxFile)) {
    return false;
  }

  // Linear scan through index (simple approach for now)
  // Could be optimized with binary search for large dictionaries
  idxFile.seek(0);

  std::string word;
  uint32_t dataOffset, dataSize;

  while (results.size() < maxResults && readIndexEntry(idxFile, word, dataOffset, dataSize)) {
    // Case-insensitive prefix match
    if (word.size() >= prefixLen && strcasecmpPrefix(word.c_str(), prefix, prefixLen) == 0) {
      stardict::SearchResult sr;
      sr.word = word;
      sr.dataOffset = dataOffset;
      sr.dataSize = dataSize;
      results.push_back(sr);
    }
  }

  idxFile.close();
  return !results.empty();
}

bool StarDict::lookupExact(const char* word, stardict::SearchResult& result) {
  if (!loaded || word == nullptr || *word == '\0') {
    return false;
  }

  FsFile idxFile;
  if (!SdMan.openFileForRead("DICT", idxPath, idxFile)) {
    return false;
  }

  idxFile.seek(0);

  std::string entryWord;
  uint32_t dataOffset, dataSize;

  while (readIndexEntry(idxFile, entryWord, dataOffset, dataSize)) {
    // Case-insensitive exact match
    if (entryWord.size() == strlen(word) && strcasecmpPrefix(entryWord.c_str(), word, entryWord.size()) == 0) {
      result.word = entryWord;
      result.dataOffset = dataOffset;
      result.dataSize = dataSize;
      idxFile.close();
      return true;
    }
  }

  idxFile.close();
  return false;
}

bool StarDict::readDefinition(const stardict::SearchResult& entry, std::string& definition, size_t maxSize) {
  definition.clear();

  if (!loaded) {
    return false;
  }

  const size_t readSize = std::min(static_cast<size_t>(entry.dataSize), maxSize);

  if (isCompressed) {
    // Handle .dict.dz (gzip compressed)
    // For simplicity, we'll read the whole compressed file and decompress
    // In a production implementation, we'd use dictzip chunk random access

    FsFile file;
    if (!SdMan.openFileForRead("DICT", dictPath, file)) {
      return false;
    }

    // Read entire compressed file
    const size_t compSize = file.size();
    uint8_t* compData = static_cast<uint8_t*>(malloc(compSize));
    if (!compData) {
      file.close();
      return false;
    }

    file.read(compData, compSize);
    file.close();

    // Decompress using miniz
    // Skip gzip header (10 bytes minimum)
    size_t headerSize = 10;
    if (compSize < headerSize) {
      free(compData);
      return false;
    }

    // Check for optional fields in gzip header
    const uint8_t flags = compData[3];
    if (flags & 0x04) {  // FEXTRA
      if (compSize < headerSize + 2) {
        free(compData);
        return false;
      }
      const uint16_t extraLen = compData[headerSize] | (compData[headerSize + 1] << 8);
      headerSize += 2 + extraLen;
    }
    if (flags & 0x08) {  // FNAME
      while (headerSize < compSize && compData[headerSize] != 0) {
        ++headerSize;
      }
      ++headerSize;  // Skip null terminator
    }
    if (flags & 0x10) {  // FCOMMENT
      while (headerSize < compSize && compData[headerSize] != 0) {
        ++headerSize;
      }
      ++headerSize;
    }
    if (flags & 0x02) {  // FHCRC
      headerSize += 2;
    }

    // Allocate buffer for decompressed data (estimate: entry offset + size + some margin)
    const size_t decompBufSize = entry.dataOffset + entry.dataSize + 1024;
    uint8_t* decompData = static_cast<uint8_t*>(malloc(decompBufSize));
    if (!decompData) {
      free(compData);
      return false;
    }

    // Try decompression - first with zlib header flag, then without
    size_t decompSize = decompBufSize;
    int status = tinfl_decompress_mem_to_mem(decompData, decompSize, compData + headerSize,
                                             compSize - headerSize - 8, TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (status < 0) {
      // Retry without zlib header flag
      decompSize = decompBufSize;
      status = tinfl_decompress_mem_to_mem(decompData, decompSize, compData + headerSize,
                                           compSize - headerSize - 8, 0);
    }
    free(compData);

    if (status < 0) {
      free(decompData);
      return false;
    }
    decompSize = status;

    // Extract definition from decompressed data
    if (entry.dataOffset + readSize <= decompSize) {
      definition.assign(reinterpret_cast<char*>(decompData + entry.dataOffset), readSize);
    }

    free(decompData);
  } else {
    // Uncompressed .dict file
    FsFile file;
    if (!SdMan.openFileForRead("DICT", dictPath, file)) {
      return false;
    }

    file.seek(entry.dataOffset);

    char* buffer = static_cast<char*>(malloc(readSize + 1));
    if (!buffer) {
      file.close();
      return false;
    }

    const size_t bytesRead = file.read(buffer, readSize);
    buffer[bytesRead] = '\0';
    definition = buffer;

    free(buffer);
    file.close();
  }

  return !definition.empty();
}
