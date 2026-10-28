// Controls.h - the panel's rotary encoder (interrupt-driven, so no steps are
// lost while loop() is busy loading an IR) and its three keys: the encoder
// push, CONFIRM and BACK (pins in config.h).
#pragma once

#include "Key.h"

struct ControlEvents {
  int detents;  // turned since the last call, + = clockwise
  ui::Key::Event push, confirm, back;
};

void controlsBegin();
// Call every loop() pass.
ControlEvents readControls(uint32_t nowMs);
