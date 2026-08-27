#include <unity.h>

#include "core/frame_ownership.h"

namespace cf = core::frame;

void test_transfer_must_finish_before_scratch_can_start() {
  cf::Ownership frame;
  frame.allocated();
  TEST_ASSERT_TRUE(frame.beginComposition());
  TEST_ASSERT_TRUE(frame.beginTransfer());
  TEST_ASSERT_FALSE(frame.acquireScratch());
  TEST_ASSERT_TRUE(frame.finishTransfer());
  TEST_ASSERT_TRUE(frame.acquireScratch());
}

void test_png_release_invalidates_pixels_and_requires_full_composition() {
  cf::Ownership frame;
  frame.allocated();
  TEST_ASSERT_TRUE(frame.acquireScratch());
  TEST_ASSERT_TRUE(frame.releaseScratch());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(cf::State::kInvalid),
                        static_cast<int>(frame.state()));
  TEST_ASSERT_FALSE(frame.beginTransfer());
  TEST_ASSERT_TRUE(frame.beginComposition());
  TEST_ASSERT_TRUE(frame.beginTransfer());
  TEST_ASSERT_TRUE(frame.finishTransfer());
}

void test_composition_and_scratch_are_mutually_exclusive() {
  cf::Ownership frame;
  frame.allocated();
  TEST_ASSERT_TRUE(frame.beginComposition());
  TEST_ASSERT_FALSE(frame.acquireScratch());
  TEST_ASSERT_TRUE(frame.beginTransfer());
  TEST_ASSERT_FALSE(frame.beginComposition());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_transfer_must_finish_before_scratch_can_start);
  RUN_TEST(test_png_release_invalidates_pixels_and_requires_full_composition);
  RUN_TEST(test_composition_and_scratch_are_mutually_exclusive);
  return UNITY_END();
}
