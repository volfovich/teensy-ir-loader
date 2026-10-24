// irdsp_port.h - placement hints that only matter on the Teensy.
//
// IRDSP_COLD puts a function in flash instead of the fast RAM1 (ITCM) the
// Teensy 4 copies all code to by default. Use it for code that never runs in
// the audio interrupt (file loading, setup); it then runs through the 32 KB
// instruction cache, which leaves RAM1 for the audio path and the stack.
// IRDSP_PROGMEM does the same for constant tables, which would otherwise be
// copied to RAM1 (DTCM). On the host both expand to nothing.
#pragma once

#if defined(__IMXRT1062__)
#define IRDSP_COLD __attribute__((section(".flashmem")))
#define IRDSP_PROGMEM __attribute__((section(".progmem")))
#else
#define IRDSP_COLD
#define IRDSP_PROGMEM
#endif
