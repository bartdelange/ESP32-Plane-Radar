#include "ui/radar_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "config.h"
#include "core/geo.h"
#include "core/platform.h"
#include "ui/display.h"
#include "ui/display_font.h"
#include "core/adsb.h"
#include "core/settings.h"
#include "core/tag_collision.h"
#include "core/tag_content.h"
#include "core/track_history.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/runway_overlay.h"
#include "ui/terrain_overlay.h"

namespace ui {
namespace radar {

uint16_t kColorBackground = 0x0000;
uint16_t kColorGrid = 0x0320;
uint16_t kColorLabel = 0xFFFF;
uint16_t kColorCenter = 0xFFFF;
uint16_t kColorAircraft = 0x001F;
uint16_t kColorTrackVector = 0xFFFF;
uint16_t kColorTagType = 0x5DFF;
uint16_t kColorTagAltitude = 0xFFE0;
uint16_t kColorTagRoute = 0x07E0;
uint16_t kColorVertClimb = 0x07E0;
uint16_t kColorVertDescent = 0xFD20;
uint16_t kColorTrackTrail[4] = {};
uint16_t kColorRunway = 0x4D5F;
uint16_t kColorRunwayLabel = 0x7DFF;
uint16_t kColorTerrain[kTerrainBandCount] = {};

}  // namespace radar

namespace {

bool s_label_metrics_ready = false;
bool s_cardinal_use_vlw = false;
bool s_scale_use_vlw = false;
float s_cardinal_vlw_size = 0.56f;
float s_scale_vlw_size = 0.50f;
float s_tag_vlw_size = 0.56f;
const lgfx::GFXfont* s_cardinal_gfx = &fonts::FreeSansBold12pt7b;
const lgfx::GFXfont* s_scale_gfx = &fonts::FreeSansBold9pt7b;
const lgfx::GFXfont* s_tag_gfx = &fonts::FreeSansBold12pt7b;

bool s_tag_label_metrics_ready = false;
bool s_tag_use_vlw = false;

int s_scale_label_max_w = 0;
int s_scale_label_h = 0;

lgfx::LovyanGFX* s_draw = &tft;
LGFX_Sprite s_frame(&tft);
bool s_frame_ready = false;
bool s_tag_cycle_active = false;
unsigned long s_tag_cycle_phase_drawn = 0;

class DrawScope {
 public:
  explicit DrawScope(lgfx::LovyanGFX& gfx) : prev_(s_draw) { s_draw = &gfx; }
  ~DrawScope() { s_draw = prev_; }

 private:
  lgfx::LovyanGFX* prev_;
};

int absDiff(int a, int b) { return std::abs(a - b); }

int measureGfxHeight(const lgfx::GFXfont& font) {
  tft.setFont(&font);
  tft.setTextSize(1);
  return tft.fontHeight();
}

int measureVlwHeight(float size) {
  tft.setTextSize(size);
  return tft.fontHeight();
}

float findVlwSizeForHeight(int target_px) {
  float lo = 0.25f;
  float hi = 1.2f;
  for (int i = 0; i < 16; ++i) {
    const float mid = (lo + hi) * 0.5f;
    if (measureVlwHeight(mid) < target_px) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

void applyScaleStyle();

const lgfx::GFXfont* pickGfxFontClosest(
    int target_px, const lgfx::GFXfont* const* candidates, size_t count) {
  const lgfx::GFXfont* best = candidates[0];
  int best_diff = absDiff(measureGfxHeight(*best), target_px);

  for (size_t i = 1; i < count; ++i) {
    const int diff = absDiff(measureGfxHeight(*candidates[i]), target_px);
    if (diff < best_diff) {
      best_diff = diff;
      best = candidates[i];
    }
  }
  return best;
}

void initLabelMetrics() {
  if (s_label_metrics_ready) {
    return;
  }

  const int cardinal_target = radar::kCardinalLabelHeightPx;

  if (displayFontIsSmooth()) {
    s_cardinal_use_vlw = true;
    s_cardinal_vlw_size = findVlwSizeForHeight(cardinal_target);
    const int cardinal_h = measureVlwHeight(s_cardinal_vlw_size);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    s_scale_use_vlw = true;
    s_scale_vlw_size = findVlwSizeForHeight(scale_target);
  } else {
    const lgfx::GFXfont* cardinal_candidates[] = {&fonts::FreeSansBold12pt7b,
                                                  &fonts::FreeSansBold9pt7b};
    s_cardinal_gfx =
        pickGfxFontClosest(cardinal_target, cardinal_candidates, 2);
    s_cardinal_use_vlw = false;

    const int cardinal_h = measureGfxHeight(*s_cardinal_gfx);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    const lgfx::GFXfont* scale_candidates[] = {&fonts::FreeSansBold9pt7b,
                                               &fonts::FreeSansBold12pt7b};
    s_scale_gfx = pickGfxFontClosest(scale_target, scale_candidates, 2);
    s_scale_use_vlw = false;
  }

  applyScaleStyle();
  s_scale_label_h = tft.fontHeight();
  s_scale_label_max_w = 0;
  char label[12];
  for (bool miles : {false, true}) {
    radar::formatRing3Label(label, sizeof(label),
                            core::settings::kMaxRangeKm, miles);
    const int w = tft.textWidth(label);
    if (w > s_scale_label_max_w) {
      s_scale_label_max_w = w;
    }
  }

  s_label_metrics_ready = true;
}

void initTagLabelMetrics() {
  if (s_tag_label_metrics_ready) {
    return;
  }

  const int target = radar::kAircraftTagLabelHeightPx;
  if (displayFontIsSmooth()) {
    s_tag_use_vlw = true;
    s_tag_vlw_size = findVlwSizeForHeight(target);
  } else {
    const lgfx::GFXfont* tag_candidates[] = {&fonts::FreeSansBold12pt7b,
                                               &fonts::FreeSansBold9pt7b};
    s_tag_gfx = pickGfxFontClosest(target, tag_candidates, 2);
    s_tag_use_vlw = false;
  }

  s_tag_label_metrics_ready = true;
}

void initPalette() {
  radar::kColorBackground =
      displayColor565(radar::kBgR, radar::kBgG, radar::kBgB);
  radar::kColorGrid =
      displayColor565(radar::kGridR, radar::kGridG, radar::kGridB);
  radar::kColorLabel = displayColor565(255, 255, 255);
  radar::kColorCenter = displayColor565(255, 255, 255);
  radar::kColorAircraft = displayColor565(radar::kAircraftR, radar::kAircraftG,
                                           radar::kAircraftB);
  radar::kColorTrackVector =
      displayColor565(radar::kTrackR, radar::kTrackG, radar::kTrackB);
  radar::kColorTagType =
      displayColor565(radar::kTagTypeR, radar::kTagTypeG, radar::kTagTypeB);
  radar::kColorTagAltitude =
      displayColor565(radar::kTagAltR, radar::kTagAltG, radar::kTagAltB);
  radar::kColorTagRoute =
      displayColor565(radar::kTagRouteR, radar::kTagRouteG, radar::kTagRouteB);
  radar::kColorVertClimb =
      displayColor565(radar::kVertClimbR, radar::kVertClimbG,
                      radar::kVertClimbB);
  radar::kColorVertDescent = displayColor565(
      radar::kVertDescentR, radar::kVertDescentG, radar::kVertDescentB);
  for (int i = 0; i < 4; ++i) {
    const int scale = i + 1;
    radar::kColorTrackTrail[i] = displayColor565(
        radar::kTrackR * scale / 5, radar::kTrackG * scale / 5,
        radar::kTrackB * scale / 5);
  }
  radar::kColorRunway =
      displayColor565(radar::kRunwayR, radar::kRunwayG, radar::kRunwayB);
  radar::kColorRunwayLabel = displayColor565(
      radar::kRunwayLabelR, radar::kRunwayLabelG, radar::kRunwayLabelB);
  for (int i = 0; i < radar::kTerrainBandCount; ++i) {
    radar::kColorTerrain[i] = displayColor565(
        radar::kTerrainBandR[i], radar::kTerrainBandG[i], radar::kTerrainBandB[i]);
  }
}

/** Current view, rebuilt on demand from the live location and range preset. */
core::geo::Viewport viewport() {
  core::geo::Viewport vp;
  vp.center_lat = core::settings::lat();
  vp.center_lon = core::settings::lon();
  vp.center_x = radar::kCenterX;
  vp.center_y = radar::kCenterY;
  vp.outer_radius_px = radar::kGridOuterRadius;
  vp.outer_km = radar::rangeCurrent().outer_km;
  return vp;
}

void drawBeyondRingDot(int x, int y) {
  s_draw->fillSmoothCircle(x, y, radar::kBeyondRingDotRadiusPx,
                           radar::kColorAircraft);
}

int speedLineLengthPx(float gs_knots) {
  if (gs_knots <= 0.0f) {
    return 0;
  }

  // Fixed screen scale: 60 s horizon at gs, not tied to current range zoom.
  constexpr float kKmPerKnotPerHorizon =
      1.852f * radar::kAircraftTrackHorizonSec / 3600.0f;
  const float px =
      gs_knots * kKmPerKnotPerHorizon * radar::kGridOuterRadius /
      radar::kAircraftTrackRefOuterKm * radar::kAircraftTrackLengthScale;

  const int len = static_cast<int>(px + 0.5f);
  if (len < radar::kAircraftSpeedLineMinPx) {
    return radar::kAircraftSpeedLineMinPx;
  }
  return len;
}

void noseTip(int cx, int cy, float heading_deg, int* tip_x, int* tip_y) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  *tip_x = cx + static_cast<int>(lroundf(sinf(rad) * radar::kAircraftNoseLenPx));
  *tip_y = cy - static_cast<int>(lroundf(cosf(rad) * radar::kAircraftNoseLenPx));
}

void drawHeadingTriangleSized(int cx, int cy, float heading_deg, int nose_len,
                              int tail_len, int tail_half, uint16_t color) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  const float sin_h = sinf(rad);
  const float cos_h = cosf(rad);

  int tip_x = 0;
  int tip_y = 0;
  tip_x = cx + static_cast<int>(lroundf(sin_h * nose_len));
  tip_y = cy - static_cast<int>(lroundf(cos_h * nose_len));

  const int base_x =
      cx - static_cast<int>(lroundf(sin_h * static_cast<float>(tail_len)));
  const int base_y =
      cy + static_cast<int>(lroundf(cos_h * static_cast<float>(tail_len)));

  const int wing_x = static_cast<int>(lroundf(cos_h * tail_half));
  const int wing_y = static_cast<int>(lroundf(sin_h * tail_half));

  s_draw->fillTriangle(tip_x, tip_y, base_x + wing_x, base_y + wing_y,
                       base_x - wing_x, base_y - wing_y, color);
}

void drawAircraftSymbol(int cx, int cy, float heading_deg, bool highlighted) {
  if (highlighted) {
    constexpr int kOutlinePx = 1;
    drawHeadingTriangleSized(
        cx, cy, heading_deg, radar::kAircraftNoseLenPx + kOutlinePx,
        radar::kAircraftTailLenPx + kOutlinePx,
        radar::kAircraftTailHalfPx + kOutlinePx, radar::kColorLabel);
  }
  drawHeadingTriangleSized(cx, cy, heading_deg, radar::kAircraftNoseLenPx,
                           radar::kAircraftTailLenPx,
                           radar::kAircraftTailHalfPx, radar::kColorAircraft);
}

void drawSpeedVector(int cx, int cy, float heading_deg, float track_deg,
                     float gs_knots, uint16_t color) {
  const int len = speedLineLengthPx(gs_knots);
  if (len <= 0) {
    return;
  }

  int tip_x = 0;
  int tip_y = 0;
  noseTip(cx, cy, heading_deg, &tip_x, &tip_y);

  constexpr float kDegToRad = 0.01745329252f;
  const float rad = track_deg * kDegToRad;
  int ex = tip_x + static_cast<int>(lroundf(sinf(rad) * len));
  int ey = tip_y - static_cast<int>(lroundf(cosf(rad) * len));
  core::geo::clipPointToOuterRing(viewport(), radar::kGridOuterRadius, tip_x,
                                  tip_y, &ex, &ey);
  if (ex == tip_x && ey == tip_y) {
    return;
  }
  s_draw->drawWideLine(tip_x, tip_y, ex, ey, radar::kAircraftTrackLineHalfWidth,
                       color);
}

void applyTagStyle() {
  if (s_tag_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_tag_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_tag_gfx);
  }
}

struct TagLine {
  const char* text;
  uint16_t color;
};

constexpr int kMaxTagLines = 5;

struct TagLayout {
  TagLine lines[kMaxTagLines] = {};
  int line_count = 0;
  int anchor_x = 0;
  int top = 0;
  int line_h = 0;
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  textdatum_t datum = textdatum_t::top_left;
  char altitude[16] = {};
  char route[44] = {};
};

void buildAircraftTag(int x, int y, const core::adsb::Aircraft& plane,
                      bool compact, TagLayout* out) {
  initTagLabelMetrics();
  applyTagStyle();
  *out = TagLayout{};
  TagLayout& tag = *out;

  if (plane.callsign[0] != '\0')
    tag.lines[tag.line_count++] = {plane.callsign, radar::kColorLabel};
  if (!compact) {
    const auto mode = ui::radar::airlineDisplay();
    const char* airline = nullptr;
    if (mode == ui::radar::AirlineDisplay::kAbbrev && plane.airline != nullptr)
      airline = plane.airline->short_name;
    else if (mode == ui::radar::AirlineDisplay::kFullName)
      airline = core::airlines::preferredFullName(plane.airline,
                                                   plane.route_airline);
    if (airline != nullptr && airline[0] != '\0')
      tag.lines[tag.line_count++] = {airline, radar::kColorLabel};
  }
  if (!compact && plane.type[0] != '\0')
    tag.lines[tag.line_count++] = {plane.type, radar::kColorTagType};
  if (core::tag_content::showAltitudeLine(plane.alt)) {
    const auto direction = core::adsb::verticalDirection(plane.vertical_rate_fpm);
    const char* marker = direction == core::adsb::VerticalDirection::kClimb
                             ? "^ "
                         : direction == core::adsb::VerticalDirection::kDescent
                             ? "v "
                             : "";
    snprintf(tag.altitude, sizeof(tag.altitude), "%s%s", marker, plane.alt);
    const uint16_t color = direction == core::adsb::VerticalDirection::kClimb
                               ? radar::kColorVertClimb
                           : direction == core::adsb::VerticalDirection::kDescent
                               ? radar::kColorVertDescent
                               : radar::kColorTagAltitude;
    tag.lines[tag.line_count++] = {tag.altitude, color};
  }
  if (core::tag_content::showRouteLine(
          compact, core::settings::showRoutes(), plane.route_origin,
          plane.route_destination)) {
    snprintf(tag.route, sizeof(tag.route), "%s > %s",
             plane.route_origin[0] ? plane.route_origin : "?",
             plane.route_destination[0] ? plane.route_destination : "?");
    tag.lines[tag.line_count++] = {tag.route, radar::kColorTagRoute};
  }
  if (tag.line_count == 0) return;

  tag.line_h = s_draw->fontHeight();
  int block_w = 0;
  for (int i = 0; i < tag.line_count; ++i) {
    const int w = s_draw->textWidth(tag.lines[i].text);
    if (w > block_w) {
      block_w = w;
    }
  }
  const int block_h =
      core::tag_content::blockHeight(tag.line_h, tag.line_count);
  int ly = y - block_h / 2;

  const int symbol_half =
      radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx;
  // West (left): tag toward center on the right; east (right): tag on the left.
  const bool tag_on_right = x < radar::kCenterX;
  int anchor_x = 0;
  if (tag_on_right) {
    anchor_x = x + symbol_half + radar::kAircraftLabelGapPx;
    anchor_x = std::min(anchor_x, radar::kSize - block_w - 1);
    tag.datum = textdatum_t::top_left;
    tag.x0 = anchor_x;
    tag.x1 = anchor_x + block_w;
  } else {
    anchor_x = x - symbol_half - radar::kAircraftLabelGapPx;
    anchor_x = std::max(anchor_x, block_w + 1);
    tag.datum = textdatum_t::top_right;
    tag.x0 = anchor_x - block_w;
    tag.x1 = anchor_x;
  }
  ly = std::max(1, std::min(ly, radar::kSize - block_h - 1));
  tag.anchor_x = anchor_x;
  tag.top = ly;
  tag.y0 = ly;
  tag.y1 = ly + block_h;
}

void drawAircraftTag(const TagLayout& tag) {
  applyTagStyle();
  s_draw->setTextDatum(tag.datum);
  int y = tag.top;
  for (int i = 0; i < tag.line_count; ++i) {
    s_draw->setTextColor(tag.lines[i].color, radar::kColorBackground);
    s_draw->drawString(tag.lines[i].text, tag.anchor_x, y);
    y += tag.line_h;
  }
}

struct AircraftDrawItem {
  size_t index = 0;
  int x = 0;
  int y = 0;
  int dist_sq = 0;
};

struct BeyondDotDrawItem {
  int x = 0;
  int y = 0;
  int dist_sq = 0;
};

void sortDrawItemsFarFirst(AircraftDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const AircraftDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

void sortBeyondDotsFarFirst(BeyondDotDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const BeyondDotDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

void drawTrackPath(const core::adsb::Aircraft& plane) {
  const core::track::Point* points = nullptr;
  const size_t count = core::track::path(plane.hex, &points);
  if (count < 2) return;
  const core::geo::Viewport vp = viewport();
  for (size_t i = 1; i < count; ++i) {
    const auto a_off = core::geo::offsetKmFromCenter(vp, points[i - 1].lat,
                                                     points[i - 1].lon);
    const auto b_off =
        core::geo::offsetKmFromCenter(vp, points[i].lat, points[i].lon);
    const bool a_inside = core::geo::isInsideOuterRingKm(vp, a_off.dist_km, 0);
    const bool b_inside = core::geo::isInsideOuterRingKm(vp, b_off.dist_km, 0);
    if (!a_inside && !b_inside) continue;
    auto a = core::geo::latLonToScreen(vp, points[i - 1].lat,
                                       points[i - 1].lon);
    auto b =
        core::geo::latLonToScreen(vp, points[i].lat, points[i].lon);
    if (a_inside && !b_inside) {
      core::geo::clipPointToOuterRing(vp, radar::kGridOuterRadius, a.x, a.y,
                                      &b.x, &b.y);
    } else if (!a_inside && b_inside) {
      core::geo::clipPointToOuterRing(vp, radar::kGridOuterRadius, b.x, b.y,
                                      &a.x, &a.y);
    }
    const size_t shade = std::min<size_t>(3, (i * 4) / count);
    s_draw->drawWideLine(a.x, a.y, b.x, b.y, 1.0f,
                         radar::kColorTrackTrail[shade]);
  }
}

void drawAircraft() {
  initLabelMetrics();

  const size_t n = core::adsb::aircraftCount();
  const core::adsb::Aircraft* planes = core::adsb::aircraftList();

  AircraftDrawItem items[core::adsb::kMaxAircraft];
  BeyondDotDrawItem dots[core::adsb::kMaxAircraft];
  size_t draw_count = 0;
  size_t dot_count = 0;

  const core::geo::Viewport vp = viewport();
  constexpr int kInset = radar::kAircraftInsideRingInsetPx;
  const int rim_r = radar::kCenterX - radar::kBeyondRingScreenMarginPx;

  for (size_t i = 0; i < n; ++i) {
    const core::geo::Offset off =
        core::geo::offsetKmFromCenter(vp, planes[i].lat, planes[i].lon);

    if (core::geo::isInsideOuterRingKm(vp, off.dist_km, kInset)) {
      const core::geo::Point p =
          core::geo::latLonToScreen(vp, planes[i].lat, planes[i].lon);
      items[draw_count].index = i;
      items[draw_count].x = p.x;
      items[draw_count].y = p.y;
      items[draw_count].dist_sq = core::geo::distSqFromCenter(vp, p.x, p.y);
      ++draw_count;
      continue;
    }

    core::geo::Point dot;
    if (!core::geo::rimPointForDistantTarget(vp, planes[i].lat, planes[i].lon,
                                             kInset, rim_r, &dot)) {
      continue;
    }
    dots[dot_count].x = dot.x;
    dots[dot_count].y = dot.y;
    dots[dot_count].dist_sq = core::geo::distSqFromCenter(vp, dot.x, dot.y);
    ++dot_count;
  }

  sortBeyondDotsFarFirst(dots, dot_count);
  for (size_t d = 0; d < dot_count; ++d) {
    drawBeyondRingDot(dots[d].x, dots[d].y);
  }

  sortDrawItemsFarFirst(items, draw_count);
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    const int x = items[d].x;
    const int y = items[d].y;
    drawTrackPath(planes[i]);
    drawSpeedVector(x, y, planes[i].nose_deg, planes[i].track_deg,
                    planes[i].gs_knots, radar::kColorTrackVector);
  }
  // Only tagged aircraft count toward the compact threshold. Targets beyond the
  // outer ring are drawn as bare rim dots with no tag at all, so counting them
  // would collapse a handful of readable tags because of traffic that is not
  // even on the disc.
  const bool compact = core::tag_content::useCompactMode(draw_count);

  static TagLayout tags[core::adsb::kMaxAircraft];
  core::tag_collision::Bounds bounds[core::adsb::kMaxAircraft];
  bool tag_visible[core::adsb::kMaxAircraft] = {};
  bool highlighted[core::adsb::kMaxAircraft] = {};
  for (size_t d = 0; d < draw_count; ++d) {
    buildAircraftTag(items[d].x, items[d].y, planes[items[d].index], compact,
                     &tags[d]);
    bounds[d].x0 = tags[d].x0;
    bounds[d].y0 = tags[d].y0;
    bounds[d].x1 = tags[d].x1;
    bounds[d].y1 = tags[d].y1;
    bounds[d].visible = tags[d].line_count > 0;
  }
  const unsigned long phase =
      core::platform::nowMs() / config::kTagCycleIntervalMs;
  s_tag_cycle_phase_drawn = phase;
  core::tag_collision::select(bounds, draw_count, phase, tag_visible,
                              highlighted);
  s_tag_cycle_active = false;
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    drawAircraftSymbol(items[d].x, items[d].y, planes[i].nose_deg,
                       highlighted[d]);
    if (tag_visible[d]) drawAircraftTag(tags[d]);
    if (highlighted[d]) s_tag_cycle_active = true;
  }
}

void applyCardinalStyle() {
  if (s_cardinal_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_cardinal_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_cardinal_gfx);
  }
}

void applyScaleStyle() {
  if (s_scale_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_scale_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_scale_gfx);
  }
}

void drawCardinalLabel(const char* text, int x, int y, textdatum_t datum) {
  applyCardinalStyle();
  s_draw->setTextDatum(datum);
  s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawScaleLabelWithBackground(const char* text, int x, int y) {
  applyScaleStyle();
  s_draw->setTextDatum(textdatum_t::middle_right);

  const int tw = s_draw->textWidth(text);
  const int th = s_draw->fontHeight();
  constexpr int kPadX = 3;
  constexpr int kPadY = 2;

  const int left = x - tw - kPadX;
  const int top = y - th / 2 - kPadY;

  s_draw->fillRect(left, top, tw + kPadX * 2, th + kPadY * 2,
                   radar::kColorBackground);
  s_draw->setTextColor(radar::kColorGrid, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawGridRing(int cx, int cy, int r, uint16_t color) {
  if (r <= 0) {
    return;
  }
  const int thickness =
      std::max(1, static_cast<int>(radar::kGridStrokeHalfWidth * 2.0f));
  for (int i = 0; i < thickness && r - i > 0; ++i) {
    s_draw->drawCircle(cx, cy, r - i, color);
  }
}

void drawRings(int cx, int cy, int outer_radius) {
  for (int i = 1; i <= radar::kRingCount; ++i) {
    const int r = (outer_radius * i) / radar::kRingCount;
    drawGridRing(cx, cy, r, radar::kColorGrid);
  }
}

void drawCrosshairs(int cx, int cy, int radius, uint16_t color) {
  s_draw->drawWideLine(cx, cy - radius, cx, cy + radius,
                       radar::kGridStrokeHalfWidth, color);
  s_draw->drawWideLine(cx - radius, cy, cx + radius, cy,
                       radar::kGridStrokeHalfWidth, color);
}

void drawCenterDot(int cx, int cy) {
  s_draw->fillSmoothCircle(cx, cy, radar::kCenterDotRadius, radar::kColorCenter);
}

void drawCardinalLabels() {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int edge = radar::kSize - 1;

  drawCardinalLabel("N", cx, radar::kCardinalNorthOffsetY, textdatum_t::top_center);
  drawCardinalLabel("S", cx, edge + radar::kCardinalSouthOffsetY,
                    textdatum_t::bottom_center);
  drawCardinalLabel("W", 0, cy, textdatum_t::middle_left);
  drawCardinalLabel("E", edge, cy, textdatum_t::middle_right);
}

int scaleLabelAnchorX(int cx, int outer_radius) {
  return cx + outer_radius - radar::kScaleGapFromOuterRing;
}

void drawScaleLabel(int cx, int cy, int outer_radius) {
  char scale_label[12];
  radar::formatCurrentRing3Label(scale_label, sizeof(scale_label));
  drawScaleLabelWithBackground(scale_label,
                               scaleLabelAnchorX(cx, outer_radius), cy);
}

template <typename Gfx>
void drawStaticGrid(Gfx& gfx) {
  initLabelMetrics();
  const DrawScope scope(gfx);
  displayFontEnsureLoaded(gfx);
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int grid_r = radar::kGridOuterRadius;

  gfx.fillScreen(radar::kColorBackground);
  terrain::drawTerrainBackground(gfx);
  drawRings(cx, cy, grid_r);
  drawCrosshairs(cx, cy, grid_r, radar::kColorGrid);
  initPalette();
  runway::drawLargeAirportRunways(gfx);
  drawCenterDot(cx, cy);
  drawCardinalLabels();
  drawScaleLabel(cx, cy, grid_r);
  gfx.setTextDatum(textdatum_t::top_left);
}

bool ensureFrameSprite() {
  if (s_frame_ready) {
    return true;
  }
  s_frame.setColorDepth(16);
  if (!s_frame.createSprite(radar::kSize, radar::kSize)) {
    core::platform::logf("radar: frame sprite alloc failed\n");
    return false;
  }
  s_frame_ready = true;
  return true;
}

// Double-buffered frame: composite the grid AND aircraft into the off-screen
// sprite, then blit it to the panel in a single pushSprite. Because the panel
// is updated in one pass, labels never show an erase/redraw gap — no flicker.
void renderFrame() {
  drawStaticGrid(s_frame);  // opens its own DrawScope(s_frame)
  {
    const DrawScope scope(s_frame);
    drawAircraft();
  }
  s_frame.pushSprite(0, 0);
  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace

void radarDisplayDraw() {
  initPalette();
  initLabelMetrics();

  if (ensureFrameSprite()) {
    renderFrame();
    return;
  }

  // Fallback when the sprite can't be allocated: draw straight to the panel.
  const DrawScope scope(tft);
  drawStaticGrid(tft);
  drawAircraft();
  tft.setTextDatum(textdatum_t::top_left);
}

void radarDisplayRefreshAircraft() {
  initPalette();

  if (ensureFrameSprite()) {
    renderFrame();
    return;
  }

  radarDisplayDraw();
}

void radarDisplayTick() {
  if (!s_tag_cycle_active) return;
  const unsigned long phase =
      core::platform::nowMs() / config::kTagCycleIntervalMs;
  if (phase != s_tag_cycle_phase_drawn) radarDisplayRefreshAircraft();
}

}  // namespace ui
