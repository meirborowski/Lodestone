# Lodestone

[![CI](https://github.com/meirborowski/Lodestone/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/meirborowski/Lodestone/actions/workflows/ci.yml)

Lodestone is a simple, production-grade 3D game engine for Windows, macOS and Linux. It's built in C++23 with a Vulkan renderer, Lua scripting, Jolt physics and server-authoritative multiplayer, and its editor is fully controllable by AI agents through an embedded MCP server.

Lodestone is under construction, milestone by milestone - see [Milestones](docs/Milestones.md) for progress.

## Building

### Prerequisites
- CMake 3.28+ and Ninja
- Git LFS (`git lfs install` once, before cloning)
- Python 3.12+, for the code style scripts and the editor's MCP end-to-end tests
- A C++23 compiler:
  - **Windows** - Visual Studio 2026 (MSVC Build Tools 14.50+) with the "Desktop development with C++" workload
  - **macOS** - Xcode 16.3+
  - **Ubuntu 24.04+** - GCC 14 (`sudo apt install g++-14`) or Clang 19 (`sudo apt install clang-19`)
- A graphics driver with Vulkan 1.3, to run the engine
- **macOS** - the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) 1.4.363.0, for MoltenVK and the shader compiler. After installing it, run `sudo ./install_vulkan.py` in the SDK's directory
- **Ubuntu** - GLFW's build dependencies and the Vulkan loader: `sudo apt install pkg-config libwayland-dev libxkbcommon-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libvulkan1`

Everything else is downloaded by CMake on the first configure.

Rendering tests render on lavapipe, a software Vulkan driver, so they need no GPU. On Windows CMake downloads it; on Linux build it once with `tools/build-lavapipe.sh` (the script lists the packages it needs). Without it, the reference-image tests are skipped.

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

## The Editor
```sh
build/debug/Source/Editor/LodestoneEditor [--project <directory>]
```
Create or open a project from the File menu, then build scenes in the viewport, hierarchy and inspector (see [Editor](docs/Features/Editor.md)).

AI agents drive the editor through MCP. The repository's `.mcp.json` gives Claude Code two servers: `lodestone-editor`, which attaches to a running editor over HTTP on `127.0.0.1:7850`, and `lodestone-headless`, which starts a headless editor of its own over stdio (`--headless --mcp-stdio`). See [AI Control](docs/AIControl.md) for the tools.

## Documentation
- [AGENTS.md](AGENTS.md) - how to work on Lodestone: rules, commands and project layout
- [Architecture](docs/Architecture.md), [Tech Stack & Build](docs/TechStack.md), [Code Style](docs/CodeStyle.md) and [Testing & CI](docs/Testing.md)
- [Decisions](docs/Decisions/README.md) - why things are the way they are

## Third-Party Software
Lodestone's dependencies and their licenses are listed in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
