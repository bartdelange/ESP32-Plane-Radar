#include "core/platform.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace core::platform {
namespace {
const std::string& cachePath() {
  static const std::string value = [] {
    const char* override_path = std::getenv("PLANE_RADAR_TERRAIN_CACHE");
    if (override_path != nullptr && override_path[0] != '\0') {
      return std::string(override_path);
    }
    const char* home = std::getenv("HOME");
    return std::string(home != nullptr ? home : ".") +
           "/.plane-radar/terrain-grid.bin";
  }();
  return value;
}

void ensureParentDir() {
  const size_t slash = cachePath().find_last_of('/');
  if (slash != std::string::npos && slash > 0) {
    ::mkdir(cachePath().substr(0, slash).c_str(), 0700);
  }
}
}  // namespace

bool TerrainCacheStore::load(void* data, size_t len) {
  if (data == nullptr) return false;
  std::FILE* file = std::fopen(cachePath().c_str(), "rb");
  if (file == nullptr) return false;
  const bool ok = std::fread(data, 1, len, file) == len &&
                  std::fgetc(file) == EOF;
  std::fclose(file);
  return ok;
}

bool TerrainCacheStore::save(const void* data, size_t len) {
  if (data == nullptr) return false;
  ensureParentDir();
  const std::string temp = cachePath() + ".tmp";
  std::FILE* file = std::fopen(temp.c_str(), "wb");
  if (file == nullptr) return false;
  const bool written = std::fwrite(data, 1, len, file) == len;
  std::fflush(file);
  ::fsync(::fileno(file));
  std::fclose(file);
  if (!written || std::rename(temp.c_str(), cachePath().c_str()) != 0) {
    std::remove(temp.c_str());
    return false;
  }
  return true;
}

void TerrainCacheStore::remove() {
  std::remove(cachePath().c_str());
  const std::string temp = cachePath() + ".tmp";
  std::remove(temp.c_str());
}
}  // namespace core::platform
