# Lodestone

Lodestone is a production-grade, simple and straightforward 3D game engine for Windows, macOS and Linux (Ubuntu 24+). This is professional-grade software that needs to be stable, fully tested, and real-world deployable. No hacks, no shortcuts - solid as a rock.

The documents in `docs/` describe the target design of the engine. Build it milestone by milestone - see [Milestones](docs/Milestones.md) for the order and current progress.

## Rules
- Ask questions only when absolutely necessary - work autonomously
- IMPORTANT! DO NOT look outside of the working directory or use other code on this computer as reference. Third-party dependencies fetched by CMake into the build directory are fine
- Testing is of the utmost importance - follow [Testing & CI](docs/Testing.md), and run automated tests on every change
- Never delete, skip or weaken a test, loosen a tolerance, or regenerate a reference image just to get a build green - see [Test Integrity](docs/Testing.md#test-integrity)
- Do things properly - this is production-grade, not a hack project
- Follow the [Code Style](docs/CodeStyle.md) in all code
- Work through the [Milestones](docs/Milestones.md) in order. Don't start a milestone until the previous one meets its definition of done
- Keep [Current State](docs/Milestones.md#current-state) in Milestones.md up to date, so the next session can pick up where you stopped
- Record significant design decisions in [Decisions](docs/Decisions/README.md) - especially ones made without asking
- Use Git LFS for binary assets (HDRIs, glTF models, textures, audio, fonts, reference images)
- Keep a THIRD_PARTY_LICENSES.md listing every dependency and its license. Only use permissively licensed dependencies (e.g. MIT, BSD, zlib, Apache 2.0, public domain/CC0)

## Building and Testing
Prerequisites: CMake 3.28+, Ninja, Git LFS, Python 3.12+, and a C++23 compiler - MSVC from Visual Studio 2026 (Windows), Xcode 16.3+ (macOS), or GCC 14+ / Clang 19+ (Ubuntu 24.04+). macOS also needs the Vulkan SDK (for DXC and MoltenVK), and Ubuntu the window system development files and the Vulkan loader. See [Tech Stack & Build](docs/TechStack.md#build-prerequisites).

```sh
cmake --workflow --preset debug     # configure, build and run every test (also: release, dist)
cmake --preset debug                # or step by step: configure,
cmake --build --preset debug        # build,
ctest --preset debug -L unit        # and run a test tier (unit, render, integration, fuzz)
```

- Windows: run these from a Developer PowerShell for VS 2026 (or after `vcvars64.bat`), so Ninja finds MSVC. If another compiler is on the `PATH`, set `CC=cl` and `CXX=cl` before the first configure
- Ubuntu: GCC 13 is the default and too old - configure with `CC=gcc-14 CXX=g++-14` or `CC=clang-19 CXX=clang++-19`. Install GLFW's build dependencies and the Vulkan loader first: `sudo apt install pkg-config libwayland-dev libxkbcommon-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libvulkan1`
- macOS: install the Vulkan SDK, then run `sudo ./install_vulkan.py` in its directory, so DXC, the Vulkan loader and MoltenVK are found
- Rendering tests render on lavapipe at a pinned Mesa version, never the GPU. Windows downloads it automatically; on Linux, build it once with `tools/build-lavapipe.sh` and reconfigure. Without lavapipe, reference-image tests are skipped with a message; macOS always skips them (see [Reference Images](docs/Testing.md#reference-images))
- Sanitizers (GCC and Clang only): `cmake --workflow --preset asan` builds and tests with AddressSanitizer and UndefinedBehaviorSanitizer
- Build trees go to `build/<preset>`. Dependency archives are cached in `.cache/dependencies`, and lavapipe in `.cache/lavapipe`

Code style tools, pinned to one LLVM version (install once, ideally in a virtual environment: `pip install -r tools/requirements.txt`):

```sh
python tools/format.py --fix                    # format every source file (without --fix: check only, as CI does)
python tools/tidy.py --build-dir build/debug    # clang-tidy, on a configured build tree
```

Before every pull request: format, clang-tidy, and every test tier in Debug, Release and Dist.

## Project Layout
| Path | Contents |
|---|---|
| `Source/Core` | `LodestoneCore` - ECS, scenes, assets and the simulation. Never links GLFW, nvrhi or miniaudio |
| `Source/Client` | `LodestoneClient` - window and device input (`Lodestone/Platform`, `Lodestone/Input`), the graphics device, swapchain and renderer (`Lodestone/Graphics`), shaders (`Shaders`), and audio |
| `Source/Editor` | `LodestoneEditor` - editor UI, MCP server, headless mode. Not built in Dist |
| `Source/Runtime` | `LodestoneRuntime` - the player for exported games |
| `Source/Server` | `LodestoneServer` - the headless dedicated server |
| `Tests` | Test executables by target (`Tests/Core` is `LodestoneCoreTests`, `Tests/Render` is `LodestoneRenderTests`), shared helpers in `Tests/Common`, and reference images in `Tests/ReferenceImages` |
| `cmake` | Build configurations, compiler settings, dependencies, shader compilation (`ls_add_shaders()`), lavapipe, and `ls_configure_target()` |
| `tools` | Formatting and clang-tidy scripts, the pinned tool versions, and the pinned lavapipe version and its Linux build script |
| `docs` | Design docs, milestones and decisions |
| `.github` | CI workflow and its composite actions |

Headers live next to their sources and are included by their path below the target's source directory: `#include "Lodestone/Core/Log.h"`. Every target that compiles Lodestone code calls `ls_configure_target()`, which turns on warnings as errors.

Engine fundamentals in `Source/Core/Lodestone/Core`: `Base.h` (`Ref`/`Scope`, platform and configuration macros), `Error.h` (`Error`, `ErrorCode`), `Log.h` (`LS_CORE_*` and `LS_*` macros), `Assert.h` (`LS_CORE_ASSERT`, `LS_ASSERT`), and `RunMain.h`, which every executable's `main()` goes through. Also there: `Image.h` (RGBA8 images, PNG/JPEG loading and PNG saving), `CommandLine.h` and `Environment.h`.

Rendering in `Source/Client/Lodestone/Graphics`: `GraphicsDevice` (the Vulkan device wrapped in NVRHI; one at a time, headless or for windows), `Swapchain`, and `ReadTexture()` for reading rendered images back. Shaders are HLSL in `Source/Client/Shaders`, listed in `Shaders.cfg` and `ls_add_shaders()`, and compiled at build time into headers the code includes (see [Decision 0008](docs/Decisions/0008-shader-pipeline.md)).

## Skills
Workflows that repeat have skills in `.claude/skills/`:
- `build-and-test` - building, testing, formatting and clang-tidy on any platform
- `ship-change` - branch, review, pull request, CI and squash-merge
- `add-dependency` - adding a pinned third-party dependency
- `update-reference-images` - adding a reference-image test, or deliberately updating reference images

## Git
- Work on a branch, push it to the [GitHub repo](https://github.com/meirborowski/Lodestone), and squash-merge it into main through a pull request once CI passes - never push directly to main
- main keeps a linear history: squash merging is the only merge method the repository allows, and branch protection requires it
- IMPORTANT: before every commit, review the full diff (in Claude Code, run `/code-review`), and make sure all changes comply with the code style, meet production-grade quality standards, and have been properly tested, with unit tests that pass where necessary
- CI must stay green - a failing build gets fixed before any other work

## Non-Goals
- Mobile, web and console platforms
- Animation state machines, blend trees, IK, root motion and morph targets (for now - may be added later)
- Matchmaking, lobbies, NAT traversal and relay servers - players connect directly by IP and port
- Encrypted network traffic and anti-cheat

## Documentation
| Doc | Read when |
|---|---|
| [Code Style](docs/CodeStyle.md) | Before writing any code |
| [Testing & CI](docs/Testing.md) | Before any change - every change needs tests |
| [Milestones](docs/Milestones.md) | Starting work, or finishing a milestone |
| [Tech Stack & Build](docs/TechStack.md) | Adding a dependency, changing the build, or setting up a platform |
| [Architecture](docs/Architecture.md) | Working on engine structure, the ECS, the simulation loop, scenes, file formats or the asset system |
| [Decisions](docs/Decisions/README.md) | Making or revisiting a significant design decision |
| [AI Control](docs/AIControl.md) | Working on the MCP server, headless mode, or any editor feature (every editor action needs an MCP tool) |
| [Editor](docs/Features/Editor.md) | Working on editor panels, the gizmo, undo/redo, play mode or prefabs |
| [3D Renderer](docs/Features/Renderer.md) | Working on rendering, materials, lighting, post-processing or performance |
| [Animation](docs/Features/Animation.md) | Working on skeletal animation or skinning |
| [3D Physics](docs/Features/Physics.md) | Working on physics |
| [Scripting](docs/Features/Scripting.md) | Working on the scripting runtime or API |
| [Input](docs/Features/Input.md) | Working on input |
| [Audio](docs/Features/Audio.md) | Working on audio |
| [In-Game UI & Text](docs/Features/UI.md) | Working on fonts, text or in-game UI |
| [Networking](docs/Features/Networking.md) | Working on multiplayer, replication or anything that changes game state (it may need to replicate) |
| [Export](docs/Features/Export.md) | Working on exporting, asset packing or the Dist runtime |

## Keeping This Up to Date
Keep AGENTS.md, the docs in `docs/` and the skills in `.claude/skills/` up to date as the engine evolves. As each of these comes to exist, document it here:
- Build and test commands for each platform
- Project layout
- How to add a component (data and reflection registration - which drives serialization, the inspector, MCP, script bindings and replication - plus any custom handling it needs)
- How to add a script binding

Create skills for workflows that repeat, such as adding a component, adding a script binding, or updating reference images.
