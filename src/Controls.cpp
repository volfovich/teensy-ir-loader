#include "Controls.h"

#include <Arduino.h>

#include "Quadrature.h"
#include "config.h"

// Quarter steps counted by the interrupt, and the A/B levels it last saw.
static volatile int32_t g_count = 0;
static volatile uint8_t g_ab = 0;
static int32_t g_used = 0;  // quarter steps already returned as detents
static ui::Key g_push, g_confirm, g_back;

// Runs from ITCM: it is short and must not wait on a flash cache miss.
static void encoderIsr() {
  const uint8_t ab = (uint8_t)((digitalReadFast(ENC_A_PIN) << 1) | digitalReadFast(ENC_B_PIN));
  g_count += ui::quadStep(g_ab, ab);
  g_ab = ab;
}

FLASHMEM void controlsBegin() {
  for (int pin : {ENC_A_PIN, ENC_B_PIN, ENC_PUSH_PIN, KEY_CONFIRM_PIN, KEY_BACK_PIN}) pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(50);  // let the pull-ups settle before the first read
  g_ab = (uint8_t)((digitalReadFast(ENC_A_PIN) << 1) | digitalReadFast(ENC_B_PIN));
  attachInterrupt(digitalPinToInterrupt(ENC_A_PIN), encoderIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B_PIN), encoderIsr, CHANGE);
}

FLASHMEM ControlEvents readControls(uint32_t now) {
  ControlEvents e;
  // Whole detents only; the rest stays for the next call. Rounded toward
  // zero both ways, so a half-turned knob left alone never ticks.
  const int32_t pending = g_count - g_used;
  e.detents = (int)(pending / ENC_COUNTS_PER_DETENT);
  g_used += e.detents * ENC_COUNTS_PER_DETENT;
  if (ENC_REVERSE) e.detents = -e.detents;
  e.push = g_push.update(!digitalReadFast(ENC_PUSH_PIN), now);
  e.confirm = g_confirm.update(!digitalReadFast(KEY_CONFIRM_PIN), now);
  e.back = g_back.update(!digitalReadFast(KEY_BACK_PIN), now);
  return e;
}
