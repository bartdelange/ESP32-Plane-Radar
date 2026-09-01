#include "core/platform.h"

#include <string>

namespace core::platform {
namespace {
std::string s_body;
}  // namespace

bool TemporaryBody::prepare() { return true; }

bool TemporaryBody::store(BodyReader& body) {
  s_body.clear();
  char chunk[4096];
  for (;;) {
    const size_t n = body.readBytes(chunk, sizeof(chunk));
    if (n == 0) break;
    s_body.append(chunk, n);
  }
  return !s_body.empty();
}

bool TemporaryBody::replay(BodyFn on_body) {
  if (on_body == nullptr || s_body.empty()) return false;
  MemoryBodyReader body(s_body.data(), s_body.size());
  return on_body(body);
}

void TemporaryBody::clear() { std::string().swap(s_body); }
}  // namespace core::platform

