#include "core/platform.h"

#include <LittleFS.h>

namespace core::platform {
namespace {
constexpr char kPath[] = "/terrain-grid.bin";
constexpr char kTempPath[] = "/terrain-grid.tmp";
}

bool TerrainCacheStore::load(void* data, size_t len) {
  if (data == nullptr || !LittleFS.begin(true)) return false;
  File file = LittleFS.open(kPath, FILE_READ);
  const bool ok = file && file.size() == len &&
                  file.read(static_cast<uint8_t*>(data), len) == len;
  if (file) file.close();
  LittleFS.end();
  return ok;
}

bool TerrainCacheStore::save(const void* data, size_t len) {
  if (data == nullptr || !LittleFS.begin(true)) return false;
  LittleFS.remove(kTempPath);
  File file = LittleFS.open(kTempPath, FILE_WRITE);
  const bool written = file &&
      file.write(static_cast<const uint8_t*>(data), len) == len;
  if (file) file.close();
  bool ok = written;
  if (ok) {
    LittleFS.remove(kPath);
    ok = LittleFS.rename(kTempPath, kPath);
  }
  if (!ok) LittleFS.remove(kTempPath);
  LittleFS.end();
  return ok;
}

void TerrainCacheStore::remove() {
  if (!LittleFS.begin(true)) return;
  LittleFS.remove(kPath);
  LittleFS.remove(kTempPath);
  LittleFS.end();
}
}  // namespace core::platform
