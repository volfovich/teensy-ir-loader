// config.h - compile-time settings: IR handling, memory sizes, the delay's
// fixed voicing and the hardware. The settings changeable over USB serial and
// the panel (levels, delay, reverb, drums, bass), with their ranges and the
// defaults a blank EEPROM starts with, are in lib/oledui/src/ParamDefs.cpp.
#pragma once

// ---------------------------------------------------------------------------
// Latency / block size
// ---------------------------------------------------------------------------
// The audio block size is set in platformio.ini (-D AUDIO_BLOCK_SAMPLES=32).
// The convolver's partition size equals the block size, so the convolution
// itself adds zero latency; total latency is ~2 blocks of I/O buffering plus
// the codec's converters:
//   128 -> ~5.3 ms   64 -> ~2.7 ms   32 -> ~1.3 ms   16 -> ~0.7 ms  (+ codec)
// at 48 kHz (AUDIO_SAMPLE_RATE_EXACT in platformio.ini).
// Smaller blocks cost more CPU (see README for numbers).

// ---------------------------------------------------------------------------
// Impulse responses
// ---------------------------------------------------------------------------
// Longest IR in samples at 48 kHz. 4096 samples = 85 ms, longer than any
// cabinet has audible response; longer files are cut there with a short
// fade. Memory (DTCM) and convolution CPU scale with it: 8192 doubles both.
// Must be a multiple of AUDIO_BLOCK_SAMPLES.
#define IR_MAX_LENGTH 4096

// Folders scanned for .wav files (FAT is case-insensitive: "/IR" == "/ir").
#define IR_SCAN_DIRS {"/", "/IR"}
#define IR_MAX_FILES 128
#define IR_PATH_MAX 96

// Crossfade time when switching IRs (no clicks, no dropouts).
#define IR_FADE_MS 25

// Loudness matching between IRs: kEnergy (recommended), kPeak or kNone.
#define IR_NORMALIZE irdsp::Normalize::kEnergy
// Stereo IR files: kFirst = left channel only, kMix = average of channels.
#define IR_CHANNEL_MODE irdsp::ChannelMode::kFirst
// Trimming relative to the IR peak (set to 0 to disable).
//  - leading silence is pure latency, so it is removed
//  - the inaudible tail is removed to save CPU
#define IR_TRIM_LEAD_DB -70.0f
#define IR_TRIM_TAIL_DB -80.0f

// ---------------------------------------------------------------------------
// Delay (between the IR and the reverb): the studio 80s wet/dry/wet dual
// delay, after the digital racks of the era (see DELAY.md).
// ---------------------------------------------------------------------------
// Longest delay time. The two float delay lines need 2 x 4 bytes per sample
// of delay (750 ms = 288 KB at 48 kHz), one in RAM1 and one in RAM2: every
// 100 ms takes about 19 KB from each.
#define DELAY_MAX_MS 750
// Fixed voicing.
#define DELAY80_RIGHT_RATIO 0.75f        // right time = left time x this (0.1 .. 1)
#define DELAY80_CROSS 0.2f               // share of each side's repeats fed to the other (0 .. 0.5)
#define DELAY80_BASS_CUT 0.15f           // bass loss per repeat (0 .. 1)
#define DELAY80_MOD_RATE_HZ 0.8f         // chorus rate of the tail
#define DELAY80_DUCK_THRESHOLD_DB -36.0f // input level where ducking starts (full 12 dB above)
#define DELAY80_DUCK_RELEASE_MS 200.0f   // how fast the repeats swell back after you stop

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------
#define SDCARD_CS_PIN 10  // SD slot on the Teensy 4 Audio Shield (Rev D)

// OLED + encoder module (see DISPLAY.md): a 128x64 SH1106 or SSD1306 on its
// own I2C bus, Wire1 (SCL1 pin 16, SDA1 pin 17), apart from the codec's
// (Wire, pins 18/19); the encoder and the keys switch to GND (internal
// pull-ups). Other free pins with the audio shield fitted: 0, 1, 14, 22 (15
// is the shield's volume-pot pad, 6 its memory-chip CS).
#define OLED_WIRE Wire1          // Wire to share the codec's bus (pins 18/19)
#define OLED_I2C_HZ 1000000      // 400000 if the picture glitches (the SH1106 is rated for 400 kHz)
#define OLED_I2C_ADDR 0x3C       // 0x3D on some modules (tried too); -1 disables the display
#define OLED_SH1106 1            // 1 = SH1106 (1.3" modules), 0 = SSD1306 (0.96")
// SH1106 only: its RAM is 132 columns wide; the first one the glass shows.
// 2 on many modules, 0 on ours. Wrong: the line ends are cut off on one side
// and stray pixels show on the other.
#define OLED_COL_OFFSET 0
#define ENC_A_PIN 2
#define ENC_B_PIN 3
#define ENC_PUSH_PIN 4
#define KEY_CONFIRM_PIN 5
#define KEY_BACK_PIN 9
#define ENC_COUNTS_PER_DETENT 4  // a full quadrature cycle per click (ours); 2 if one click moves two steps
#define ENC_REVERSE 1            // 1 if clockwise moves the wrong way (it does on our module)

#define AUDIO_MEMORY_BLOCKS 20
