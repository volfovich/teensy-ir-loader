# Credits: the "rock" kit

The samples in this folder come from the **AVL Drumkits** ("Red Zeppelin" kit) by Glen MacArthur, AV Linux / Bandshed:

- Project: https://www.bandshed.net/avldrumkits/
- Source used: https://github.com/studiorack/avl-drumkits (the `Samples/` FLAC files)

They are licensed under the **Creative Commons Attribution-ShareAlike 3.0 Unported** license (CC BY-SA 3.0): https://creativecommons.org/licenses/by-sa/3.0/

This folder's WAVs are an adaptation, and they are shared under the same license. They were changed as follows:

- **Selection:** two velocity layers for the kick, three for the snare, four for the closed hi-hat and one for each other sound. The source files are listed in `kit.txt`.
- **Trimming:** the leading silence was cut, and the tails were shortened with a fade-out (lengths in `kit.txt`).
- **Levels:** each sample was scaled to its drum's peak level in `kit.txt` times its velocity.
- **Format:** 16-bit mono 44.1 kHz WAV.

To rebuild them from the originals, run `python3 tools/import_kit.py kits/rock`.
