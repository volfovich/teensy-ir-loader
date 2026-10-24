// Key.h - debounced push button with press, short release and hold events.
#pragma once

#include <stdint.h>

namespace ui {

class Key {
 public:
  enum Event : uint8_t {
    kNone,
    kPress,    // just went down (after the debounce)
    kRelease,  // came back up after a press shorter than the hold time
    kHold,     // has been down for kHoldMs (once per press)
  };
  static constexpr uint32_t kDebounceMs = 15, kHoldMs = 600;

  // down: the current (raw) level, true = pressed. Call often (every
  // loop() pass); returns at most one event per call.
  Event update(bool down, uint32_t nowMs);
  bool down() const { return stable_; }

 private:
  bool stable_ = false, raw_ = false, held_ = false;
  uint32_t changedAt_ = 0, pressedAt_ = 0;
};

}  // namespace ui
