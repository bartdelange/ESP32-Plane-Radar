/**
 * Device HttpClient: Arduino HTTPClient over WiFiClientSecure.
 *
 * Both polling loops below exist because a request can block for seconds, and
 * the config portal and the BOOT button have to stay alive across it — hence
 * the PollFn invoked on every iteration of the connect loop and of the refill
 * that feeds the body reader.
 *
 * TLS is deliberately unverified (setInsecure), matching the shipping firmware.
 */

#include "core/platform.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include <cstring>
#include <climits>

namespace core::platform {

namespace {

constexpr int kConnectTimeoutMs = 2000;
constexpr size_t kReadChunk = 512;
bool s_request_active = false;

void poll(PollFn fn) {
  if (fn != nullptr) {
    fn();
  }
}

const char* requestCategory(const char* url) {
  if (url != nullptr && strstr(url, "opendata.adsb.fi") != nullptr)
    return "ADSB";
  if (url != nullptr && strstr(url, "api.adsbdb.com") != nullptr)
    return "ROUTE";
  if (url != nullptr && strstr(url, "elevation-tiles-prod") != nullptr)
    return "TERRAIN";
  return "OTHER";
}

void logHeap(const char* category, const char* event, int status) {
  const size_t free_heap = ESP.getFreeHeap();
  const size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (status == INT_MIN) {
    logf("HTTP %s %s free=%u free8=%u largest=%u\n", category, event,
         static_cast<unsigned>(free_heap), static_cast<unsigned>(free_8bit),
         static_cast<unsigned>(largest));
  } else {
    logf("HTTP %s %s status=%d free=%u free8=%u largest=%u\n", category,
         event, status, static_cast<unsigned>(free_heap),
         static_cast<unsigned>(free_8bit),
         static_cast<unsigned>(largest));
  }
}

/**
 * Reads the body straight off the socket, one kReadChunk window at a time.
 *
 * The window is the only buffer in the path — the decoder consumes bytes as
 * fast as they arrive, so peak RAM does not grow with the size of the reply.
 * The poll loop around the refill is what the old read loop did: a request can
 * block for seconds and the portal and BOOT button have to survive it.
 */
class StreamBodyReader : public BodyReader {
 public:
  StreamBodyReader(HTTPClient& http, WiFiClient& stream, unsigned long deadline,
                   PollFn fn)
      : http_(http),
        stream_(stream),
        remaining_(http.getSize()),
        deadline_(deadline),
        poll_fn_(fn) {}

  int read() override {
    if (pos_ >= len_ && !fill()) {
      return -1;
    }
    return static_cast<unsigned char>(buffer_[pos_++]);
  }

  size_t readBytes(char* buf, size_t len) override {
    size_t written = 0;
    while (written < len) {
      if (pos_ >= len_ && !fill()) {
        break;
      }
      const size_t take = len_ - pos_ < len - written ? len_ - pos_
                                                      : len - written;
      memcpy(buf + written, buffer_ + pos_, take);
      pos_ += take;
      written += take;
    }
    return written;
  }

 private:
  /** Blocks until at least one byte lands, the body ends, or time runs out. */
  bool fill() {
    pos_ = 0;
    len_ = 0;
    // A negative Content-Length means the server did not send one; then the
    // close of the connection is the only end-of-body marker there is.
    if (remaining_ == 0) {
      return false;
    }

    while (millis() < deadline_) {
      poll(poll_fn_);
      const int available = stream_.available();
      if (available > 0) {
        size_t want = sizeof(buffer_);
        if (remaining_ > 0 && static_cast<size_t>(remaining_) < want) {
          want = static_cast<size_t>(remaining_);
        }
        const size_t got = stream_.readBytes(buffer_, want);
        if (got > 0) {
          len_ = got;
          if (remaining_ > 0) {
            remaining_ -= static_cast<int>(got);
          }
          return true;
        }
      }
      if (!http_.connected() && stream_.available() <= 0) {
        return false;
      }
      delay(1);
    }
    return false;
  }

  HTTPClient& http_;
  WiFiClient& stream_;
  int remaining_;
  unsigned long deadline_;
  PollFn poll_fn_;
  char buffer_[kReadChunk];
  size_t pos_ = 0;
  size_t len_ = 0;
};

}  // namespace

int HttpClient::getStatus(const char* url, BodyFn on_body,
                          unsigned long timeout_ms, PollFn fn) {
  const char* category = requestCategory(url);
  if (s_request_active) {
    logf("HTTP %s deferred: another request is active\n", category);
    return 0;
  }
  struct ActiveRequest {
    ActiveRequest() { s_request_active = true; }
    ~ActiveRequest() { s_request_active = false; }
  } active_request;
  int result = 0;
  int log_status = 0;
  struct RequestLog {
    const char* category;
    int* result;
    ~RequestLog() { logHeap(category, "end", *result); }
  } request_log{category, &log_status};
  logHeap(category, "start", INT_MIN);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    logf("http: begin failed\n");
    return result;
  }

  // adsb.fi/Cloudflare may use HTTP/1.1 chunked transfer encoding. The
  // streaming reader deliberately consumes the socket directly, so request
  // HTTP/1.0 and receive an unframed close-delimited body instead.
  http.useHTTP10(true);
  http.setTimeout(timeout_ms);
  http.setConnectTimeout(kConnectTimeoutMs);
  poll(fn);
  // Exactly one transport attempt per logical request. Retrying GET() here on
  // NOT_CONNECTED used to create a new TLS handshake every ~5 ms after an
  // mbedTLS allocation failure; subsystem-level cadence/backoff owns retries.
  const int code = http.GET();
  log_status = code;
  if (code != HTTP_CODE_OK && code != HTTP_CODE_NOT_FOUND) {
    logf("http: HTTP %d\n", code);
    http.end();
    result = code > 0 ? code : 0;
    return result;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    http.end();
    return 0;
  }

  StreamBodyReader body(http, *stream, millis() + timeout_ms, fn);
  const bool ok = on_body(body);
  http.end();
  result = ok ? code : 0;
  log_status = result;
  return result;
}

bool HttpClient::get(const char* url, BodyFn on_body, unsigned long timeout_ms,
                     PollFn fn) {
  return getStatus(url, on_body, timeout_ms, fn) == HTTP_CODE_OK;
}

}  // namespace core::platform
