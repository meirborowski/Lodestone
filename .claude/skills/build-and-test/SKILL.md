---
name: build-and-test
description: Build Lodestone and run its tests, formatting check and clang-tidy, on Windows, macOS or Linux. Use after any code change, before committing, and before opening a pull request.
---

# Build and Test Lodestone

Every change gets built and tested (see `docs/Testing.md`). Run the steps below from the repository root.

## 1. Environment
- **Windows**: the commands must run in a Visual Studio 2026 developer environment, or Ninja can't find MSVC. In a plain PowerShell, enter it first:
  ```powershell
  $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
  & "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
  $env:CC = 'cl'; $env:CXX = 'cl'
  ```
  Each new shell needs this again.
- **Ubuntu**: export `CC=gcc-14 CXX=g++-14` (or `CC=clang-19 CXX=clang++-19`) before the first configure of a build tree.
- **Code style tools**: `pip install -r tools/requirements.txt`, ideally in a virtual environment (`.venv` in the repository root is ignored by Git). Put its scripts directory on the `PATH`.

## 2. Build and test
```sh
cmake --workflow --preset debug
```
This configures `build/debug`, builds it, and runs every test. The other presets are `release`, `dist` and, with GCC or Clang, `asan`. For a quicker loop: `cmake --build --preset debug` then `ctest --preset debug -L unit`.

Failed tests print their output. To run one doctest case directly: `build/debug/Tests/LodestoneCoreTests --test-case="<name>"`.

## 3. Style
```sh
python tools/format.py --fix
python tools/tidy.py --build-dir build/debug
```
clang-tidy treats every finding as an error. Fix the code rather than suppressing a check; if a check is wrong for this codebase, change `.clang-tidy` and say why in `docs/Decisions/0006-code-style-tooling.md`.

## 4. Before a pull request
Run the `debug`, `release` and `dist` workflows (every test tier), the format check and clang-tidy. All must pass. Never skip, weaken or delete a test to get there (see Test Integrity in `docs/Testing.md`).
