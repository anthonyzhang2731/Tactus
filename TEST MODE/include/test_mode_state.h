#pragma once

#include <stdint.h>

namespace tactus {

constexpr uint16_t TEST_LIMIT = 400;
// Match normal touchpad firmware's continuous LOW validation period.
constexpr uint32_t TOUCH_PRESS_MS = 150;
constexpr uint32_t TOUCH_RELEASE_MS = 30;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;
constexpr uint32_t HIT_FEEDBACK_MS = 120;

// One event per qualified press, with a stable release before rearming.
// A switch already held at initialization must first be released.
class QualifiedPress {
 public:
  constexpr QualifiedPress(uint32_t pressMs, uint32_t releaseMs)
      : pressMs_(pressMs), releaseMs_(releaseMs) {}

  constexpr void reset(uint32_t now, bool pressed) {
    rawPressed_ = pressed;
    rawSince_ = now;
    armed_ = false;
  }

  constexpr bool update(uint32_t now, bool pressed) {
    if (pressed != rawPressed_) {
      rawPressed_ = pressed;
      rawSince_ = now;
    }
    const uint32_t stableMs = now - rawSince_;
    if (!rawPressed_) {
      if (stableMs >= releaseMs_) armed_ = true;
    } else if (armed_ && stableMs >= pressMs_) {
      armed_ = false;
      return true;
    }
    return false;
  }

  constexpr bool needsRelease() const { return !armed_; }

 private:
  uint32_t pressMs_;
  uint32_t releaseMs_;
  uint32_t rawSince_ = 0;
  bool rawPressed_ = false;
  bool armed_ = false;
};

enum class TestPhase { Ready, Running, Complete };

class TouchTest {
 public:
  constexpr void begin(uint32_t now, bool buttonPressed, bool padPressed) {
    phase_ = TestPhase::Ready;
    count_ = 0;
    lastHit_ = 0;
    button_.reset(now, buttonPressed);
    pad_.reset(now, padPressed);
  }

  // Return true only for a newly accepted touchpad press.
  constexpr bool update(uint32_t now, bool buttonPressed, bool padPressed) {
    const bool startPressed = button_.update(now, buttonPressed);
    if (startPressed && phase_ != TestPhase::Running) {
      phase_ = TestPhase::Running;
      count_ = 0;
      lastHit_ = 0;
      pad_.reset(now, padPressed);
    }

    if (phase_ != TestPhase::Running || !pad_.update(now, padPressed)) {
      return false;
    }

    ++count_;
    lastHit_ = now;
    if (count_ == TEST_LIMIT) {
      phase_ = TestPhase::Complete;
      // A button press begun during the run must not erase the final result.
      button_.reset(now, buttonPressed);
    }
    return true;
  }

  constexpr TestPhase phase() const { return phase_; }
  constexpr uint16_t count() const { return count_; }
  constexpr bool needsRelease() const { return pad_.needsRelease(); }
  constexpr bool hitFeedback(uint32_t now) const {
    return phase_ == TestPhase::Running && count_ > 0 &&
           uint32_t(now - lastHit_) < HIT_FEEDBACK_MS;
  }

 private:
  QualifiedPress button_{BUTTON_DEBOUNCE_MS, BUTTON_DEBOUNCE_MS};
  QualifiedPress pad_{TOUCH_PRESS_MS, TOUCH_RELEASE_MS};
  TestPhase phase_ = TestPhase::Ready;
  uint16_t count_ = 0;
  uint32_t lastHit_ = 0;
};

}  // namespace tactus
