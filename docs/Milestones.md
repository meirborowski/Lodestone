# Milestones

Work through these in order. Don't start a milestone until the previous one is done.

A milestone is done when:
- Every item and acceptance criterion listed under it is met
- Its editor-facing features have MCP tools, each with automated tests (see [AI Control](AIControl.md))
- CI is green on all three platforms (see [CI](Testing.md#ci))
- The test scenes cover the new features
- Decisions made along the way are recorded in [Decisions](Decisions/README.md)
- AGENTS.md, the docs and the skills are updated
- The work has been code reviewed and squash-merged into main through a pull request

Tick an item (`[x]`) when it's done, and tick the milestone in [Progress](#progress) when it meets the definition of done.

## Current State
Keep this section current, so a new session can pick up where the last one stopped.

- **Active milestone:** none - Milestone 2 is next
- **In progress:** nothing. Milestone 1 is done: CI is green on every platform and configuration, and the acceptance checks passed (a misformatted header failed the Format job, and an unused variable failed every build and clang-tidy, then both were reverted)
- **Next:** start Milestone 2. Install the Vulkan SDK locally first (it's a build prerequisite from Milestone 2), add GLFW, nvrhi and ShaderMake with `ls_declare_dependency()`, and bring the Vulkan device up on MoltenVK first. `LodestoneClient` is still a placeholder (macOS `ranlib` warns that its archive has no symbols until it gets real code)
- **Watch for:** MSVC Build Tools 14.52 reaching the `windows-2025-vs2026` image - CI posts a notice until then ([Decision 0004](Decisions/0004-msvc-cpp23-switch.md))

## Open Decisions
- **SIL Open Font License 1.1 for fonts** - most good free fonts use it, and it isn't on the permissive-license list in AGENTS.md. Needs the user's decision before Milestone 10. Recommendation: allow it for font files only - it permits bundling and redistributing fonts with software, as long as the fonts aren't sold on their own

## Progress
- [x] [1. Foundation](#1-foundation)
- [ ] [2. Window, input and rendering backend](#2-window-input-and-rendering-backend)
- [ ] [3. ECS, scenes and assets](#3-ecs-scenes-and-assets)
- [ ] [4. Editor and AI control](#4-editor-and-ai-control)
- [ ] [5. 3D renderer](#5-3d-renderer)
- [ ] [6. Animation](#6-animation)
- [ ] [7. Physics](#7-physics)
- [ ] [8. Scripting](#8-scripting)
- [ ] [9. Audio](#9-audio)
- [ ] [10. In-game UI and text](#10-in-game-ui-and-text)
- [ ] [11. Networking](#11-networking)
- [ ] [12. Export](#12-export)
- [ ] [13. Validation](#13-validation)

## Milestones

### 1. Foundation
- [x] CMake project with the target layout from [Architecture](Architecture.md#targets-and-layering) (stub executables are fine), and Debug, Release and Dist configurations
- [x] Dependency fetching with `FetchContent`, pinned to exact versions
- [x] Logging, asserts, the `Error` type and `std::expected` error handling
- [x] doctest, with CTest labels for the [test tiers](Testing.md#test-tiers)
- [x] `.clang-format` and `.clang-tidy` encoding the [Code Style](CodeStyle.md#enforcement), with warnings as errors for engine code
- [x] GitHub Actions CI on every platform and configuration (Dist included), plus format, clang-tidy and ASan/UBSan jobs, with compiler, dependency and Git LFS caching (see [CI](Testing.md#ci))
- [x] Toolchain checks on CI: `std::expected` and the other C++23 features we use compile on GCC 14, Clang 19, MSVC and Apple Clang; the hosted Windows image has MSVC 14.52+ (otherwise CI installs the Build Tools). 14.52 isn't a stable release yet, so CI reports the image's version (14.51) and builds with `/std:c++latest` until it is - see [Decision 0004](Decisions/0004-msvc-cpp23-switch.md)
- [x] Lua 5.4 and sol2, pinned to a commit that builds cleanly on all four compilers, with a smoke-test binding
- [x] Pull-request workflow: main is protected, so CI must pass before merging (enabling branch protection on GitHub needs the user's approval)
- [x] AGENTS.md / CLAUDE.md / skills, THIRD_PARTY_LICENSES.md, Git LFS (`.gitattributes`)

**Acceptance:** every CI job is green on a pull request; a deliberately misformatted file and a deliberate compiler warning each fail CI (checked once, then reverted); Dist builds on every platform.

### 2. Window, input and rendering backend
- [ ] GLFW window and device input (keyboard, mouse, gamepads)
- [ ] nvrhi Vulkan device and swapchain - bring it up on MoltenVK first, since it's the backend most likely to cause trouble
- [ ] Shader compilation (HLSL to SPIR-V with DXC, via ShaderMake)
- [ ] Offscreen/headless rendering
- [ ] macOS CI smoke check: MoltenVK loads and a Vulkan device is found. Decide the macOS fallback for rendering tests (lavapipe, or skipped with a logged reason) and record it as a decision
- [ ] lavapipe at a pinned Mesa version, on CI and locally, and the reference-image tooling (see [Reference Images](Testing.md#reference-images))
- [ ] First reference-image test

**Acceptance:** an offscreen render matches its reference image on lavapipe on every CI platform that has a Vulkan driver; the windowed app runs locally on all three platforms.

### 3. ECS, scenes and assets
- [ ] EnTT and core components (ID, name, transform, hierarchy)
- [ ] Component reflection registry - serialization uses it now; the inspector, MCP, scripting and replication use it later (see [Components and Reflection](Architecture.md#components-and-reflection))
- [ ] Fixed-tick simulation loop, independent of the frame rate, with render interpolation (see [Simulation](Architecture.md#simulation))
- [ ] Per-tick input commands - the simulation reads only input commands, never devices
- [ ] Save and restore of simulation state, for client-side prediction rollback
- [ ] Scene save/load with round-trip tests
- [ ] Versioned file formats with step-by-step migrations (see [File Formats](Architecture.md#file-formats))
- [ ] Asset registry and UUIDs
- [ ] Fuzz tests for the scene and asset metadata loaders

**Acceptance:** `LodestoneCore` builds, and its tests run, without GLFW, nvrhi or miniaudio; a headless test runs the simulation for many ticks from injected input commands; restoring a saved state and re-simulating the same input commands reproduces the same state.

### 4. Editor and AI control
- [ ] Docked editor layout: viewport, scene hierarchy, inspector, content browser, console
- [ ] Gizmo
- [ ] Command system with undo/redo, shared by the editor UI and MCP
- [ ] Play mode
- [ ] Prefabs
- [ ] MCP server with both [transports](AIControl.md#transports) and the [security checks](AIControl.md#security), and `.mcp.json`
- [ ] Generic, reflection-driven component tools, plus tools for entities, scenes, prefabs, play mode, input, screenshots and logs (see [Tools](AIControl.md#tools))
- [ ] Headless mode
- [ ] MCP tests: in-process tests for every tool, and end-to-end tests over both transports

**Acceptance:** an automated agent smoke test builds a scene using only MCP tools - create entities, set components, instance a prefab, save, reload, screenshot - and checks the result.

### 5. 3D renderer
- [ ] glTF import (meshes, materials and textures)
- [ ] PBR materials
- [ ] IBL with HDRIs
- [ ] Soft shadows
- [ ] SSAO
- [ ] HDR pipeline and tonemapping
- [ ] Stats overlay with frame time, CPU/GPU timings and draw calls
- [ ] Fuzz tests for the glTF, image and HDRI importers
- [ ] Benchmark scene, local performance benchmark with a recorded baseline, and CI tracking of the GPU-independent proxies (see [Performance](Testing.md#performance))

**Acceptance:** reference-image tests cover every renderer feature; the benchmark scene sustains 60 fps at 1920x1080 on an RTX 3060-class GPU with every feature enabled.

### 6. Animation
- [ ] glTF skeleton, skin and animation clip import
- [ ] Animator component: play, loop, playback speed and crossfade
- [ ] GPU skinning, supported by shadows, SSAO and the rest of the renderer
- [ ] Editor preview with a timeline to scrub through clips

**Acceptance:** clip sampling, looping and crossfade tests pass against known poses; reference-image tests of skinned meshes at fixed animation times pass.

### 7. Physics
Before starting, confirm the [prediction scope](Features/Networking.md#prediction) and record it as a decision - the character controller and physics state handling are built around it.

- [ ] Jolt integration, stepped once per simulation tick
- [ ] Rigid bodies, colliders, triggers and raycasts, authored as components
- [ ] Collision and trigger events
- [ ] Character controller component (Jolt `CharacterVirtual`), driven by input commands
- [ ] Physics state included in simulation save/restore

**Acceptance:** restoring a saved state and re-simulating the same input commands reproduces the same physics state on the same machine.

### 8. Scripting
- [ ] Full scripting API, including animation and physics
- [ ] Hot reload
- [ ] [Sandbox](Features/Scripting.md#sandbox)
- [ ] LuaLS annotation files generated from the bindings, and CI type-checking of the test and sample scripts
- [ ] Mid-point validation: using only the MCP tools, an agent builds a small single-player game (e.g. Pong) in `Samples/`. Fix every gap found along the way

**Acceptance:** the test scenes exercise the entire scripting API; the sample game plays from start to finish.

### 9. Audio
- [ ] Audio source and listener components
- [ ] 2D and spatial audio, with streaming for music
- [ ] Scripting API
- [ ] Editor preview

**Acceptance:** spatialization tests (attenuation and panning for known source and listener positions) and component and scripting API tests pass.

### 10. In-game UI and text
Before starting, resolve the font license question in [Open Decisions](#open-decisions).

- [ ] SDF text rendering
- [ ] UI elements: text, image, panel and button
- [ ] Anchored layout
- [ ] Scripting API

**Acceptance:** reference-image tests of text and UI at several window sizes and aspect ratios pass.

### 11. Networking
- [ ] ENet transport
- [ ] Replication, driven by the reflection registry
- [ ] Interpolation
- [ ] Client-side prediction and reconciliation
- [ ] RPCs
- [ ] Listen and dedicated servers
- [ ] Multiplayer play mode
- [ ] Network simulation (latency, jitter, packet loss)
- [ ] ThreadSanitizer CI job (sooner, if the engine uses more than one thread before this milestone)

**Acceptance:** the [networking tests](Features/Networking.md#testing) pass on CI on all platforms - multiple clients over loopback under simulated latency, jitter, packet loss and reordering, plus the fuzz tests.

### 12. Export
- [ ] Asset packing
- [ ] Dist runtime and dedicated server builds
- [ ] Export from the editor, MCP and the command line
- [ ] Exports include THIRD_PARTY_LICENSES.md

**Acceptance:** CI exports a sample game and smoke-tests the exported runtime and dedicated server headlessly on every platform.

### 13. Validation
- [ ] Using only the MCP tools, build and export two complete small games in `Samples/`: a single-player game (e.g. Tetris), and a multiplayer 3D game with animated characters. Fix every gap found along the way

**Acceptance:** both exported games play from start to finish on all three platforms.
