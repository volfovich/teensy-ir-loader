// Buttons.h - optional next / previous / bypass buttons (pins in config.h).
#pragma once

void buttonsBegin();
// Call from loop(): debounces the buttons and acts on presses.
void serviceButtons();
