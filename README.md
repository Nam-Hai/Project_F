# Drum machine (Teensy 4.1)

A DIY sample-based drum machine: C++ firmware for a **Teensy 4.1** with a PCM5102A DAC, an SSD1306 OLED screen, 16 step buttons, a rotary encoder and a few extra buttons. The code is developed first on a computer, with an emulator, and will be ported to the Teensy afterwards.

## What it does
- **16-step sequencer**, up to 16 tracks, one WAV sample per track (mono or stereo, PCM 16-bit or 32-bit float, 44.1 kHz).
- **Per step:** velocity, pitch, micro-timing (±50 % of a step), up to 4 notes per step (rolls), and balance.
- **Per track:** gain, quantize strength, stereo width (mono ↔ wide), balance, and an ADSR envelope (a short Release removes the clicks when a note replaces another).
- **Live recording:** a keyboard/rec mode plays the track at 24 pitches, with a one-bar pre-count and a take that keeps the real timing of each hit.
- **Loops:** up to 12 saved in a JSON profile (loaded and saved from the UI).

## Layout
| Path | Content |
|---|---|
| `core/` | Portable code, no OS dependency, no allocation in the audio path: engine, sequencer, sample player, mixer, profile codec, UI logic and 128×64 1-bit canvas. |
| `platform/desktop/` | PC side: miniaudio output, WAV loading, file storage, and the SDL2 + Dear ImGui **emulator** (OLED view + simulated panel). |
| `tests/` | Unit and integration tests. |
| `third_party/` | miniaudio, Dear ImGui, jsmn. |
| `samples/`, `profiles/` | Sample library and the saved profile. |

## Build and run
Requires CMake 3.16+ and a C++17 compiler; the emulator also needs SDL2 (`brew install sdl2` on macOS).

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build      # run the tests
./build/emulator            # run the emulator
```

Profiles are stored in a compact binary format (`profiles/default.dmp`), the same file on the PC and on the Teensy. `profile_tool` converts them to readable JSON and back:

```sh
./build/profile_tool bin2json profiles/default.dmp [out.json]   # binary -> JSON (standard output if no file)
./build/profile_tool json2bin in.json out.dmp                   # JSON -> binary
./build/profile_tool check profiles/default.dmp                 # verify the file (CRC, bounds) and print a summary
```

The Teensy 4.1 firmware builds with [PlatformIO](https://platformio.org) (`brew install platformio`): `pio run` compiles `core/` and `platform/teensy/` for the board (the first run downloads the toolchain into `~/.platformio`), `pio run -t upload` flashes it.

## Docs (in French)
- `feuille_de_route_technique_architecture_logicielle.md`: architecture and roadmap.
- `fiche_technique_mat_rielle_composants.md`: hardware and bill of materials.
- `r_capitulatif_du_projet_points_d_attention.md`: recap and pitfalls (audio callback, memory, noise).
- `plan_*.md`: design notes per feature (UI, loops, step parameters, keyboard, recording, stereo, envelope).

## Status
Everything runs on the computer. The Teensy port (audio output, SD card, screen, buttons, PSRAM for samples) has not started yet.
