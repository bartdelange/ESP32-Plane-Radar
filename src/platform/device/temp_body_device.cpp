#include "core/platform.h"

#include <LittleFS.h>

namespace core::platform {
namespace {
constexpr char kPath[] = "/terrain-tile.tmp";
constexpr size_t kMaxBytes = 256 * 1024;

class FileBodyReader final : public BodyReader {
 public:
  explicit FileBodyReader(File& file) : file_(file) {}
  int read() override { return file_.read(); }
  size_t readBytes(char* buf, size_t len) override {
    return file_.readBytes(buf, len);
  }
 private:
  File& file_;
};

bool s_mounted = false;
}  // namespace

bool TemporaryBody::prepare() {
  if (!s_mounted) s_mounted = LittleFS.begin(true);
  return s_mounted;
}

bool TemporaryBody::store(BodyReader& body) {
  if (!s_mounted) return false;
  File file = LittleFS.open(kPath, FILE_WRITE);
  if (!file) return false;
  char chunk[512];
  size_t total = 0;
  for (;;) {
    const size_t n = body.readBytes(chunk, sizeof(chunk));
    if (n == 0) break;
    total += n;
    if (total > kMaxBytes ||
        file.write(reinterpret_cast<const uint8_t*>(chunk), n) != n) {
      file.close();
      LittleFS.remove(kPath);
      return false;
    }
  }
  file.close();
  return total > 0;
}

bool TemporaryBody::replay(BodyFn on_body) {
  if (!s_mounted || on_body == nullptr) return false;
  File file = LittleFS.open(kPath, FILE_READ);
  if (!file) return false;
  FileBodyReader body(file);
  const bool ok = on_body(body);
  file.close();
  return ok;
}

void TemporaryBody::clear() {
  if (!s_mounted) return;
  LittleFS.remove(kPath);
  LittleFS.end();
  s_mounted = false;
}
}  // namespace core::platform

