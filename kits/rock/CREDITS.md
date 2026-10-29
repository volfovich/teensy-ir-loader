# Credits: the "rock" kit

The kick, snare and closed hi-hat (`kick-9.wav`, `snare-9.wav`, `hat-closed-9.wav`) are the project's own samples, made from `originals/` (`Kick1.wav`, `Snare1.wav`, `Closed1.wav`). They are not part of the AVL kit or its license.

The other samples in this folder come from the **AVL Drumkits** ("Red Zeppelin" kit) by Glen MacArthur, AV Linux / Bandshed:

- Project: https://www.bandshed.net/avldrumkits/
- Source used: https://github.com/studiorack/avl-drumkits (the `Samples/` FLAC files)

They are licensed under the **Creative Commons Attribution-ShareAlike 3.0 Unported** license (CC BY-SA 3.0): https://creativecommons.org/licenses/by-sa/3.0/

These WAVs are an adaptation, and they are shared under the same license. They were changed as follows:

- **Selection:** one velocity layer for each sound (rim, open hat, ride, crash, rack tom, floor tom). The source files are listed in `kit.txt`.
- **Trimming:** the leading silence was cut, and the tails were shortened with a fade-out (lengths in `kit.txt`).
- **Levels:** each sample was scaled to its drum's peak level in `kit.txt` times its velocity.
- **Format:** 16-bit mono 48 kHz WAV, resampled from the 44.1 kHz sources.

To rebuild all the WAVs from the originals, run `python3 tools/import_kit.py kits/rock`.
