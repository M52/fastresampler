# FastResampler

A small plugin host for Windows. It loads one VST3 plugin or one VST 2
instrument (VSTi) at a time, shows the plugin's own editor, sends it MIDI
notes and controllers from a GUI keyboard and a set of sliders, and plays the
plugin's output through WASAPI or ASIO.

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
  the VST3 SDK, version 3.8.1 or later (the first under the MIT licence), at
  `W:\vst-sdk_3.8.1\VST_SDK\vst3sdk` or wherever `VST3_SDK_DIR` points. The
  ASIO SDK is not needed: the driver interface is declared in
  `src/platform/fastresampler_asio.h`. The VST 2 SDK is not needed either,
  and Steinberg no longer ships it: the plugin ABI (the `AEffect` structure,
  the opcodes, the event and time structures) is declared in
  `src/fastresampler_vst2.h`.
- VST 2 instruments: a `.dll` under a scanned folder counts when its export
  table names the VST 2 entry point, read from the file's headers without
  running it. Whether such a DLL is an instrument or an effect only shows
  once it runs, so the scan opens each new DLL once, reads its name and its
  synth flag, closes it, and keeps the answer in
  `fastresampler_vsti_cache.txt` next to the exe, keyed on the file's size
  and write time. The entry is written as "opening" before the DLL runs; one
  still marked so at the next start took the scan down and is skipped until
  the file changes. Effects and shell plugins are left out of the list, and
  refused when loaded by path. MIDI reaches a VST 2 plugin as it is, so the
  CC sliders and the controller curves always work with one.
- The `.fsi` and `.fsb` formats belong to FastSampler and are never copied
  here. `build.bat` adds the FastSampler checkout to the include path
  (`FASTSAMPLER_DIR`, by default the sibling folder `..\fastsampler`) and the
  unity root compiles its `fastsampler.pb.h/.c`, `fastsampler_fsb.h`,
  `fastsampler_pb.h` and the nanopb runtime from there. A change to the schema
  or the bank header in FastSampler is picked up by the next build of this
  project; `src/fastresampler_instrument.cpp` only fills in the messages.
- The YAML reader is the other way round: a resampling project is this
  project's own format, so FastSampler's parser is copied into
  `src/fastresampler_yaml.*` and nothing about the projects depends on that
  checkout.
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
  down), each velocity range up to its sampled velocity. RR are dynamic
  layers, the box after the round robin count, turns that count into a
  number of dynamic layers for resampling sustains: every note is still
  played that many times, but each controller curve gets a box that ties it
  to one layer (three CC1 curves at three levels, say, one per layer; a
  curve on All layers plays in every pass), pre-roll is off, the files end
  in `_dynn.wav`, and the instrument built from the run is a crossfading
  sustain rather than a round robin. Every sample is mapped twice: the held
  note, from the note-on to the note-off, as a looped sustain in the
  collection of its layer (`dyn1`, `dyn2`, ...), with the loop from 250 ms
  after the note-on to 250 ms before the note-off and a 200 ms equal power
  crossfade at the join, both shrinking on a short hold; and what rings out
  past the note-off as a release sample in a collection of its own
  (`dyn1-release`, ...), which FastSampler plays when the key comes up. That
  collection names its layer as the one whose releases it holds, and every
  release zone gets a 10 ms attack: a release begins at the note-off of a
  note that was still sounding, so started flat it would step from silence
  to that level and click on every key-up.
  Every zone has Ignore vel set, as the layers carry the dynamics. Every
  collection, the release ones too, carries FastSampler's gain effect,
  driven by the controller the layer curves were drawn for (the mod wheel
  when none is) through a curve between -36 and 0 dB: layer k of n sits at
  0 dB at k/n of the controller's range, the first from the bottom up to
  1/n and the last at the top, and between two peaks the lower layer fades
  out and the upper in by an equal power law, so the controller moves
  through the layers at a steady level. The curves share FastSampler's first
  curve group, so its editor draws them over one another.
  Capture legato transitions, in the Legato section, adds the moves between
  the notes to the run. After every note has been sampled, each capture
  holds one note, presses the next one over it, lets the first go after the
  overlap, and holds the second; whatever the plugin plays between them --
  a slur, a re-bow, a portamento -- is what the sample keeps. The
  intervals are widths in semitones, "1, 2, 3, 4, 5, 6, 7" by default, each
  recorded up and down. Only a width that was captured gets a move: a slur
  of any other width changes note without one, because a move is a recording
  of two particular notes and one of the wrong width would land on the wrong
  note, which is heard as a passing note that was never played. Leave no
  gaps, then, and spend the run on widths rather than on the notes the moves
  start from -- those have a step of their own, but a move is rooted on the
  note it is left from, so a note between two captured ones plays the nearer
  move transposed, and transposing a move keeps its width and so still lands
  where it should. The note landed on is
  pressed at each of the legato velocities, which is what a library that
  reads its legato speed off the velocity needs -- Cinematic Studio Strings
  has three zones, 0-64, 65-100 and 101-127, so "30, 80, 110" captures its
  slow, medium and fast slur -- and the note left is pressed at the same
  velocity. Every move carries two frames: the departure, where it leaves the
  note it is played from, and the arrival, where it has reached the note it
  goes to. Both take a nominal time per velocity; with Detect on, the build
  reads the real pair out of the audio instead (see
  `src/fastresampler_legato.h`) and keeps the nominal for a move it cannot
  hear apart. The sample is then cut one margin before the departure and one
  margin past the arrival, so every move keeps the same amount of the note
  being left in front of it and of the note landed on behind it, whatever the
  library's own delay was. The instrument puts the moves in one collection
  per interval, mapped and rooted on the note each was left from, and gives
  every collection of notes FastSampler's legato effect with one link row per
  interval, each row answering for its own width and no other. The
  crossfades are the sampler's: it starts a move one fade-in before the
  departure, fading the note being left across that same time, and fades the
  move out at the arrival while the sustain of the note landed on rises, so
  between the two frames nothing but the move itself is heard. The margin is
  room for those fades, not something that is played as such, and a
  transition zone carries no envelope of its own. The part in front of the
  departure is held in RAM, so a longer margin costs memory per move. A
  looped sustain needs no way out of its
  loop: the legato effect fades the voice it leaves, and the voice keeps
  looping under that fade. With dynamic layers every layer gets its own
  moves and its own links, and its releases stay in the collection of their
  own, which names the layer they belong to: a note a move has taken over
  must leave no release behind, and what records that the move happened is
  kept with the layer's notes, so the releases are read off the layer rather
  than off themselves. The bank keeps a
  chosen number of frames of every sample in RAM (8192 by default; 1024 to
  65536) and streams the rest, WavPack compressed, or in RAM mode holds every
  sample whole. It stores those samples at the chosen bit depth, 16-bit by
  default or 24-bit, in the resident part and the streamed one both: the two
  depths the format carries, and the two FastSampler's own export offers.
  The render runs at
  the chosen export rate (96 kHz by default, up to 192 kHz) with the plugin in
  offline mode, then the plugin returns to the device's rate. Before a render
  the free space of the output disk is checked against the full WAV and the
  sample files, when the folder is picked, when the MIDI is built, and again
  right before the render; a modal reports a shortfall and the render does
  not start. A render speed
  (realtime to 16x, or unlimited; 4x by default) paces the render so a sampler
  that streams from disk has the wall time to read its sample tails. Controller
  curves (up to 64, one controller each) are drawn over the time of one
  sample, its pre-roll, hold and gap, in a bezier or freehand editor after
  FastSampler's, and sent along with every note: a CC1 held at 127, say, so a
  sustain patch plays its loudest dynamic layer. The tab is laid out in
  sections under capitalised orange headings: the samples' folder, Generate
  split samples and the numbered Build MIDI and Render to WAV buttons make up
  Output, and the preload size, the bit depth, the instrument's own folder,
  Export .rzc files, Remove full WAV and the FSI + FSB build make up Export
  to FastSampler. Generate split samples and Remove full WAV both follow from
  the bank being cut from the full WAV, not from the sample files: the first,
  on by default, cuts that WAV into one file per sample and can be turned off
  by a run that only exports to FastSampler; the second deletes the WAV once
  the bank has been built from it. With the first off and the second on, an
  export is Build MIDI, Render to WAV, Build FSI + FSB, and the folder is
  left holding only the instrument. Export .rzc files writes one readable
  zone collection per collection of the instrument beside it: the same mapping in
  FastSampler's editable text format, each zone naming the split sample file
  it plays, so that box needs the split ones and says so when it is off.
  The format is FastSampler's own, mirrored from `rzc_save_file` off the zone
  records the `.fsi` is built from -- its own writer cannot be compiled in
  here, as it is built against the YAML parser and platform layer this
  project deliberately forked. Both folders are picked with the system's dialog.
  Save resampling project, last of all, writes every setting of the tab to
  `Projects\<name>.yaml` next to the exe and loads one back from the list of
  what is in that folder; a load starts the tab again at Build MIDI.
  **Settings** has the folders to scan (for both formats; a fresh install
  starts with the system's VST3 folder and its VST 2 folder, the one the
  registry names or `Program Files\VstPlugins`), the
  audio device and block size, the ASIO control panel, and whether the tips
  (the dim lines that explain a control) are shown.
- Right: when nothing is loaded, two tabs: **VST3** lists the VST3 plugins
  and **VSTi** the VST 2 instruments found under the scanned folders
  (double-click to load; the tab last shown is remembered). A right-click on
  a row bans that plugin: it leaves the list and is neither scanned, nor
  opened by the VST 2 probe, nor loaded by path, until it is unbanned from
  the Unban submenu of the same menu or from the Banned button in the
  list's header. The bans are `banned_plugin` lines in the settings file,
  so a DLL with a bad entry point stays out for good. Otherwise the
  plugin's editor under a header with its name and a close button. A plugin
  that wants more room than the window has grows the window to fit it, once,
  as far as the screen allows. A VST 2 editor has the one size it reports
  and is given an idle call every frame. Under that, the 88-key GUI keyboard, as wide
  as the plugin area. Click to play; lower on the key is louder.
- The timing sliders (hold, gap, pre-roll gap) are FastSampler's drag
  sliders: a plain drag moves in 10 ms steps, a Shift drag 100 ms per pixel,
  an Alt drag 1 ms per pixel, and they read in milliseconds below one second.

Files:
- `src/fastresampler_app.*` — the application state and the requests the UI raises.
- `src/fastresampler_host.*` — plugin hosting, VST3: module, component, controller, processing, editor. Hands a VST 2 plugin on.
- `src/fastresampler_host_vst2.*` — VST 2 instrument hosting: the DLL, the audio master callback, processing, editor, and the scan's probe.
- `src/fastresampler_vst2.h` — the VST 2.4 ABI, declared here so no SDK is needed.
- `src/fastresampler_audio.*` — the audio device API; backends under `src/platform/`.
- `src/fastresampler_scan.*` — finds `.vst3` bundles and files and VST 2 instrument `.dll`s under the configured folders; the VST 2 cache.
- `src/fastresampler_settings.*` — `fastresampler_settings.txt` next to the exe.
- `src/fastresampler_midi.h` — the lock-free MIDI queue from the UI thread to the audio thread, note names.
- `src/fastresampler_resample.*` — the resampling script and the offline render worker.
- `src/fastresampler_wav.*` — the float32 WAV writer and reader.
- `src/fastresampler_yaml.*` — the YAML reader, FastSampler's parser copied here.
- `src/fastresampler_project.*` — the `Projects\<name>.yaml` files: the whole Resample tab saved and loaded.
- `src/fastresampler_instrument.*` — the `.fsi`/`.fsb` builder and the key/velocity mapping.
- `src/fastresampler_loop.*` — a sustain's loop points, taken off the waveform instead of the clock.
- `src/fastresampler_legato.*` — where a legato move leaves one note and reaches the next, read out of the audio.
- `src/fastresampler_rzc.*` — FastSampler's readable zone collection, one file per collection.
- `src/ui/` — the ImGui side panel, plugin list, keyboard and the controller curve editor.

Threading: the UI thread owns the plugin. The audio thread only calls
`host_plugin_process()` while `App::plugin_active` is set and raises
`App::audio_busy` around the call; the UI thread clears the flag and waits for
the counter before it unloads or reconfigures a plugin.

How to build:
1. Clone ImGui once: `git clone https://github.com/ocornut/imgui.git 3rdparty/imgui`
2. Run `cmd.exe /c build.bat`. Pass `debug` for an unoptimised build with symbols.
   Each run increments `build_number.txt`; the exe lands in `build\fastresampler.exe`.
