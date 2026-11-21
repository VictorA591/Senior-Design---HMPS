# HMPS — Hybrid Music Production System

C++20 music workstation and educational interface, using JUCE and Tracktion Engine.

## Choose your platform

| Platform | Start here | Dependencies | Build output (ignored by Git) |
| --- | --- | --- | --- |
| Mac / Xcode | [Mac setup](platforms/macos/README.md) | Xcode/macOS SDK + CMake; bundled library source | `build/` |
| Raspberry Pi 5 / OS Lite 64-bit | [Pi setup](platforms/raspberry-pi/README.md) | [Linux package list](platforms/raspberry-pi/packages-build.txt) and [installer](platforms/raspberry-pi/install-dependencies.sh) | `build-pi/` |

## Repository map

```text
platforms/
  macos/                  Mac-specific build and test instructions
  raspberry-pi/           Linux package lists, installer, and ARM64 build script
Source/                   Shared application engine and UI
*.h, HmpsKeysPlugin.cpp    Shared components and keyboard VST3 implementation
Assets/                   Shared piano samples and asset metadata
JUCE/                     Bundled cross-platform JUCE source
Dependencies/             Bundled patched Tracktion source and provenance
CMakeLists.txt            One build definition for both platforms
build/                    Generated Mac artifacts; not uploaded
build-pi/                 Generated Linux ARM64 artifacts; not uploaded
```

The platform folders contain setup instructions and scripts, not separate copies
of the DAW. The same application code is compiled for each operating system.
Linux system dependencies are installed with apt on the Pi; this repository does
not contain a flashable OS image or prebuilt Linux executables.

## Raspberry Pi quick start

After cloning onto Raspberry Pi OS Lite 64-bit, run from the repository root:

```sh
bash platforms/raspberry-pi/install-dependencies.sh --with-display
bash platforms/raspberry-pi/build.sh
```

Follow the [Pi guide](platforms/raspberry-pi/README.md) to start a graphical session
and test audio, touchscreen and MIDI. Pi deployment is not yet hardware-validated.

## More documentation

- [Keyboard, instruments and MIDI testing](KEYBOARD-SETUP.md)
- [Dependency versions and local Tracktion changes](Dependencies/README.md)
- [Piano sample license](PIANO-LICENSE.md)
