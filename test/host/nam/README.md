# NAM test files

- `A2.nam`: the A2 example model of [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (`example_models/A2.nam`, commit 0b3d3c9), MIT License (`LICENSE-NeuralAmpModelerCore`). An A2 `SlimmableContainer` with a Lite (3-channel) and a Full (8-channel) network.
- `A2_lite_ref.f32`: NeuralAmpModelerCore's output for that model's Lite network on `test_nam`'s test signal (1 s at 48 kHz, raw 32-bit floats). `test_nam` checks `NamA2` against it.

To make the reference again (needs a NeuralAmpModelerCore checkout with its submodules, and clang++):

```
./make_reference.sh /path/to/NeuralAmpModelerCore
```
