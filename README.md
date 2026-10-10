# FastResampler

FSI/FSB/FSP/SMFP definitions now come from the pinned public
`fastsampler-shared` submodule. See [FORMATS.md](FORMATS.md) before changing
any file format or updating its revision.

A small plugin host for Windows. It loads one VST3 plugin or one VST 2
instrument (VSTi) at a time, shows the plugin's own editor, sends it MIDI
notes and controllers from a GUI keyboard and a set of sliders, and plays the
plugin's output through WASAPI or ASIO.

## Building

Windows 10 or later, x64. It needs git and Visual Studio 2022 (any edition,
or the Build Tools) with the "Desktop development with C++" workload, and
nothing else:

```bat
git clone --recursive <repository URL> fastresampler
cd fastresampler
build.bat
```

In a clone made without `--recursive`, run
`git submodule update --init --recursive` first. `build.bat` finds Visual
Studio with `vswhere`, builds WavPack into `build\wavpack\`, compiles
`build\fastresampler.exe` and copies `wavpackdll.dll` next to it.
`build.bat format-tests` builds and runs `tests/format_fixture.cpp`, which
writes a synthetic instrument through the production writer and exits 0 on
success; `build.bat debug` builds without optimisation.

The dependencies are under `3rdparty/`: git submodules pinned to a release,
and the icon font and its names, which are kept in this repository:

| Path | Project | Pinned at | Licence |
|---|---|---|---|
| `3rdparty/imgui` | Dear ImGui | `96b6eb77` (1.93.0 WIP) | MIT |
| `3rdparty/vst3sdk/base`, `pluginterfaces`, `public.sdk` | VST 3 SDK | `v3.8.1_build_84` | MIT |
| `3rdparty/wavpack` | WavPack | `5.9.0` | BSD 3-Clause |
| `3rdparty/fastsampler-shared` | FastSampler file formats, with nanopb | see [FORMATS.md](FORMATS.md) | MIT; nanopb zlib |
| `3rdparty/lucide` | Lucide icon font | in this repository | ISC |
| `3rdparty/IconFontCppHeaders` | IconFontCppHeaders | in this repository | zlib |

`VST3_SDK_DIR` points the build at another VST 3 SDK, version 3.8.1 or later:
the folder that holds `base`, `pluginterfaces` and `public.sdk`.

The WavPack and VST 3 SDK repositories hold some long file paths. If git
stops with "Filename too long", clone to a shorter folder or run
`git config --global core.longpaths true` and update the submodules again.

## Licence

FastResampler is under the MIT licence, see [LICENSE](LICENSE). The
third-party code keeps its own licence, listed in the table above; the
program shows every notice in its Licenses tab.

Details about the project:
- Windows 10, x64. Built with MSVC as a unity build: `build.bat` compiles the
  one translation-unit root `src/fastresampler_win32.cpp`, which `#include`s
  every other source file. A macOS or Linux port adds a sibling root with the
  same module list and its own platform files.
- The code follows the Casey Muratori / Handmade Hero ethos, like FastSampler:
  plain old data structs, procedural functions, fixed-size arrays allocated up
  front, no OOP. The unavoidable classes are the VST3 interfaces a host has to
  implement (`IComponentHandler`, `IPlugFrame`) and the SDK's hosting helpers.
- Dependencies: Dear ImGui, the VST3 SDK 3.8.1 (the first under the MIT
  licence) and WavPack 5.9.0, all submodules (see Building). The
  ASIO SDK is not needed: the driver interface is declared in
  `src/platform/fastresampler_asio.h`. The VST 2 SDK is not needed either,
  and Steinberg no longer ships it: the plugin ABI (the `AEffect` structure,
  the opcodes, the event and time structures) is declared in
  `src/fastresampler_vst2.h`. The icons are the Lucide icon font
  (`3rdparty/lucide`, ISC) named by IconFontCppHeaders
  (`3rdparty/IconFontCppHeaders`, zlib); `build.bat` copies `lucide.ttf` next
  to the exe, which loads it at start and draws no icons without it.
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
- The public `3rdparty/fastsampler-shared` submodule supplies FSI/FSB
  definitions, effect IDs, defaults and the nanopb runtime. Run
  `git submodule update --init --recursive` before building; see [FORMATS.md](FORMATS.md).
- The YAML reader is the other way round: a resampling project is this
  project's own format, so FastSampler's parser is copied into
  `src/fastresampler_yaml.*` and nothing about the projects depends on that
  checkout.
- WavPack is upstream 5.9.0, built from the submodule by
  `scripts/build-wavpack.bat` into `build/wavpack/`; `build.bat` links that
  import library and copies that DLL next to the exe. The bank builder
  compresses the streamed part of every sample with it, the way FastSampler's
  own export does. FastSampler's copy of WavPack differs only in its entropy
  decoder, and this project only encodes, so its banks are the same byte for
  byte.
- Platform calls go through `src/platform/fastresampler_platform.h`. The audio
  backends (`src/platform/fastresampler_audio_*.cpp`) and the window glue
  (`src/fastresampler_view_win32.cpp`) are the only other files that may
  include OS headers.
- The colour palette is FastSampler's, copied verbatim in
  `src/ui/fastresampler_palette.h`.
- Never use heredoc for writing files. Use the write tool instead.
- The code carries no comments.
- UI strings capitalise only the first letter.

Layout of the window:
- Left: the side panel, one column down the whole left edge, with four tabs.
  **Plugin** shows what is loaded, its
  output meters and a close button. **MIDI** has the channel, CC sliders,
  sustain, pitch bend and all-notes-off. **Resample** builds a MIDI script
  (a note range sampled every n semitones, a list of velocities, a number of
  round robins per note with optional pre-roll notes, each note held then left
  to ring out), renders it through the plugin to one 32 bit float WAV per
  output bus faster than realtime, then splits each of those into one WAV per
  sample named
  `Bus_Instrument_Articulation_Note_Velocity_RRn.wav`, and finally
  builds a FastSampler instrument (`.fsi` + `.fsb`) per bus with the samples mapped:
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
  collection names its layer as the one whose releases it holds, and carries
  FastSampler's Smart release: at the key-up it reads the level the layer's
  sustain was sounding at, the crossfade included, starts the release there,
  and fades it in over the sustain's own release as that fades out. The level
  may move as far as the effect allows, 60 dB, so the release of a layer the
  controller had faded to the floor follows it there. Every release zone
  keeps a 10 ms attack for when there is no sustain to read: a release begins
  at the note-off of a note that was still sounding, so started flat it would
  step from silence to that level and click on every key-up.
  Every zone has Ignore vel set, as the layers carry the dynamics. Every
  other collection carries FastSampler's gain effect, driven by the
  controller the layer curves were drawn for (the mod wheel when none is)
  through a curve between -50 and 0 dB: layer k of n sits at 0 dB at k/n of
  the controller's range, the first from the bottom up to 1/n and the last
  at the top, and between two peaks the lower layer fades out and the upper
  in by an equal power law, so the controller moves through the layers at a
  steady level. The curves share FastSampler's first curve group, so its
  editor draws them over one another.
  A brighter layer would bring its high end in the moment it leaves the
  floor, long before the controller reaches it, so with more than one layer
  every collection, the releases too, also carries FastSampler's spectral
  morph filter on the same controller. It is placed at its own layer's peak
  and morphs toward the same collection of every other layer -- its
  sustains, its releases, its moves of the same interval -- placed at
  theirs: the upper layer comes in sounding like the lower one and turns
  into itself as the controller reaches it. FastSampler measures a
  collection's own spectrum as it loads it; the other layers' come from one
  fingerprint file per collection beside the instrument (`<name>_dyn1.smfp`,
  `<name>_dyn1-release.smfp`, ...), measured the way FastSampler measures
  them. The instrument names those files by their whole path, because
  FastSampler opens them as they stand: move the folder and the morph finds
  nothing until the instrument is built again, while the layers still
  crossfade on gain.
  Capture legato transitions, in the Legato section, adds the moves between
  the notes to the run; the rest of that section is only there while the box
  is on. Legato capture is experimental: the section only shows, and a run
  only captures transitions, while Show experimental features is on in the
  Settings tab. After every note has been sampled, each capture
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
  to FastSampler. Where a run writes comes from Naming and from the output
  busses: under the root project folder, the first row of that section, each
  bus makes `<root>\<Instrument>\<Articulation>\<Bus>` and puts its full WAV,
  its sample files and its instrument there, so every take gets a folder of
  its own and every microphone a folder in it. Output and Export to
  FastSampler each show the first bus's folder and each carry a Choose
  another folder box that overrides it with a folder of their own, one for
  the samples and one for the instrument; a bus is the last folder either
  way, so an overridden folder still gets one subfolder per bus and two
  microphones never write over one another. The whole chain is made at the
  render, however deep it is.
  A settings file or a project from before the root existed names only the
  two folders it wrote to: when the samples already went to a folder that
  ends in those three names, that root is read back off it and nothing
  moves; otherwise both boxes go on, which also keeps the folders as they
  were. A settings file or a project from before the output busses named one
  Microphone instead: that name becomes the first bus, so such a run keeps
  the folder and the file names it always had.
  Generate split samples and Remove full WAV both follow from
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
  project deliberately forked. All three folders are picked with the system's dialog.
  Projects, last of all, is a small file browser that starts in the
  `Projects` folder next to the exe (made at start when it is missing):
  folders first, then the `.yaml` projects, with buttons to go up a folder,
  back to `Projects` and to read the folder again. A double-click goes into a
  folder or opens a project; Save project writes every setting of the tab to
  `<name>.yaml` in the folder shown. Opening a project starts the tab again
  at Build MIDI, and the browser moves to that project's folder.
  A project also holds the plugin it was made with, so opening one brings
  back the whole set-up and not only the tab. The YAML names the plugin, its
  format and its path, and the block of state the plugin wrote goes in
  `<name>.fstate` beside it: the patch it held, every parameter, and
  the outputs it was routed to, exactly what a DAW keeps in its song. Opening
  the project loads that plugin when another one, or none, is up, then hands
  it its state; when it is already the one loaded, only the state goes in, so
  a sampler that takes a minute to start is not started again. A project
  carries to another computer: when the path it holds is not there, the
  scanned list of its format is searched for the same file name and then for
  the same plugin name, so a Kontakt installed in another folder is still
  found, and the log says where it came from. The state names the plugin it
  belongs to -- the class id for a VST3, the unique id for a VST 2 -- so a
  state is refused rather than pushed into the wrong plugin of the same file
  name. The folders travel the same way: one whose whole tree is missing,
  which is what another computer's drive letter looks like, comes back to
  the folder beside the exe, and an override that pointed there is dropped;
  a folder that is simply not made yet is kept, as a run makes it. A plugin
  that comes back on other busses than it had -- a sampler whose state names
  more microphones -- is set up again for the layout it reports then, and the
  log says how many channels over how many busses it offers. A project saved
  with nothing loaded keeps no state and takes any stale one with it, and a
  plugin this computer has not got at all is reported while the settings
  still load. What a plugin's own state points at is the plugin's business:
  a Kontakt multi names its libraries by path, so the computer that opens
  the project needs those installed, exactly as it would in a DAW.
  **Settings** has the folders to scan (for both formats; a fresh install
  starts with the system's VST3 folder and its VST 2 folder, the one the
  registry names or `Program Files\VstPlugins`), the
  audio device and block size, the ASIO control panel, whether the tips
  (the dim lines that explain a control) are shown, Show experimental
  features, and a button that shows the disclaimer again. **Licenses** lists
  the third-party software FastResampler is built with, each with its
  license notice; the notices are kept in `src/fastresampler_licenses.h`.
  The side panel reopens on the tab it was left on.
- At start: the disclaimer comes first, on an otherwise empty window, until
  Don't show again is ticked. Nothing starts before I understand & agree is
  clicked -- no scan, no audio device, no plugin, no Projects folder -- and
  the first time that button only unlocks after five seconds. I disagree.
  Exit closes FastResampler without writing the settings file. Then the
  plugin folders are scanned behind a progress bar, one VST 2 DLL per
  frame, so the name of the DLL being opened is on screen while it loads. A
  plugin path given on the command line, or a project load asked for
  meanwhile, waits for the scan. While any modal is open, the plugin
  editor's child window is hidden, as Windows would otherwise draw it over
  the modal.
- A new controller curve takes the controller last picked in a curve's box,
  or added, in this session: CC1 to begin with, or the last curve's after a
  project load.
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
  and is given an idle call every frame. Under that a strip with two tabs,
  **Output log** and **Output busses**, and
  under the strip the 88-key GUI keyboard, both as wide as the plugin area.
  Click a key to play it; lower on the key is louder.
- The output log is a ledger of every status message, each stamped with the
  time of day and errors in red: what a render wrote and how fast, what a
  build made of the settings, a project saved or loaded, a folder picked, a
  disk too small. The arrow on the strip's bar folds it down to that bar
  alone and
  opens it up again, and the grip along its top edge is dragged up or down
  to set how tall it is; both are kept in the settings, as is the tab last
  shown. Clear empties it.
  The newest line is the one the side panel's own status strip shows, so
  the log is that line's history. It holds the last 512 and drops the
  oldest past that; `fastresampler.log` next to the exe keeps them all.
  A plugin editor is a child window, which the compositor draws over
  everything this program renders itself, so the editor is never given the
  room the strip and the keyboard have: it is clipped above the strip's top
  edge and the ledger stays in sight whatever is loaded. Dragging the grip
  therefore offers the plugin a shorter area, and a plugin that can shrink
  does. The plugin area keeps a floor of its own, so the ledger cannot be
  dragged over the whole window.
- The output busses are how a multi-miced instrument is resampled in one go.
  A library that plays a close, a tree and an outrigger microphone at once
  routes each of them to its own pair of the plugin's outputs; a bus here
  reads one such pair and is named after the microphone on it. One render
  then captures every microphone in the single pass, and each bus writes a
  folder of its own holding its full WAV, its samples and its instrument.
  The tab is one row per bus: a radio button that picks the one the speakers
  play (the device is stereo and cannot carry them all), the name, the pair
  it reads, a meter, and a button that removes it. Add output bus adds one on
  the lowest pair nothing reads yet, up to sixteen; the last bus cannot go,
  as a run must have somewhere to write. The meters are what make the
  routing visible: play a note and the bus the plugin sent it to moves. A
  line under the list says how many output channels the loaded plugin has,
  over how many busses, against how many the busses read, and warns when a
  bus sits past the end of them, which would record silence. Two busses
  under one name, or on one pair, are marked in red and refused by Build
  MIDI, because either would have two microphones write the same files.
  The plugin's own output busses are flattened into one list of channels
  first, so a VST3 with three stereo busses and a VST 2 with six output
  channels are addressed the same way.
- The timing sliders (hold, gap, pre-roll gap) are FastSampler's drag
  sliders: a plain drag moves in 10 ms steps, a Shift drag 100 ms per pixel,
  an Alt drag 1 ms per pixel, and they read in milliseconds below one second.

Files:
- `src/fastresampler_app.*` — the application state and the requests the UI raises.
- `src/fastresampler_host.*` — plugin hosting, VST3: module, component, controller, processing, state, editor. Hands a VST 2 plugin on.
- `src/fastresampler_host_vst2.*` — VST 2 instrument hosting: the DLL, the audio master callback, processing, state, editor, and the scan's probe.
- `src/fastresampler_vst2.h` — the VST 2.4 ABI, declared here so no SDK is needed.
- `src/fastresampler_audio.*` — the audio device API; backends under `src/platform/`.
- `src/fastresampler_scan.*` — finds `.vst3` bundles and files and VST 2 instrument `.dll`s under the configured folders; the VST 2 cache.
- `src/fastresampler_settings.*` — `fastresampler_settings.txt` next to the exe.
- `src/fastresampler_midi.h` — the lock-free MIDI queue from the UI thread to the audio thread, note names.
- `src/fastresampler_resample.*` — the resampling script and the offline render worker.
- `src/fastresampler_wav.*` — the float32 WAV writer and reader.
- `src/fastresampler_yaml.*` — the YAML reader, FastSampler's parser copied here.
- `src/fastresampler_state.h` — the container a plugin's own state is kept in: a header and any number of parts, so a VST3's two streams and a VST 2's chunk both fit.
- `src/fastresampler_project.*` — the `<name>.yaml` project files: the whole Resample tab saved and loaded, the plugin's state in `<name>.fstate` beside them, and the folder listing the project browser shows.
- `src/fastresampler_licenses.h` — the third-party license notices the Licenses tab shows.
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
