# NoSuchDevice for 4ms MetaModule

This directory builds the VCV Rack plugin (TrigGate, DivTrig, Corrupter) as a
`.mmplugin` for the [4ms MetaModule](https://metamodule.info) using the
[MetaModule plugin SDK](https://github.com/4ms/metamodule-plugin-sdk) Rack
adapter. The Rack sources in `../src` are compiled unchanged; the SDK provides
the `rack::` API on the device, and `#ifdef METAMODULE` guards in
`Corrupter.cpp` handle the few places where the host differs.

## Requirements

- `arm-none-eabi-gcc` 12.3 or newer (Homebrew: `brew install --cask gcc-arm-embedded`)
- CMake 3.22+ and Ninja
- The SDK checked out with submodules, by default as a sibling directory:

```sh
git clone --recurse-submodules https://github.com/4ms/metamodule-plugin-sdk ../../metamodule-plugin-sdk
```

Or point at another location with `-DMETAMODULE_SDK_DIR=...` or the
`METAMODULE_SDK_DIR` environment variable.

## Build

```sh
cd metamodule
cmake --fresh -B build -G Ninja
cmake --build build
```

The result is `build/dist/NoSuchDevice.mmplugin` plus a release-named copy
`NoSuchDevice-v<version>.mmplugin` (version read from `../plugin.json`).
Pass `-DINSTALL_DIR=<dir>` to change the output directory.

## Install on the MetaModule

Copy the `.mmplugin` file into a `metamodule-plugins/` folder on a USB drive or
SD card, insert it into the MetaModule, then open **Settings → Plugins** and
scan/load it. Modules appear under the **No Such Device** brand.

## Faceplates

MetaModule needs opaque 240 px tall PNG faceplates instead of the Rack SVGs.
They are committed under `assets/` and regenerated with
`vendor/corrupter-dsp/vcv/metamodule/scripts/bake_panels.py` (requires
Inkscape and the DejaVu Sans font, found automatically from a VCV Rack
install or `$DEJAVU_TTF`):

```sh
python3 vendor/corrupter-dsp/vcv/metamodule/scripts/bake_panels.py --res res --out metamodule/assets
```

The Corrupter panel draws its labels at runtime in Rack (`CorrupterLabels`);
on MetaModule that overlay is compiled out and the script bakes the same
labels into the PNG instead. If you move a control in `Corrupter.cpp`, update
the label table in the script and re-run it.

## What differs on the MetaModule

- Corrupter allocates its delay buffer on the first `process()` call, sized
  for the actual sample rate, instead of in the constructor (the adapter
  instantiates every module at plugin load).
- The host block size is 16 frames, so Corrupter's internal block is 16.
- Parameter tooltips (`ParamQuantity::description`) are not updated on the
  device; strings must not be built on the audio thread.
- TrigGate and DivTrig pass explicit Schmitt-trigger thresholds because the
  SDK's `dsp::SchmittTrigger::process()` defaults differ from Rack's.

## Testing without hardware: the 4ms firmware simulator

The [4ms/metamodule](https://github.com/4ms/metamodule) firmware repo ships a
desktop simulator that can compile this directory in as a built-in brand
(`docs/simulator-ext-plugins.md` there). The `.mmplugin` itself cannot be
loaded into the simulator, but the same sources, faceplates and
`#ifdef METAMODULE` paths are exercised.

Register the brand in the firmware clone's `simulator/ext-plugins.cmake`:

```cmake
list(APPEND ext_builtin_brand_paths "${CMAKE_CURRENT_LIST_DIR}/../../NoSuchDevice/metamodule")
list(APPEND ext_builtin_brand_libname "NoSuchDevice")
```

Then build (`cmake --preset Default -DSIMULATOR_MIDI=OFF && cmake --build build`
for the GUI, `cmake --preset headless && cmake --build build-headless` for
audio rendering) and use the patches in `sim-patches/`:

```sh
cd <firmware>/simulator
# Screenshot the Corrupter module view (module index 1 in the patch)
./build/simulator -p ../../NoSuchDevice/metamodule/sim-patches/ --patch /CorrupterTest.yml     --page moduleview --module 1 --screenshot corrupter.bmp --screenshot-frames 30
# Render 8 s of audio through Corrupter and report CPU load
build-headless/simulator -p ../../NoSuchDevice/metamodule/sim-patches/CorrupterTest.yml     --in in.wav --out out.wav -n 384000
```

Headless WAV files are stereo float32 at 48 kHz, with sample values used as
volts directly. Note that the release-name copy step in `CMakeLists.txt` is
skipped under the simulator's fake SDK.

## Release

The `MetaModule plugin` GitHub Actions workflow builds the plugin on every
push and attaches `NoSuchDevice-v<version>.mmplugin` to the GitHub release
when a `v*` tag is pushed (see `AGENTS.md` for the tagging steps).
