# macOS — development and testing

Use this folder for Mac setup. Use `../raspberry-pi/` for Linux system dependencies
and Pi builds. Application and instrument source are shared between both platforms.

## Tools

- Xcode with its C++ toolchain and macOS SDK.
- CMake 3.22 or newer, available on PATH.
- Bundled JUCE and patched Tracktion source (already in this repository).

macOS supplies CoreAudio, CoreMIDI and native window frameworks through the system
and SDK. Do not run the Pi apt installer on the Mac.

## Existing Xcode project

Continue using the generated project inside `build/`. Select the EducationalDAW
scheme and build/run. The CMake target also builds HMPS Keys VST3.

To generate the Mac project from a fresh repository clone, run at the root:

```sh
cmake -S . -B build -G Xcode
open build/EducationalDAW.xcodeproj
```

If an older cache points to an external Tracktion checkout, reconfigure with:

```sh
cmake -S . -B build -G Xcode \
  -DHMPS_JUCE_DIR="$PWD/JUCE" \
  -DHMPS_TRACKTION_DIR="$PWD/Dependencies/tracktion_engine"
```

## Outputs and tests

- Mac app: `build/EducationalDAW_artefacts/Debug/Educational DAW Engine.app`
- Mac VST3: `build/HMPSKeys_artefacts/Debug/VST3/HMPS Keys.vst3`
- Output location changes to `Release` for a Release build.

For an outgoing MIDI test, select IAC Driver Bus 1 as HMPS MIDI Output, leave HMPS
MIDI Input at None, and observe Bus 1 in a MIDI monitor. Test the built-in keyboard
and the keyboard inside the HMPS Keys plugin separately.

Mac builds and mouse tests do not validate Linux ARM64 execution or touchscreen
multi-touch. Transfer source through Git and rebuild on the Pi; do not transfer
Mac executables as Pi applications.
