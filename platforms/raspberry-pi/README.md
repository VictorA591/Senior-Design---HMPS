# Raspberry Pi 5 — Linux ARM64 setup

Start here for Raspberry Pi OS Lite **64-bit**. These instructions describe a
native build on the Pi, not cross-compilation on a Mac.

## What the dependency files mean

- `packages-build.txt`: Linux compiler, ALSA audio/MIDI, fonts, X11 and graphics headers.
- `packages-display.txt`: optional minimal X11 display environment and diagnostic tools.
- `install-dependencies.sh`: installs those package names using apt on the Pi.
- `build.sh`: builds the shared source into Linux ARM64 app and VST3 binaries.

System libraries are supplied by Raspberry Pi OS/Debian package repositories.
They are not checked-in Linux executables. JUCE and patched Tracktion **source**
are already included at `../../JUCE` and `../../Dependencies/tracktion_engine`.
The dependency list follows `../../JUCE/docs/Linux Dependencies.md` for the
modules/features used here. WebKit and CURL are unnecessary because this project's
CMake disables `JUCE_WEB_BROWSER` and `JUCE_USE_CURL`.
OpenGL headers are included for graphics-capable builds/plugins; installing them
alone does not enable OpenGL rendering in HMPS.

## 1. Install packages

From the repository root, with internet access:

```sh
bash platforms/raspberry-pi/install-dependencies.sh --with-display
```

Omit `--with-display` if an appropriate graphical environment already exists.
The installer refuses macOS and non-ARM64 Debian installations. It installs
packages only; it does not change your kernel, autologin, or boot configuration.

## 2. Compile

```sh
bash platforms/raspberry-pi/build.sh
```

This uses a fresh `build-pi/` directory, separate from the Mac's `build/`.
Never copy the Mac build cache into it. If an existing build-pi directory uses
another generator, move it aside before running the script.

Expected CMake output locations for this Ninja Release build:

- App: `build-pi/EducationalDAW_artefacts/Release/Educational DAW Engine`
- Plugin bundle: `build-pi/HMPSKeys_artefacts/Release/VST3/HMPS Keys.vst3`

Linux applications do not normally have a `.exe` extension. Keep the entire
`.vst3` directory together; a Mac VST3 binary cannot run on Linux ARM64.

## 3. Display and audio

OS Lite has no graphical desktop by default. Installing development headers is
not enough: a graphical session must also be running.

For an initial local-screen test, log in directly on the Pi and run `startx` after
installing the display packages. From a terminal in that X11 session, run from the
repository root:

```sh
./build-pi/EducationalDAW_artefacts/Release/"Educational DAW Engine" --kiosk
```

A normal SSH terminal does not provide the Pi touchscreen's graphical session.
Choose and test the actual audio output (USB audio, HDMI, or your configured I2S
DAC). `aplay -l` lists ALSA playback devices; `aconnect -l` lists ALSA MIDI ports.
Connect an ESP32 running USB-MIDI firmware, then select its MIDI input/output in HMPS.
A bare ESP32 programming/serial port is not automatically a MIDI device.

## 4. Remaining deployment validation

These scripts have syntax/guard checks on the Mac, but have not been executed on
a Pi. Linux package installation, compilation, DAC configuration, real touchscreen
multi-touch, audio latency, thermal behavior, and USB MIDI need Pi validation.
A configured real-time kernel and scheduling policy are separate tasks; these
packages do not establish real-time guarantees. Automatic boot-to-HMPS and an
onscreen typing keyboard are not configured by these scripts.

Retain all project and dependency licenses when distributing builds.
