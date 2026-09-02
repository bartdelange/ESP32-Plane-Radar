#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Rebuild the static frame off-screen before TLS; never pushes it to the panel. */
void radarDisplayPrepareForNetwork();
/** Advance time-based tag cycling without waiting for another ADS-B fetch. */
void radarDisplayTick();

}  // namespace ui
