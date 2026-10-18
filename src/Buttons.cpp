#include "Buttons.h"

#include <Arduino.h>

#include "AudioGraph.h"
#include "IRLibrary.h"
#include "Settings.h"
#include "config.h"

struct Button {
  int pin;
  bool stable = true, lastRead = true;  // pull-up: released = HIGH
  uint32_t changedAt = 0;
  // Returns true once per press.
  bool pressed() {
    if (pin < 0) return false;
    bool r = digitalReadFast(pin);
    uint32_t now = millis();
    if (r != lastRead) { lastRead = r; changedAt = now; }
    if (r != stable && now - changedAt > 15) {
      stable = r;
      return !r;
    }
    return false;
  }
};
static Button btnNext{BUTTON_NEXT_PIN}, btnPrev{BUTTON_PREV_PIN}, btnBypass{BUTTON_BYPASS_PIN},
    btnReverb{BUTTON_REVERB_PIN};

void buttonsBegin() {
  for (Button* b : {&btnNext, &btnPrev, &btnBypass, &btnReverb})
    if (b->pin >= 0) pinMode(b->pin, INPUT_PULLUP);
}

void serviceButtons() {
  if (btnNext.pressed()) loadRelative(+1);
  if (btnPrev.pressed()) loadRelative(-1);
  if (btnBypass.pressed()) setBypass(!g_settings.bypass);
  if (btnReverb.pressed()) setReverbOn(!g_settings.reverbOn);
}
