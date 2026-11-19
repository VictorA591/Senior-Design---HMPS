# HMPS dependency snapshots

This repository vendors source snapshots so a normal clone contains the dependencies.
No Git submodule commands or sibling checkouts are needed.

- `JUCE/`: JUCE 9.0.2 from the Mac download used for the working HMPS build.
  It was not a Git checkout; no upstream commit identity is claimed.
  Prebuilt `.app` bundles and `.DS_Store` files are excluded.
- `Dependencies/tracktion_engine/`: tracked source files from upstream
  https://github.com/Tracktion/tracktion_engine at `877e39c0fe9f1c3e4a9d836f5f338688250875d7`,
  with the three existing JUCE compatibility edits preserved.
  `tracktion-local-changes.patch` records those edits against that commit.
  Tracktion's nested JUCE submodule is omitted: the root CMake adds JUCE first.

Keep the dependency license files when redistributing. Dependencies retain their
original licenses; copying them here does not change licensing terms.
`source-sha256.json` records the initial snapshot file hashes.

## Fresh build

Install the Linux development dependencies described in `../KEYBOARD-SETUP.md`,
then run from the repository root:

```sh
cmake -S . -B build-pi -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-pi --target EducationalDAW --parallel 2
```

Existing CMake build directories may cache the old external Tracktion location.
Configure them explicitly with `-DHMPS_TRACKTION_DIR="$PWD/Dependencies/tracktion_engine"`
and `-DHMPS_JUCE_DIR="$PWD/JUCE"`, or use a fresh build directory.

Future dependency updates should deliberately replace these snapshots, reapply or
retire the compatibility patch, and rerun the app/plugin checks.

Upstream `.gitignore` files are omitted from these snapshots so their rules cannot
hide vendored source files (for example Tracktion airwindows/BuildATPDF).
