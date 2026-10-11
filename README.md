# FastResampler
A small application for Windows that allows resampling of any VST3 or VSTi (VST2) instrument.

## Building
```bat
git clone --recursive https://github.com/M52/fastresampler.git
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
| `3rdparty/fastsampler-shared` | FastSampler file formats, with nanopb | `4d6fd40` | MIT; nanopb zlib |
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