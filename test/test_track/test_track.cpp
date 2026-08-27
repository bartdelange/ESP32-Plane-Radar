#include <unity.h>

#include "config.h"
#include "core/track_history.h"

namespace track = core::track;

void test_history_keeps_order_and_ignores_tiny_movements(void) {
  track::clear();
  track::recordAt("abc123", 52.0f, 5.0f, 100);
  track::recordAt("abc123", 52.00001f, 5.00001f, 200);
  track::recordAt("abc123", 52.001f, 5.001f, 300);
  const track::Point* points = nullptr;
  TEST_ASSERT_EQUAL_size_t(2, track::path("abc123", &points));
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 52.0f, points[0].lat);
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 52.001f, points[1].lat);
}

void test_history_survives_brief_dropout_then_expires(void) {
  track::clear();
  track::recordAt("abc123", 52.0f, 5.0f, 100);
  track::expireStaleAt(100 + config::kTrackHistoryTtlMs - 1);
  const track::Point* points = nullptr;
  TEST_ASSERT_EQUAL_size_t(1, track::path("abc123", &points));
  track::expireStaleAt(100 + config::kTrackHistoryTtlMs);
  TEST_ASSERT_EQUAL_size_t(0, track::path("abc123", &points));
}

void setUp(void) {}
void tearDown(void) {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_history_keeps_order_and_ignores_tiny_movements);
  RUN_TEST(test_history_survives_brief_dropout_then_expires);
  return UNITY_END();
}
