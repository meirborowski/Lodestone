# Lodestone

[![CI](https://github.com/meirborowski/Lodestone/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/meirborowski/Lodestone/actions/workflows/ci.yml)

Lodestone is a simple, production-grade 3D game engine for Windows, macOS and Linux. It's built in C++23 with a Vulkan renderer, Lua scripting, Jolt physics and server-authoritative multiplayer, and its editor is fully controllable by AI agents through an embedded MCP server.

Lodestone is under construction, milestone by milestone - see [Milestones](docs/Milestones.md) for progress.

## Building

### Prerequisites
- CMake 3.28+ and Ninja
- Git LFS (`git lfs install` once, before cloning)
- Python 3.12+, for the code style scripts
- A C++23 compiler:
  - **Windows** - Visual Studio 2026 (MSVC Build Tools 14.50+) with the "Desktop development with C++" workload
  - **macOS** - Xcode 16.3+
  - **Ubuntu 24.04+** - GCC 14 (`sudo apt install g++-14`) or Clang 19 (`sudo apt install clang-19`)

Everything else is downloaded by CMake on the first configure.

### Build and test
```sh
cmake --workflow --preset debug
```
This configures a Debug build in `build/debug`, builds it and runs the tests. The `release` and `dist` presets build the other configurations: Dist is the fully optimized configuration used for exported games, without the editor.

- **Windows**: run it from a *Developer PowerShell for VS 2026*, so Ninja finds MSVC
- **Ubuntu**: choose the compiler before the first configure, e.g. `CC=gcc-14 CXX=g++-14 cmake --workflow --preset debug`

### Code style
The code style tools are pinned to one LLVM version:
```sh
pip install -r tools/requirements.txt
python tools/format.py --fix
python tools/tidy.py --build-dir build/debug
```

## Documentation
- [AGENTS.md](AGENTS.md) - how to work on Lodestone: rules, commands and project layout
- [Architecture](docs/Architecture.md), [Tech Stack & Build](docs/TechStack.md), [Code Style](docs/CodeStyle.md) and [Testing & CI](docs/Testing.md)
- [Decisions](docs/Decisions/README.md) - why things are the way they are

## Third-Party Software
Lodestone's dependencies and their licenses are listed in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
