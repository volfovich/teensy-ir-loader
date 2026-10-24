// ui_port.h - keeps the UI out of the Teensy's fast RAM1 (see DISPLAY.md,
// "RAM1 strategy"):
//
//   UI_COLD     on every function: it runs from flash (through the 32 KB
//               instruction cache) instead of being copied to ITCM.
//   UI_PROGMEM  on constant tables: they stay in flash instead of being
//               copied to DTCM like ordinary .rodata.
//   UI_STR("x") a string literal that stays in flash. Plain literals would
//               be copied to RAM1 too.
//
// GCC can't put inline functions, templates or constructors in a named
// section, so UI_COLD goes only on out-of-line functions in .cpp files.
// On the host all three are no-ops.
#pragma once

#if defined(__IMXRT1062__)
#define UI_COLD __attribute__((section(".flashmem")))
#define UI_PROGMEM __attribute__((section(".progmem")))
#else
#define UI_COLD
#define UI_PROGMEM
#endif

#define UI_STR(s) (__extension__({ static const char UI_PROGMEM uiStr_[] = (s); &uiStr_[0]; }))
