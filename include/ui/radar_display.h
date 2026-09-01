#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Restore the off-screen static frame and release optional cache heap for TLS. */
void radarDisplayPrepareForNetwork();
/** Advance time-based tag cycling without waiting for another ADS-B fetch. */
void radarDisplayTick();

}  // namespace ui
