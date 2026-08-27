#pragma once

#include <cstddef>
#include <cstdint>

namespace ui {

/** Reserve the permanent RGB565 frame before Wi-Fi fragments device heap. */
bool radarDisplayInitFrame();
/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();
/** Advance time-based tag cycling without waiting for another ADS-B fetch. */
void radarDisplayTick();

}  // namespace ui
