// config.h - all user-tunable settings in one place.
#pragma once

// ---------------------------------------------------------------------------
// Latency / block size
// ---------------------------------------------------------------------------
// The audio block size is set in platformio.ini (-D AUDIO_BLOCK_SAMPLES=32).
// The convolver's partition size equals the block size, so the convolution
// itself adds zero latency; total latency is ~2 blocks of I/O buffering plus
// the codec's converters:
//   128 -> ~5.8 ms   64 -> ~2.9 ms   32 -> ~1.5 ms   16 -> ~0.7 ms  (+ codec)
// Smaller blocks cost more CPU (see README for numbers).

// ---------------------------------------------------------------------------
// Impulse responses
// ---------------------------------------------------------------------------
// Longest IR in samples at 44.1 kHz. 8192 samples = 186 ms, far longer than
// any guitar cabinet needs. Must be a multiple of AUDIO_BLOCK_SAMPLES.
#define IR_MAX_LENGTH 8192

// Folders scanned for .wav files (FAT is case-insensitive: "/IR" == "/ir").
#define IR_SCAN_DIRS {"/", "/IR"}
#define IR_MAX_FILES 128
#define IR_PATH_MAX 96

// Source samples decoded per load. 36864 covers IR_MAX_LENGTH at 192 kHz.
#define IR_LOAD_SCRATCH 36864

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
// Levels (defaults; changeable over USB serial and saved to EEPROM)
// ---------------------------------------------------------------------------
// Output gain applied after the convolution, in dB.
#define DEFAULT_OUTPUT_GAIN_DB -6.0f
// SGTL5000 line-in sensitivity: 0 = 3.12 Vpp (least sensitive, best for a
// hot tube-amp line out) ... 5 = 1.33 Vpp ... 15 = 0.24 Vpp.
#define DEFAULT_LINE_IN_LEVEL 0
// SGTL5000 line-out level: 13 = 3.16 Vpp (loudest) ... 29 = 1.29 Vpp ... 31 = 1.16 Vpp.
#define DEFAULT_LINE_OUT_LEVEL 29
// Headphone volume 0.0 .. 1.0 (0.8 is already loud).
#define DEFAULT_HP_VOLUME 0.5f

// ---------------------------------------------------------------------------
// Reverb (stereo plate after the IR, ported from HexeFX)
// ---------------------------------------------------------------------------
// Defaults, changeable over USB serial and saved to EEPROM (all 0 .. 100).
#define DEFAULT_REVERB_ON 0
#define DEFAULT_REVERB_MIX 25   // 0 = dry only, 100 = wet only
#define DEFAULT_REVERB_SIZE 50  // reverb time (about 1.1 .. 6 s)
#define DEFAULT_REVERB_TONE 50  // treble in the tail, 100 = brightest
// Fixed voicing, 0.0 .. 1.0.
#define REVERB_DIFFUSION 1.0f  // lower values give more distinct echoes
#define REVERB_LODAMP 0.2f     // bass loss in the tail (keeps it from getting muddy)
#define REVERB_LOWPASS 0.0f    // treble cut on the reverb output
#define REVERB_HIPASS 0.0f     // bass cut on the reverb output

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------
#define SDCARD_CS_PIN 10  // SD slot on the Teensy 4 Audio Shield (Rev D)

// Optional momentary buttons to GND (internal pull-ups). -1 disables.
// Free pins with the audio shield fitted: 0-5, 9, 14, 16, 17, 22
// (15 is the shield's volume-pot pad, 6 its memory-chip CS).
#define BUTTON_NEXT_PIN 2
#define BUTTON_PREV_PIN 3
#define BUTTON_BYPASS_PIN 4
#define BUTTON_REVERB_PIN 5

#define AUDIO_MEMORY_BLOCKS 20
