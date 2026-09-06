# FastResampler

A small VST3 host for Windows. It loads one VST3 sampler at a time, shows the
plugin's own editor, sends it MIDI notes and controllers from a GUI keyboard
and a set of sliders, and plays the plugin's output through WASAPI or ASIO.

Details about the project:
- Windows 10, x64. Built with MSVC as a unity build: `build.bat` compiles the
  one translation-unit root `src/fastresampler_win32.cpp`, which `#include`s
  every other source file. A macOS or Linux port adds a sibling root with the
  same module list and its own platform files.
- The code follows the Casey Muratori / Handmade Hero ethos, like FastSampler:
  plain old data structs, procedural functions, fixed-size arrays allocated up
  front, no OOP. The unavoidable classes are the VST3 interfaces a host has to
  implement (`IComponentHandler`, `IPlugFrame`) and the SDK's hosting helpers.
- Dependencies: Dear ImGui (a git clone in `3rdparty/imgui`, not tracked) and
  the VST3 SDK at `W:\VST_SDK\vst3sdk`. The ASIO SDK is not needed: the driver
  interface is declared in `src/platform/fastresampler_asio.h`.
- The `.fsi` and `.fsb` formats belong to FastSampler and are never copied
  here. `build.bat` adds the FastSampler checkout to the include path
  (`FASTSAMPLER_DIR`, by default the sibling folder `..\fastsampler`) and the
  unity root compiles its `fastsampler.pb.h/.c`, `fastsampler_fsb.h`,
  `fastsampler_pb.h` and the nanopb runtime from there. A change to the schema
  or the bank header in FastSampler is picked up by the next build of this
  project; `src/fastresampler_instrument.cpp` only fills in the messages.
- WavPack comes from the same checkout (`3rdparty/wavpack-4.80.0-dll`): the
  import library is linked and `build.bat` copies `wavpackdll.dll` next to
  the exe. The bank builder compresses the streamed part of every sample with
  it, the way FastSampler's own export does.
- Platform calls go through `src/platform/fastresampler_platform.h`. The audio
  backends (`src/platform/fastresampler_audio_*.cpp`) and the window glue
  (`src/fastresampler_view_win32.cpp`) are the only other files that may
  include OS headers.
- The colour palette is FastSampler's, copied verbatim in
  `src/ui/fastresampler_palette.h`.
- Never use heredoc for writing files. Use the write tool instead.
- Comments are short and in ASD-STE100 simplified technical english.
- UI strings capitalise only the first letter.

Layout of the window:
- Left: the side panel, one column down the whole left edge, with four tabs.
  **Plugin** shows what is loaded, its
  output meters and a close button. **MIDI** has the channel, CC sliders,
  sustain, pitch bend and all-notes-off. **Resample** builds a MIDI script
  (a note range sampled every n semitones, a list of velocities, a number of
  round robins per note with optional pre-roll notes, each note held then left
  to ring out), renders it through the plugin to one 32 bit float WAV faster
  than realtime, then splits that file into one WAV per sample named
  `Microphone_Instrument_Articulation_Note_Velocity_RRn.wav`, and finally
  builds a FastSampler instrument (`.fsi` + `.fsb`) with the samples mapped:
  one zone collection per round robin, linked by FastSampler's round robin
  effect on a shared counter, each key on its nearest sampled note (ties pitch
  down), each velocity range up to its sampled velocity. The bank keeps a
  chosen number of frames of every sample in RAM (8192 by default; 1024 to
  65536) and streams the rest, WavPack compressed, or in RAM mode holds every
  sample whole. The render runs at
  the chosen export rate (96 kHz by default, up to 192 kHz) with the plugin in
  offline mode, then the plugin returns to the device's rate. Before a render
  the free space of the output disk is checked against the full WAV and the
  sample files, when the folder is picked, when the MIDI is built, and again
  right before the render; a modal reports a shortfall and the render does
  not start. A render speed
  (realtime to 16x, or unlimited; 4x by default) paces the render so a sampler
  that streams from disk has the wall time to read its sample tails. Controller
  curves (up to eight, one controller each) are drawn over the time of one
  sample, its pre-roll, hold and gap, in a bezier or freehand editor after
  FastSampler's, and sent along with every note: a CC1 held at 127, say, so a
  sustain patch plays its loudest dynamic layer. The output folder is picked
  with the system's folder dialog. **Settings** has the folders to scan, the
  audio device and block size, the ASIO control panel, and whether the tips
  (the dim lines that explain a control) are shown.
- Right: the plugin list when nothing is loaded (double-click to load), or the
  plugin's editor under a header with its name and a close button. A plugin
  that wants more room than the window has grows the window to fit it, once,
  as far as the screen allows. Under that, the 88-key GUI keyboard, as wide
  as the plugin area. Click to play; lower on the key is louder.
- The timing sliders (hold, gap, pre-roll gap) are FastSampler's drag
  sliders: a plain drag moves in 10 ms steps, a Shift drag 100 ms per pixel,
  an Alt drag 1 ms per pixel, and they read in milliseconds below one second.

Files:
- `src/fastresampler_app.*` — the application state and the requests the UI raises.
- `src/fastresampler_host.*` — VST3 hosting: module, component, controller, processing, editor.
- `src/fastresampler_audio.*` — the audio device API; backends under `src/platform/`.
- `src/fastresampler_scan.*` — finds `.vst3` bundles and files under the configured folders.
- `src/fastresampler_settings.*` — `fastresampler_settings.txt` next to the exe.
- `src/fastresampler_midi.h` — the lock-free MIDI queue from the UI thread to the audio thread, note names.
- `src/fastresampler_resample.*` — the resampling script and the offline render worker.
- `src/fastresampler_wav.*` — the float32 WAV writer and reader.
- `src/fastresampler_instrument.*` — the `.fsi`/`.fsb` builder and the key/velocity mapping.
- `src/ui/` — the ImGui side panel, plugin list, keyboard and the controller curve editor.

Threading: the UI thread owns the plugin. The audio thread only calls
`host_plugin_process()` while `App::plugin_active` is set and raises
`App::audio_busy` around the call; the UI thread clears the flag and waits for
the counter before it unloads or reconfigures a plugin.

How to build:
1. Clone ImGui once: `git clone https://github.com/ocornut/imgui.git 3rdparty/imgui`
2. Run `cmd.exe /c build.bat`. Pass `debug` for an unoptimised build with symbols.
   Each run increments `build_number.txt`; the exe lands in `build\fastresampler.exe`.
