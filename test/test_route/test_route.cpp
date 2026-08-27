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

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fold_ascii_keeps_city_names_without_exonyms);
  RUN_TEST(test_route_operator_preferred_with_local_airline_fallback);
  return UNITY_END();
}
