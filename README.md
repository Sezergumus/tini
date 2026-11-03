# Tını

A guitar amp simulator with a reorderable pedal chain, cabinet IR loader and presets.
Built with [JUCE](https://juce.com) as a **standalone app** and a **VST3 plugin**.

## Features

- Amp model and a convolution cabinet simulator (load your own IR)
- Pedal board: noise gate, compressor, boost, overdrive, chorus, delay, reverb
- Neural Amp Modeler (`.nam`) support: models for the amp slot and NAM pedals, each pedal named after its model
- Library page: your amp models, pedal captures, cabinet IRs and other models in one place (`Documents/tini`), sorted by what the files say about themselves; drop files onto the window to add them
- Drag pedals and the amp to reorder the signal chain
- Preset manager (save, load, delete)
- Standalone shows the real device round-trip latency (`≈ xx ms`) next to the driver name
- ASIO support on Windows for low-latency playing (built in, nothing extra for users to install except their audio interface's ASIO driver, or [FlexASIO](https://github.com/dechamps/FlexASIO) / ASIO4ALL)

## Building (Windows)

Requirements: Visual Studio 2022 (C++ workload), CMake 3.24+, Git. JUCE is downloaded automatically.

```powershell
cmake -B build
cmake --build build --config Release
```

Outputs are in `build/NeuralFlowAmp_artefacts/Release/` (`Standalone/Tini.exe` and `VST3/Tini.vst3`).
`build_and_run.ps1` builds and launches the standalone.

Headless DSP/UI tests:

```powershell
cmake --build build --config Release --target NeuralFlowAmpTests
build\NeuralFlowAmpTests_artefacts\Release\NeuralFlowAmpTests.exe
```

### ASIO

The ASIO headers are included in `third_party/asiosdk`, so ASIO is enabled automatically
(`ASIO enabled` appears in the CMake output). To use a different SDK copy, pass
`-DASIO_SDK_DIR=<folder containing common/iasiodrv.h>`.

### About the latency readout

The number in the header is an estimate: input latency + output latency as reported by the
audio driver, plus any plugin latency. It does not include the converter delay inside your
audio interface, so measure with a loopback cable (or a tool such as Oblique Audio's RTL
Utility) when you need the exact figure. Inside a DAW only the buffer size is shown.

## License

Tını is free software, licensed under the **GNU Affero General Public License v3.0**
([LICENSE](LICENSE)). Copyright (C) 2026 Sezer Gümüş.

### Third-party software

- [JUCE](https://juce.com), used under the AGPLv3 option of its dual license.
- Steinberg ASIO SDK 2.3.4, used under the GPLv3 option of its dual license
  (see [third_party/asiosdk/LICENSE.txt](third_party/asiosdk/LICENSE.txt)).
  ASIO is a trademark and software of Steinberg Media Technologies GmbH.
- VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.
