// Compile-time simulations of the actual state machine, with no hardware mocks.
// These assertions execute in the C++14 compiler; they need no connected board.
#include "test_mode_state.h"

using namespace tactus;

constexpr void startTest(TouchTest& test, uint32_t base = 0, bool heldPad = false) {
  test.begin(base, false, heldPad);
  test.update(base + 50, false, heldPad);
  test.update(base + 51, true, heldPad);
  test.update(base + 101, true, heldPad);
  test.update(base + 102, false, heldPad);
  test.update(base + 152, false, heldPad);
}

constexpr bool idleDoesNotCount() {
  TouchTest test;
  test.begin(0, false, false);
  test.update(100, false, false);
  test.update(200, false, true);
  test.update(1000, false, true);
  return test.count() == 0 && test.phase() == TestPhase::Ready;
}

constexpr bool buttonDebounceAndBootHold() {
  TouchTest test;
  test.begin(0, true, false);
  test.update(1000, true, false);
  if (test.phase() != TestPhase::Ready) return false;
  test.update(1001, false, false);
  test.update(1051, false, false);
  test.update(1052, true, false);
  test.update(1101, true, false);
  if (test.phase() != TestPhase::Ready) return false;
  test.update(1102, true, false);
  return test.phase() == TestPhase::Running && test.count() == 0;
}

constexpr bool shortAndBouncingPresses() {
  TouchTest test;
  startTest(test);
  test.update(200, false, true);
  test.update(349, false, true);
  if (test.count() != 0) return false;
  test.update(350, false, false); // A short pulse is rejected.
  test.update(351, false, true);  // LOW qualification must restart.
  test.update(500, false, true);
  if (test.count() != 0) return false;
  return test.update(501, false, true) && test.count() == 1;
}

constexpr bool holdAndReleaseBounce() {
  TouchTest test;
  startTest(test);
  test.update(200, false, true);
  test.update(350, false, true);
  test.update(10000, false, true);
  if (test.count() != 1) return false;
  test.update(10001, false, false);
  test.update(10030, false, false); // Release shorter than 30 ms.
  test.update(10031, false, true);
  test.update(11000, false, true);
  if (test.count() != 1) return false;
  test.update(11001, false, false);
  test.update(11031, false, false);
  test.update(11032, false, true);
  return test.update(11182, false, true) && test.count() == 2;
}

constexpr bool heldPadAtStart() {
  TouchTest test;
  startTest(test, 0, true);
  test.update(1000, false, true);
  if (test.count() != 0) return false;
  test.update(1001, false, false);
  test.update(1031, false, false);
  test.update(1032, false, true);
  return test.update(1182, false, true) && test.count() == 1;
}

constexpr void touch(TouchTest& test, uint32_t now, bool button = false) {
  test.update(now, button, false);
  test.update(now + 30, button, false);
  test.update(now + 31, button, true);
  test.update(now + 181, button, true);
}

constexpr bool countTo400AndFreeze() {
  TouchTest test;
  startTest(test);
  for (uint16_t i = 0; i < 410; ++i) {
    touch(test, 200 + uint32_t(i) * 200);
    const uint16_t expected = i < TEST_LIMIT ? i + 1 : TEST_LIMIT;
    if (test.count() != expected) return false;
  }
  return test.phase() == TestPhase::Complete;
}

constexpr bool ignoreActiveButtonAndRestartAfterComplete() {
  TouchTest test;
  startTest(test);
  for (uint16_t i = 0; i < TEST_LIMIT; ++i) {
    touch(test, 200 + uint32_t(i) * 200, true);
    if (test.count() != i + 1) return false;
  }
  test.update(81000, true, true); // Held button cannot reset final result.
  if (test.count() != TEST_LIMIT) return false;
  test.update(81001, false, false);
  test.update(81051, false, false);
  test.update(81052, true, false);
  test.update(81102, true, false);
  if (test.count() != 0 || test.phase() != TestPhase::Running) return false;
  touch(test, 81200);
  return test.count() == 1;
}

constexpr bool lateButtonDoesNotEraseResult() {
  TouchTest test;
  startTest(test);
  for (uint16_t i = 0; i < TEST_LIMIT - 1; ++i) {
    touch(test, 200 + uint32_t(i) * 200);
  }
  test.update(80000, false, false);
  test.update(80030, false, false);
  test.update(80031, false, true);
  test.update(80160, true, true); // Button goes down just before final touch.
  test.update(80181, true, true);
  test.update(80250, true, true);
  return test.phase() == TestPhase::Complete && test.count() == TEST_LIMIT;
}

constexpr bool feedbackDoesNotBlockNextTouch() {
  TouchTest test;
  startTest(test);
  touch(test, 200); // Accepted at 381.
  if (!test.hitFeedback(381) || !test.hitFeedback(500) || test.hitFeedback(501)) {
    return false;
  }
  touch(test, 382); // A second press can qualify long before a 2-second cooldown.
  return test.count() == 2 && test.hitFeedback(563);
}

constexpr bool clockRollover() {
  TouchTest test;
  startTest(test, UINT32_MAX - 300);
  test.update(UINT32_MAX - 100, false, true);
  test.update(48, false, true); // 149 ms elapsed.
  if (test.count() != 0) return false;
  if (!test.update(49, false, true)) return false; // 150 ms elapsed.
  if (!test.hitFeedback(168) || test.hitFeedback(169)) return false;
  touch(test, 200);
  return test.count() == 2;
}

static_assert(idleDoesNotCount(), "No counting before the start button");
static_assert(buttonDebounceAndBootHold(), "Debounce button and ignore boot hold");
static_assert(shortAndBouncingPresses(), "Require 150 ms continuous contact");
static_assert(holdAndReleaseBounce(), "One count per press, stable release required");
static_assert(heldPadAtStart(), "Do not count a pad held while starting");
static_assert(countTo400AndFreeze(), "Count beyond 255 and freeze at exactly 400");
static_assert(ignoreActiveButtonAndRestartAfterComplete(), "Protect active count and restart");
static_assert(lateButtonDoesNotEraseResult(), "Require a fresh button press after completion");
static_assert(feedbackDoesNotBlockNextTouch(), "Feedback must not impose a cooldown");
static_assert(clockRollover(), "Debounce and feedback survive millis rollover");
