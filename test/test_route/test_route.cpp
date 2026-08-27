#include <unity.h>

#include "core/route.h"
#include "core/airlines.h"

void test_fold_ascii_keeps_city_names_without_exonyms(void) {
  char out[24];
  core::route::foldAscii("M\xC3\xA1laga", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Malaga", out);
  core::route::foldAscii("Katowice", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Katowice", out);
}

void test_route_operator_preferred_with_local_airline_fallback(void) {
  const auto* local = core::airlines::forCallsign("KLM123");
  TEST_ASSERT_EQUAL_STRING(
      "KLM Royal Dutch Airlines",
      core::airlines::preferredFullName(local, "KLM Royal Dutch Airlines"));
  TEST_ASSERT_EQUAL_STRING("KLM",
                           core::airlines::preferredFullName(local, ""));
}

void test_transient_failure_stops_remaining_route_lookups(void) {
  uint8_t remaining = 3;
  TEST_ASSERT_FALSE(core::route::consumeLookupBudget(
      /*attempted=*/true, /*transient_failure=*/true, &remaining));
  TEST_ASSERT_EQUAL_UINT8(0, remaining);
}

void test_success_consumes_only_one_route_lookup(void) {
  uint8_t remaining = 3;
  TEST_ASSERT_TRUE(core::route::consumeLookupBudget(
      /*attempted=*/true, /*transient_failure=*/false, &remaining));
  TEST_ASSERT_EQUAL_UINT8(2, remaining);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fold_ascii_keeps_city_names_without_exonyms);
  RUN_TEST(test_route_operator_preferred_with_local_airline_fallback);
  RUN_TEST(test_transient_failure_stops_remaining_route_lookups);
  RUN_TEST(test_success_consumes_only_one_route_lookup);
  return UNITY_END();
}
