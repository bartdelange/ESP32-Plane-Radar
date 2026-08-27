/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 *
 * Shared by both destinations. Everything platform-specific goes through
 * core::platform or the wifi_setup.h seam, so this file compiles unchanged for
 * the device and for the native harness.
 */

#include "config.h"
#include "core/platform.h"
#include "ui/display.h"
#include "core/adsb.h"
#include "core/settings.h"
#include "core/tap_gesture.h"
#include "core/track_history.h"
#include "platform/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace pf = core::platform;

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;

void showRadarIfConnected() {
  if (!wifiIsConnected()) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

/**
 * Rewind g_last_adsb_fetch_ms so the next loop iteration is free to fetch
 * again (subject only to kAdsbMinRefetchMs) instead of waiting out the rest
 * of kAdsbFetchIntervalMs. A subtraction from "now" rather than an absolute
 * deadline, so it stays correct across the millis() rollover. Re-stamping on
 * every tap also keeps rapid tapping inside adsb.fi's 1 req/s limit.
 */
void scheduleAdsbFetchSoon() {
  g_last_adsb_fetch_ms = pf::nowMs() - config::kAdsbFetchIntervalMs +
                         config::kAdsbMinRefetchMs;
}

void onRangeChanged() {
  scheduleAdsbFetchSoon();
}

/** Fired by core::settings whenever lat()/lon() actually move. */
void onCenterChanged() {
  core::adsb::clear();
  core::track::clear();
  g_last_adsb_fetch_ms = pf::nowMs() - config::kAdsbFetchIntervalMs;
}

/** State change plus NVS only — the drain loop in handleBootButton() owns the
 *  (single, post-drain) repaint. Returns whether anything actually changed. */
bool applyRangeNext() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  pf::logf("Range: %s (outer ~%.0f km)\n", range_label,
           static_cast<double>(ui::radar::rangeCurrent().outer_km));
  return true;
}

/** Applies the gesture's state change; returns whether a repaint is owed. */
bool dispatchTap(core::gesture::Tap tap) {
  switch (tap) {
    case core::gesture::Tap::kSingle:
      return applyRangeNext();
    case core::gesture::Tap::kDouble:
      wifiOpenConfigPortal();
      return false;
    default:
      return false;
  }
}

void handleBootButton() {
  bootButtonPollLongPress();

  bool needs_repaint = false;
  unsigned long tap_ms = 0;
  while (bootButtonConsumeTap(&tap_ms)) {
    // Load-bearing, not incidental: resolve whatever gesture is already
    // pending at the NEW tap's own release timestamp before recording that
    // tap. Polling at "now" instead would let a single left pending from a
    // stale blocking window (for example an in-flight ADS-B fetch)
    // get silently swallowed the moment an unrelated later tap shows up,
    // instead of correctly resolving as its own single.
    if (dispatchTap(core::gesture::tapPoll(tap_ms))) {
      needs_repaint = true;
    }
    core::gesture::tapPress(tap_ms);
  }
  if (dispatchTap(core::gesture::tapPoll(pf::nowMs()))) {
    needs_repaint = true;
  }
  // Draining can yield more than one gesture per loop iteration; repaint once
  // here rather than once per gesture inside dispatchTap — a full
  // radarDisplayDraw() costs roughly 128 ms.
  if (needs_repaint && g_radar_visible && wifiIsConnected()) {
    ui::radarDisplayDraw();
  }
}

/**
 * Poll hook handed to core::adsb for use during blocking HTTP
 * I/O. Forwards to wifiLoop() and nothing else — no tap consumption, no
 * gesture dispatch, no draw. That absence is structural, not just documented:
 * an HTTP poll cannot unexpectedly repaint the display from inside a request.
 */
void pollWifi() { wifiLoop(); }

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!core::adsb::fetchUpdate(core::settings::lat(), core::settings::lon(),
                               fetch_km)) {
    handleBootButton();
    return;
  }
  ui::radarDisplayRefreshAircraft();
  handleBootButton();
}

}  // namespace

void setup() {
  pf::logInit();
  pf::logf("\nPlane Radar\n");

  bootButtonInit();
  displayInit();
  statusScreenStarting();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  core::settings::init();
  // After init(), not before: init() seeds s_lat/s_lon from storage, and the
  // hook must not fire on that initial load, only on later moves.
  core::settings::setCenterChangedFn(onCenterChanged);
  core::settings::setRangeChangedFn(onRangeChanged);
  core::adsb::setPollFn(pollWifi);
  if (wifiSetupConnect()) {
    showRadarIfConnected();
    // Fetch immediately after association instead of waiting one full period.
    g_last_adsb_fetch_ms = pf::nowMs() - config::kAdsbFetchIntervalMs;
  }
}

void loop() {
  handleBootButton();
  wifiLoop();

  if (!wifiIsConnected()) {
    if (g_radar_visible) {
      pf::logf("WiFi lost — will reconnect\n");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = pf::nowMs();
    }

    const unsigned long down_ms = pf::nowMs() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        pf::nowMs() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = pf::nowMs();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
        g_last_adsb_fetch_ms = pf::nowMs() - config::kAdsbFetchIntervalMs;
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (pf::nowMs() - g_last_adsb_fetch_ms >=
               config::kAdsbFetchIntervalMs) {
      g_last_adsb_fetch_ms = pf::nowMs();
      fetchAndDrawAircraft();
    }
    ui::radarDisplayTick();
  }

  pf::sleepMs(10);
}
