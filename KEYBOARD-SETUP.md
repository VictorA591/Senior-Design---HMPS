# HMPS keyboard and sound deployment

## Built-in sounds
Every keyboard track has Piano and Synth Lead. Select a keyboard track and choose
Load Sound, select a sound, then Load. Cancel preserves the old sound.
Sound selection is stored per track in .hmps projects. Older sessions default to Piano.
Both sounds run offline and are compiled into the app. External VST3 hosting remains available.

Piano is a compact, single-velocity sample instrument using five Salamander recordings
across C4–C5; each decoded sample is limited to six seconds with a short end fade.
Velocity changes amplitude, not recording layer. See PIANO-LICENSE.md and Assets/Piano/manifest.json.
Synth Lead is an additive, band-limited oscillator with an amplitude envelope.
The keyboard has 16 shared voices, sustain-pedal support via JUCE, and +/-2-semitone pitch bend.
Higher polyphony can steal older voices. Switching a built-in sound releases that channel's notes.

## HMPS Keys VST3
CMake's HMPS_BUILD_VST3 option defaults to ON. The HMPSKeys_VST3 target builds
a real VST3 instrument using the same sample bank and voice code as the DAW.
It exposes Piano and Synth Lead as a Sound parameter and programs, saves its state,
and includes a 13-key editor with MIDI highlighting.
Build output: build/HMPSKeys_artefacts/<configuration>/VST3/HMPS Keys.vst3.
Use the DAW's Add Folder to scan that VST3 output directory. No automatic system installation occurs.
Do not scan the build's test-vst3 fixture folder as a production instrument library.

## Raspberry Pi 5
Copy the source repository including Assets/Piano, plus the JUCE and Tracktion source checkouts.
Do not copy Mac executables, Xcode build folders, or Mac VST3 binaries to the Pi.
Build a fresh Release configuration on Raspberry Pi OS Lite 64-bit:

    cmake -S . -B build-pi -DCMAKE_BUILD_TYPE=Release -DHMPS_TRACKTION_DIR=/path/to/tracktion_engine
    cmake --build build-pi --target EducationalDAW --parallel 2

Install the C++/CMake toolchain and JUCE's Linux ALSA, font, X11 and graphics development
dependencies first. Configure a graphical session and an audio output device separately.
The current Linux JUCE backend uses X11. Wayland deployments require compatibility support.
With HMPS_BUILD_VST3=OFF the app still includes both built-in sounds.

The Linux ARM64 VST3 build and Mac VST3 build contain different native binaries.
Keep PIANO-LICENSE.md with distributed application and plug-in builds.

## Verification
- Both sounds: play C4–C5, sequential melodies, repeated notes, and C/E/G chords.
- MIDI: select a device on the main menu, verify press/release highlighting,
  simultaneous notes, sustain release and cable-disconnect cleanup.
- Save different sounds on two tracks; reopen and verify independent restoration.
- Mouse: down/drag/up should start/move/release notes.
- Touch: repeat with a finger, then multiple fingers on the actual Pi touchscreen.
  JUCE's keyboard tracks distinct pointers, but the Linux display/input stack must
  deliver separate contacts. Multi-touch is NOT verified by mouse tests.
- Profile Release builds on the Pi with all tracks active; measure DAC latency,
  dropouts, CPU use and temperature. No Pi latency or multi-touch guarantee is claimed.

Recording/session timing uses the Tracktion adapter; it is separate from the keyboard sound bank.
The standalone HMPS Keys VST3 does not depend on Tracktion.

## Mac keyboard test
1. Connect a USB MIDI keyboard (or a DIN keyboard through a USB MIDI interface).
2. Run the EducationalDAW scheme in Xcode.
3. On the main menu select the keyboard in the MIDI input list.
4. Open Sandbox, create/load a session and select a Keyboard track.
5. Play C4 through C5 (MIDI notes 60–72), including C/E/G together.
   Keys outside the displayed octave can sound but are not drawn on the keyboard.
6. Use Load Sound to compare Piano and Synth Lead; test note release and sustain.
7. Record a short take, stop, play, save, reopen, and verify its sound and notes.
8. Unplug while holding a note; sound should release and highlighting should clear.
9. In Learning Mode, start the piano game and play the matching physical notes
   as their tiles reach the gold line.

If the MIDI list says None, return to the menu after connecting the keyboard;
the list refreshes automatically. Select the device explicitly. The hardware MIDI
connection has not been verified in this development environment.

## Automated validation (Mac Debug, November 13, 2026)
Xcode built the application and HMPS Keys VST3 successfully.
The development harnesses exercised:
- FLAC decoding, all thirteen pitches, both presets, thirteen-note chords and release.
  Measured full-velocity chord peaks: Piano 0.590, Synth Lead 0.695.
- Actual HMPS Keys VST3 discovery, loading, chord rendering, state and editor creation.
- Simulated external MIDI chords, held-key state, and disconnect release.
- Tracktion loop timing across audio blocks, loop toggle, stop and independent track gains.
- Arrangement tempo/event merging, recording with accompaniment, saved sound selection,
  project JSON round trips, tutorial scoring, progression and resume.
- VST3 hosting with a test instrument: child-process scanning, MIDI routing, mute,
  plug-in state restoration and asynchronous session restoration.

These are offline software tests. They do not measure audio-device latency or prove
physical USB MIDI, Pi Linux ARM64 execution, or touchscreen multi-touch.

## Plug-in sizing, touch and MIDI output
HMPS Keys is a .vst3 bundle (a directory containing a platform-specific binary and
metadata). Keep the whole bundle together. Build separately for macOS and Linux ARM64.

The editor resizes from 480x240 to 1920x1200 and recalculates the keyboard width
to keep all eight white keys and five black keys visible. Its layout adapts to the
host window instead of forcing a particular screen aspect ratio. The HMPS plug-in
window also limits its initial size to available screen space.

JUCE's keyboard tracks individual pointer IDs, allowing separate fingers to hold
different notes. The display server and touchscreen driver must deliver separate
touch contacts; mouse emulation alone cannot supply multi-touch chords.

Pressing/releasing an onscreen plug-in key produces Note On/Note Off events at the
VST3 MIDI output as well as local sound. Incoming host MIDI is not echoed.
HMPS records newly generated plug-in notes on the selected track while Record is
active. Other hosts must support and route instrument MIDI output.
Piano and Synth Lead remain selectable, and the selected sound is saved in plug-in state.
