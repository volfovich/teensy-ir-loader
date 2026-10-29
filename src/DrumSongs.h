// DrumSongs.h - the drum songs: the built-in ones (songs/*.txt) and the MIDI
// files in /DRUMS on the SD card, as one numbered list, and selecting one.
#pragma once

#include "DrumSong.h"

// Rebuilds the list of MIDI files in /DRUMS (call after the card is mounted).
void scanDrumSongs();

// The built-in songs first (1..), then the SD files.
int drumSongTotal();
// The display name of song n (1-based); a built-in song's title, or a file's
// name without its folder and extension.
const char* drumSongTitle(int n);
// The command name of song n: a built-in song's name or a file's name
// without its extension.
const char* drumSongName(int n);
bool drumSongFromSd(int n);
// n (1-based) for a number or a (case-insensitive) name; 0 for "off"; -1 if
// there is no such song.
int findDrumSongIndex(const char* s);

// Selects song n (0 = off, the style plays). Loads an SD file first; stops
// the drums while it does and starts them again (with the count-in) if they
// were playing. The tempo becomes the song's (unless keepTempo, used when
// restoring the saved song at startup). False (and no song) if it fails.
// Use setParam(ui::kDrumSong, n), which also saves it.
bool selectDrumSong(int n, bool keepTempo = false);
// Loop bars first..last (1-based) of the song; 0 = the whole song.
void setDrumLoop(int first, int last);
int drumLoopFirst();
int drumLoopLast();
// The song selected (nullptr: none).
const irdsp::DrumSong* currentDrumSong();
