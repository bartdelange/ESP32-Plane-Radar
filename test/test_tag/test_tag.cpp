#include <unity.h>

#include "core/tag_collision.h"
#include "core/tag_content.h"

namespace collision = core::tag_collision;

void test_route_setting_controls_content_and_height(void) {
  TEST_ASSERT_TRUE(
      core::tag_content::showRouteLine(false, true, "Amsterdam", "London"));
  TEST_ASSERT_FALSE(
      core::tag_content::showRouteLine(false, false, "Amsterdam", "London"));
  TEST_ASSERT_FALSE(
      core::tag_content::showRouteLine(true, true, "Amsterdam", "London"));
  TEST_ASSERT_EQUAL_INT(52, core::tag_content::blockHeight(13, 4));
  TEST_ASSERT_EQUAL_INT(39, core::tag_content::blockHeight(13, 3));
}

void test_independent_tags_are_visible_without_highlights(void) {
  const collision::Bounds bounds[] = {{0, 0, 20, 20, true},
                                      {100, 100, 120, 120, true}};
  bool visible[2] = {};
  bool highlighted[2] = {};
  collision::select(bounds, 2, 0, visible, highlighted);
  TEST_ASSERT_TRUE(visible[0]);
  TEST_ASSERT_TRUE(visible[1]);
  TEST_ASSERT_FALSE(highlighted[0]);
  TEST_ASSERT_FALSE(highlighted[1]);
}

void test_collision_selection_and_highlight_share_one_owner(void) {
  const collision::Bounds bounds[] = {{10, 10, 50, 50, true},
                                      {30, 30, 70, 70, true},
                                      {40, 40, 80, 80, true}};
  bool visible[3] = {};
  bool highlighted[3] = {};
  collision::select(bounds, 3, 0, visible, highlighted);
  TEST_ASSERT_TRUE(visible[0]);
  TEST_ASSERT_TRUE(highlighted[0]);
  TEST_ASSERT_FALSE(visible[1]);
  TEST_ASSERT_FALSE(highlighted[1]);
  TEST_ASSERT_FALSE(visible[2]);
  TEST_ASSERT_FALSE(highlighted[2]);

  collision::select(bounds, 3, 1, visible, highlighted);
  TEST_ASSERT_FALSE(highlighted[0]);
  TEST_ASSERT_TRUE(visible[1]);
  TEST_ASSERT_TRUE(highlighted[1]);
  TEST_ASSERT_FALSE(highlighted[2]);
}

void test_collision_ending_removes_highlight(void) {
  const collision::Bounds bounds[] = {{10, 10, 30, 30, true},
                                      {100, 100, 120, 120, true}};
  bool visible[2] = {};
  bool highlighted[2] = {};
  collision::select(bounds, 2, 4, visible, highlighted);
  TEST_ASSERT_TRUE(visible[0]);
  TEST_ASSERT_TRUE(visible[1]);
  TEST_ASSERT_FALSE(highlighted[0]);
  TEST_ASSERT_FALSE(highlighted[1]);
}

void test_beyond_ring_dot_is_never_selected_or_highlighted(void) {
  const collision::Bounds bounds[] = {{0, 0, 20, 20, true},
                                      {0, 0, 20, 20, false}};
  bool visible[2] = {};
  bool highlighted[2] = {};
  collision::select(bounds, 2, 0, visible, highlighted);
  TEST_ASSERT_TRUE(visible[0]);
  TEST_ASSERT_FALSE(highlighted[0]);
  TEST_ASSERT_FALSE(visible[1]);
  TEST_ASSERT_FALSE(highlighted[1]);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_route_setting_controls_content_and_height);
  RUN_TEST(test_independent_tags_are_visible_without_highlights);
  RUN_TEST(test_collision_selection_and_highlight_share_one_owner);
  RUN_TEST(test_collision_ending_removes_highlight);
  RUN_TEST(test_beyond_ring_dot_is_never_selected_or_highlighted);
  return UNITY_END();
}
