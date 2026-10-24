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
// Delay (between the IR and the reverb): two types sharing the same lines
//   0 = stereo ping-pong (ported from HexeFX), tape-like
//   1 = studio 80s: wet/dry/wet dual delay after the digital racks of the era
// ---------------------------------------------------------------------------
// Longest delay time. The two float delay lines need 2 x 4 bytes per sample
// of delay (750 ms = 265 KB), split between RAM1 and RAM2; with the IR and the
// reverb that leaves about 40 KB free in RAM1 and 145 KB in RAM2. RAM1 is the
// limit: every 100 ms takes about 17.6 KB from each. Lower it to free memory.
#define DELAY_MAX_MS 750
// Defaults, changeable over USB serial and saved to EEPROM.
#define DEFAULT_DELAY_ON 0
#define DEFAULT_DELAY_TYPE 1        // 0 = ping-pong, 1 = studio 80s
#define DEFAULT_DELAY_MIX 30        // 0 = dry only, 100 = wet only
#define DEFAULT_DELAY_TIME_MS 380   // 20 .. DELAY_MAX_MS
#define DEFAULT_DELAY_FEEDBACK 35   // repeats, 0 .. 100
#define DEFAULT_DELAY_TONE 60       // brightness of the repeats, 100 = no treble loss
#define DEFAULT_DELAY_MOD 15        // modulation depth (tape-like wobble), 0 .. 100
// Studio 80s only (the ping-pong ignores them), 0 .. 100.
#define DEFAULT_DELAY_DUCK 40       // repeats duck while you play (0 = off, 100 = -24 dB)
#define DEFAULT_DELAY_DRIVE 20      // rack input preamp grit
#define DEFAULT_DELAY_ERA 40        // 0 = late-80s 16-bit / 16 kHz, 100 = early 10-bit / 6 kHz
// Ping-pong fixed voicing, 0.0 .. 1.0.
#define DELAY_BASS_CUT 0.2f   // bass loss per repeat
#define DELAY_TREBLE 1.0f     // treble of the delayed signal (1 = flat)
#define DELAY_BASS 1.0f       // bass of the delayed signal (1 = flat)
#define DELAY_MOD_RATE 0.35f  // modulation rate (cubic, 0.35 = about 0.7 Hz)
#define DELAY_INERTIA 0.9f    // how slowly a new delay time is reached (pitch glide)
// Studio 80s fixed voicing.
#define DELAY80_RIGHT_RATIO 0.75f        // right time = left time x this (0.1 .. 1)
#define DELAY80_CROSS 0.2f               // share of each side's repeats fed to the other (0 .. 0.5)
#define DELAY80_BASS_CUT 0.15f           // bass loss per repeat (0 .. 1)
#define DELAY80_MOD_RATE_HZ 0.8f         // chorus rate of the tail
#define DELAY80_DUCK_THRESHOLD_DB -36.0f // input level where ducking starts (full 12 dB above)
#define DELAY80_DUCK_RELEASE_MS 200.0f   // how fast the repeats swell back after you stop

// ---------------------------------------------------------------------------
// Reverb (stereo plate after the delay, ported from HexeFX)
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
#define BUTTON_DELAY_PIN 9

#define AUDIO_MEMORY_BLOCKS 20
