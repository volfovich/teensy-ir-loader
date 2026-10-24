#include "Key.h"

#include "ui_port.h"

namespace ui {

UI_COLD Key::Event Key::update(bool down, uint32_t now) {
  if (down != raw_) {
    raw_ = down;
    changedAt_ = now;
  }
  if (raw_ != stable_ && now - changedAt_ >= kDebounceMs) {
    stable_ = raw_;
    if (stable_) {
      pressedAt_ = now;
      held_ = false;
      return kPress;
    }
    return held_ ? kNone : kRelease;
  }
  if (stable_ && !held_ && now - pressedAt_ >= kHoldMs) {
    held_ = true;
    return kHold;
  }
  return kNone;
}

}  // namespace ui
