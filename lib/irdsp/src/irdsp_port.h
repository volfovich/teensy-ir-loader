// irdsp_port.h - placement hints that only matter on the Teensy.
//
// IRDSP_COLD puts a function in flash instead of the fast RAM1 (ITCM) the
// Teensy 4 copies all code to by default. Use it for code that never runs in
// the audio interrupt (file loading, setup); it then runs through the 32 KB
// instruction cache, which leaves RAM1 for the audio path and the stack.
// On the host it expands to nothing.
#pragma once

#if defined(__IMXRT1062__)
#define IRDSP_COLD __attribute__((section(".flashmem")))
#else
#define IRDSP_COLD
#endif
