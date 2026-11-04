# Aseprite Tests

With `ENABLE_TESTS=ON`, the `crash_safety_tests` native target deliberately
throws conversion-worker exceptions (including `std::bad_alloc`), rejects a
stroke through KARN validation, and restores a corrupted tool-presets file.
It checks rollback, a single commit attempt, subsequent editing, undo/redo,
save/reopen, and preset-file preservation. It creates its own temporary user
profile, so running it does not modify your preferences or saved presets.

The `palette_remap_ui_tests` native target exercises the real Remap Palette
button, bank/document scope, pending snapshots, undo/redo, cross-bank linked
data, RGB reference exclusion, and the stock indexed path. It also uses an
isolated user profile. `scripts/palette_bank_remap.lua` checks every numbered
bank with KARN rules on/off, all-frame remapping, shared tilesets, nearest-color
matching, rollback, and save/reopen through the same remapping implementation.

`palette_bank_operations_ui_tests` checks palette move/copy commands, their
remap button, and resizing cel data linked across banks. The Lua scripts
`palette_bank_operations.lua`, `palette_bank_resize.lua`, and
`palette_bank_copy_merged.lua` cover all numbered banks, fixed-size color copies,
smooth resizing, shared tilesets, selection and cel-image exports, merged RGB
copies, default paste positions, reference alpha, and undo/redo.

The Windows setup/build scripts have a separate test suite that uses the
PowerShell included with Windows 11. From the repository root, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/windows-build-scripts.Tests.ps1
```

These tests use local fixtures for downloads, Git initialization, and installers;
they do not install software or need administrator rights. Fixtures are kept
under `.build/windows-script-tests`. A clean-machine installation still needs
an end-to-end check with network access and Microsoft's actual installer.

Test suite for [Aseprite](https://github.com/aseprite/aseprite)
to avoid breaking backward compatibility.

This directory is cloned by the
[build.yml](https://github.com/aseprite/aseprite/blob/main/.github/workflows/build.yml)
action to run several automated tests after Aseprite is compiled:

* Save/load file formats correctly. For this we have `.aseprite`, `.png`,
  `.gif`, etc. files [sprites](https://github.com/aseprite/aseprite/tree/main/tests/sprites)
  folder.
* Test backward compatibility with [Aseprite CLI](https://www.aseprite.org/docs/cli/) options
* Future [scripting API](https://github.com/aseprite/api) using [scripts](https://github.com/aseprite/aseprite/tree/main/tests/scripts)

## How to run tests?

You have to set the `ASEPRITE` environment variable pointing to the
Aseprite executable and then run `run-tests.sh` from Bash:

    export ASEPRITE=$HOME/your-aseprite-build/bin/aseprite
    cd tests
    bash run-tests.sh

You can filter some tests with a regex giving a parameter to
`run-tests.sh`, for example:

    run-tests.sh color

Should run all tests which have the `color` word in their name.
