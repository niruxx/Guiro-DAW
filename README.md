# Guiro

A Linux-first, pattern-based digital audio workstation in the spirit of FL Studio —
channel rack, step sequencer, piano roll, playlist, sound pads, mixer with filter
automation, and VST3 / CLAP / LV2 plugin hosting.

Built with **C++20 + Qt 6** (custom-painted widgets), audio through **JACK**, which on
modern distros runs transparently on **PipeWire**.

## Features

- **Channel rack**: FL-style step sequencer with per-step velocity (mouse wheel), swing, 8–64 steps,
  mute/solo LEDs, pan/volume knobs, mixer routing, drag-and-drop samples and plugins.
- **Piano roll**: draw, move, resize, delete, rubber-band select, transpose, duplicate,
  velocity lane, snapping, zoom.
- **Playlist**: pattern, audio and automation clips; paint, move, Ctrl+drag to copy, trim
  either edge; loop region; drop audio files straight in to create audio clips.
- **Sound pads**: 4×4 MPC-style pads (velocity by strike height, 1-4/Q-R/A-F/Z-V keys),
  drag samples onto pads, drag pads out to the rack/playlist, live recording into patterns.
- **Mixer**: master + 16 inserts, each with a state-variable filter (LP/HP/BP, cutoff,
  resonance), pan, fader, stereo peak meters, mute/solo and 8 effect slots.
- **Automation**: right-click any knob or fader → *Create automation clip*. Edit points inline
  in the playlist (double-click adds, drag moves, right-click deletes). Plugin parameters
  can be automated from the plugin's *Parameters* view.
- **Plugins**: VST3 (official SDK, MIT-licensed since 3.8), CLAP and LV2. Plugins are scanned
  in a child process so a broken plugin can't crash the DAW. Native VST3/CLAP editors are
  embedded via X11; every plugin also has a generic parameter editor.
- **Built-in sound kit**: "Guiro Basics", 16 synthesized drums and synths, generated on first run.
- **Windows plugins via Wine**: Windows VST2 `.dll`, VST3 and CLAP plugins load through
  [yabridge](https://github.com/robbert-vdh/yabridge). Open *Menu → Tools → Windows plugins (Wine)*
  (or *Windows…* in the browser's Plugins tab), add the folder your plugins were installed to,
  and Guiro syncs and scans them. Bridged plugins are tagged **Wine** in the browser.
- Native Linux VST2 (`.so`) hosting.
- Project save/load (`.guiro`, readable JSON), WAV/FLAC export, metronome, MIDI input
  (to the selected channel), recording.

## Building

Dependencies (Arch package names):

```sh
sudo pacman -S cmake qt6-base qt6-svg jack2   # or pipewire-jack
sudo pacman -S libsndfile libsamplerate lilv lv2
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
./build/guiro
```

For Windows plugins, also install Wine and yabridge:

```sh
sudo pacman -S wine-staging
yay -S yabridge-bin        # AUR; provides yabridge + yabridgectl
```

The CLAP headers and VST3 SDK are fetched automatically by CMake. Options:
`-DGUIRO_WITH_VST3=OFF`, `-DGUIRO_WITH_CLAP=OFF`, `-DGUIRO_WITH_LV2=OFF`.

## Running

| Variable | Effect |
|---|---|
| `GUIRO_WAYLAND=1` | Run as a native Wayland client (plugins then use generic editors) |
| `GUIRO_NO_JACK=1` | Skip JACK and run with the silent fallback engine |

By default Guiro runs through XWayland, because Linux plugin GUIs embed through X11.

Guiro starts with an empty project; `./build/guiro --demo` (or *Menu → File → Open demo project*)
loads a short demo song.

Headless rendering:

```sh
./build/guiro --render song.guiro out.wav   # or: --render demo out.flac
```

Plugins are searched in `~/.vst`, `/usr/lib/vst`, `~/.vst3`, `/usr/lib/vst3`, `~/.clap`, `/usr/lib/clap`, the LV2 path,
and `VST_PATH` / `VST3_PATH` / `CLAP_PATH` / `LV2_PATH`. Rescan from the browser's Plugins tab.

## Layout and shortcuts

Guiro follows FL Studio's workflow so it feels familiar: menus and a hint bar top-left
(hover anything to see what it does), the transport and pattern selector in the top bar,
the browser docked on the left, and the playlist filling the workspace with the channel
rack, piano roll, mixer and pads as floating windows on top of it.

`Space` play/stop · `L` pattern/song mode · `R` record · `F5` playlist · `F6` channel rack ·
`F7` piano roll · `F9` mixer · `Ctrl+F8` pads · `Alt+F8` browser · `F12` close all windows ·
`F4` new pattern · `F2` rename pattern · `Ctrl+R` export · `Ctrl+M` metronome · `Ctrl+.` panic

A window key raises its window; pressing it again while the window is in front hides it.

## Architecture

```
src/core     Project model, parameters, samples, file I/O, built-in kit
src/engine   Realtime engine, JACK backend, snapshot builder, DSP, exporter
src/plugins  Plugin interface, scanner/manager, VST3 / CLAP / LV2 hosts
src/ui       Main window, panels and custom widgets
```

The GUI owns the project. After each structural edit it builds an immutable **snapshot**
and swaps it in atomically. The audio thread never locks or allocates, and retired
snapshots are freed on the GUI thread once the audio callback has moved past them.
Continuous values (knobs, automation) are lock-free atomic `Param`s shared by both
sides. The engine renders in 64-frame sub-blocks for tight event and automation timing.

## Not yet implemented

Undo/redo, tempo automation, time signatures other than 4/4, audio recording,
insert-to-insert routing/sends, plugin delay compensation, and LV2 native UIs (needs suil).
