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
- Sanitizers (GCC and Clang only): `cmake --workflow --preset asan` builds and tests with AddressSanitizer and UndefinedBehaviorSanitizer, and `--preset tsan` with ThreadSanitizer
- Script tests type-check the test and sample scripts with lua-language-server, which CMake downloads (pinned in `tools/luals.env`). Without it they're skipped with a message; CI requires it (`-DLS_REQUIRE_LUA_LANGUAGE_SERVER=ON`). After changing the scripting API, update the committed annotations by running the `Core` tests with `LS_UPDATE_LUA_ANNOTATIONS=1`
- Build trees go to `build/<preset>`. Dependency archives are cached in `.cache/dependencies`, lavapipe in `.cache/lavapipe`, and lua-language-server in `.cache/luals`

Code style tools, pinned to one LLVM version (install once, ideally in a virtual environment: `pip install -r tools/requirements.txt`):

```sh
python tools/format.py --fix                    # format every source file (without --fix: check only, as CI does)
python tools/tidy.py --build-dir build/debug    # clang-tidy, on a configured build tree
```

Before every pull request: format, clang-tidy, and every test tier in Debug, Release and Dist.

## Project Layout
| Path | Contents |
|---|---|
| `Source/Core` | `LodestoneCore` - ECS and scenes (`Lodestone/Scene`), component reflection (`Lodestone/Reflection`), the simulation (`Lodestone/Simulation`), input codes and commands (`Lodestone/Input`), assets (`Lodestone/Asset`), file formats (`Lodestone/Serialization`), skeletal animation (`Lodestone/Animation`), physics (`Lodestone/Physics`, on Jolt, linked privately) and scripting (`Lodestone/Scripting`, Lua with sol2, linked privately). Never links GLFW, nvrhi or miniaudio - the configure step checks |
| `Source/Client` | `LodestoneClient` - window and device input (`Lodestone/Platform`, `Lodestone/Input`), the graphics device and swapchain (`Lodestone/Graphics`), the scene renderer (`Lodestone/Renderer`), shaders (`Shaders`), and audio |
| `Source/Editor` | `LodestoneEditorLib` and the `LodestoneEditor` executable - the editor context, commands and undo (`Lodestone/Editor`, `Lodestone/Editor/Commands`), the UI panels (`Lodestone/Editor/UI`), and the MCP server, its tools and transports (`Lodestone/Editor/Mcp`). Not built in Dist |
| `Source/Runtime` | `LodestoneRuntime` - the player for exported games |
| `Source/Server` | `LodestoneServer` - the headless dedicated server |
| `Tests` | Test executables by target (`Tests/Core` is `LodestoneCoreTests`, `Tests/Client` is `LodestoneClientTests`, `Tests/Render` is `LodestoneRenderTests`, `Tests/Editor` is `LodestoneEditorTests` and `LodestoneEditorIntegrationTests` plus the Python agent smoke test, `Tests/Fuzz` is `LodestoneFuzzTests` with its corpus), the renderer's benchmark (`Tests/Benchmark`, `LodestoneBenchmark`, with its baseline), shared helpers in `Tests/Common`, files tests load in `Tests/Fixtures` (models and environments come from `tools/generate-test-assets.py`, except the CC0 HDRIs from Poly Haven in `Environments/PolyHaven`, credited in THIRD_PARTY_LICENSES.md; `Scripting` is the test scripts' project, and `ScriptingMistakes` scripts the type-check must fault), and reference images in `Tests/ReferenceImages` |
| `Samples` | Sample games, each a project, built by an agent through MCP alone: `Pong`. `Tests/Editor/SampleGameTests.cpp` plays them from start to finish |
| `cmake` | Build configurations, compiler settings, dependencies, shader compilation (`ls_add_shaders()`), lavapipe, lua-language-server, `ls_configure_target()` and the layering check `ls_forbid_dependencies()` |
| `tools` | Formatting and clang-tidy scripts, the pinned tool versions, the pinned lavapipe version and its Linux build script, the pinned lua-language-server version and the script type-check (`check-lua.py`), the test asset generator, and the benchmark's CI summary |
| `docs` | Design docs, milestones and decisions |
| `.github` | CI workflow and its composite actions |

Headers live next to their sources and are included by their path below the target's source directory: `#include "Lodestone/Core/Log.h"`. Every target that compiles Lodestone code calls `ls_configure_target()`, which turns on warnings as errors.

Engine fundamentals in `Source/Core/Lodestone/Core`: `Base.h` (`Ref`/`Scope`, platform and configuration macros), `Error.h` (`Error`, `ErrorCode`), `Log.h` (`LS_CORE_*` and `LS_*` macros), `Assert.h` (`LS_CORE_ASSERT`, `LS_ASSERT`), and `RunMain.h`, which every executable's `main()` goes through. Also there: `UUID.h`, `FileSystem.h` (bounded reads, atomic writes, UTF-8 paths), `RandomGenerator.h`, `EnumFlags.h`, `Image.h` (RGBA8 images, PNG/JPEG loading and PNG saving), `HdrImage.h` (float images, Radiance .hdr), `BoundingBox.h`, `Rotation.h` (Euler angles, as the inspector and scripts show rotations), `Base64.h`, `CommandLine.h` and `Environment.h`.

Scenes and the simulation in `Source/Core/Lodestone`: `Scene/Scene.h` (entities with UUIDs, the hierarchy, snapshots), `Scene/Entity.h` (the entity handle), `Scene/Components.h` (the core components), `Scene/SceneSerializer.h` (`.lscene` files), `Reflection/ComponentRegistry.h` (the reflection registry), `Simulation/Simulation.h` (fixed ticks, systems, interpolation, save and restore), `Input/InputCommand.h`, `Asset/AssetRegistry.h` and `Serialization/FileFormat.h` (versioned documents and migrations). See Decisions [0010](docs/Decisions/0010-component-reflection.md), [0011](docs/Decisions/0011-file-formats.md) and [0012](docs/Decisions/0012-simulation-and-rollback.md). Models are imported from glTF by `Asset/ModelImporter.h` into a CPU `Asset/Model.h` (`Asset/ModelCache.h` keeps a project's models imported for whatever needs them without a renderer), and placed in scenes as entities by `Scene/ModelEntities.h`; the components that draw and light scenes are in `Scene/RenderComponents.h` (see [Decision 0019](docs/Decisions/0019-model-import.md)). Skeletal animation is in `Lodestone/Animation`: `AnimationComponents.h` (`ModelInstance`, `ModelNode`, `Skin` and `Animator`), `Animation.h` (clip sampling, crossfades and the simulation's animation system) and `ScenePose.h`, which derives where entities are drawn from animator state - the renderer, picking and the overlay all ask it (see [Decision 0020](docs/Decisions/0020-skeletal-animation.md)). Physics is in `Lodestone/Physics`: `PhysicsComponents.h` (`RigidBody`, the colliders, `CharacterController` and `PhysicsSettings`) and `PhysicsWorld.h`, a stateful simulation system (`StatefulSystem` in `Simulation.h`) that makes Jolt bodies from the components each tick, steps them, writes transforms and velocities back, gathers collision and trigger events, answers raycasts, and saves and restores Jolt's state with the simulation's. Jolt is linked privately: its headers are only included by the module's own sources (`JoltRuntime.h`, `PhysicsLayers.h`, `StateBuffer.h`) - see [Decision 0021](docs/Decisions/0021-physics.md).

Scripting is in `Lodestone/Scripting` (see [Scripting](docs/Features/Scripting.md) and [Decision 0022](docs/Decisions/0022-scripting.md)): `ScriptComponents.h` (the `Script` component: a script asset and its properties as JSON), `ScriptSystem.h` (the simulation's scripting system - behavior instances, their callbacks, physics events, hot reload), `ScriptRuntime.h` (the sandboxed Lua state: memory and instruction limits, `require` from the project, errors with tracebacks), `ScriptBindings.h` (the API: entities, components through reflection, the scene, input, physics, animation, time, the log and the math types), `ScriptAssets.h` (scripts and prefabs from a project, or from memory in tests) and `LuaAnnotations.h` (the lua-language-server annotations - `LuaApi.lua`, embedded at build time, plus what the registries add - and the files projects get). Lua and sol2 are linked privately: `ScriptRuntime.h` and `ScriptBindings.h` include sol2, so only the module's own sources include those two; everything else uses `ScriptSystem.h`.

The editor in `Source/Editor/Lodestone/Editor`: `EditorContext` (the project, the open document, selection, undo history and play mode, shared by the UI and MCP), `Commands/` (every edit is a command, so it undoes - see [Decision 0015](docs/Decisions/0015-editor-commands.md)), `SceneView` (the viewport and screenshots), `UI/` (the Dear ImGui panels), and `Mcp/` (`McpServer`, the tools in `EditorTools.cpp`, and the stdio and HTTP transports - see [Decision 0016](docs/Decisions/0016-mcp-server.md)). Prefabs are in Core: `Scene/PrefabSerializer.h` and `Scene/EntitySerializer.h` (entity trees as data), and projects in `Project/Project.h`.

Graphics in `Source/Client/Lodestone/Graphics`: `GraphicsDevice` (the Vulkan device wrapped in NVRHI; one at a time, headless or for windows), `Swapchain`, `RenderTarget` (offscreen color and depth), `ReadTexture()` for reading rendered images back, `ShaderModule.h`, and `ImGuiRenderer` (Dear ImGui through NVRHI). Dear ImGui's asserts go through the engine's (`Source/Client/ThirdPartyConfig/imgui`). Shaders are HLSL in `Source/Client/Shaders`, listed in `Shaders.cfg` and `ls_add_shaders()`, and compiled at build time into headers the code includes (see [Decision 0008](docs/Decisions/0008-shader-pipeline.md)); `ShaderTypes.h` holds the C++ twins of their constant structs.

The scene renderer in `Source/Client/Lodestone/Renderer` (see [Decision 0018](docs/Decisions/0018-scene-renderer.md)): `SceneRenderer` draws a `RenderWorld` (from `ExtractRenderWorld()` of a `ScenePose`, skinned meshes with their joints' matrices) seen from a `RenderView` into a `SceneRenderTarget`, and reports `RenderStats`; `RenderAssets` loads models (`GpuModel`) and environments (`EnvironmentMap`) by UUID from a `RenderAssetSource`; `ShadowCascades`, `Frustum`, `ImageBasedLighting` (spherical harmonics, the BRDF table, equirectangular mapping) and `MipChain` are the CPU parts, unit-tested; `LineRenderer` draws overlays. The editor's `SceneView` renders through it, with its grid, markers and picking.

## Driving the Editor Through MCP
`.mcp.json` gives Claude Code two servers: `lodestone-editor`, the editor that's running (HTTP on port 7850), and `lodestone-headless`, a headless editor it starts itself over stdio from `build/debug` (build the `debug` preset first). Start with `project_info`, then `project_create` or `project_open`; `component_types` lists every component's fields. See [AI Control](docs/AIControl.md) for the tools and options.

## Skills
Workflows that repeat have skills in `.claude/skills/`:
- `build-and-test` - building, testing, formatting and clang-tidy on any platform
- `ship-change` - branch, review, pull request, CI and squash-merge
- `add-dependency` - adding a pinned third-party dependency
- `update-reference-images` - adding a reference-image test, or deliberately updating reference images
- `add-component` - adding a component, with its reflection registration and tests
- `add-mcp-tool` - adding an MCP tool for an editor feature, with its tests
- `run-benchmark` - measuring the renderer against its baseline, and recording a new baseline
- `add-script-binding` - adding a function, a table or a callback to the scripting API, with its annotation and tests

## Adding a Component
Components are plain structs with public PascalCase fields and default member initializers. Registering one in the reflection registry is what gives it serialization now, and the inspector, MCP tools, script bindings and replication as they arrive (see [Decision 0010](docs/Decisions/0010-component-reflection.md)):

```cpp
struct LightComponent
{
	glm::vec3 Color{1.0f};
	float Intensity = 1.0f;
};

registry.Register<LightComponent>("Light", "A point light")
	.Field("Color", &LightComponent::Color, {.Description = "Linear RGB", .Min = 0.0, .Max = 1.0})
	.Field("Intensity", &LightComponent::Intensity, {.Description = "In candela", .Min = 0.0});
```

- Register it where its module registers its components (the core components are in `RegisterCoreComponents()`), before any scene uses it. The name is permanent: files, MCP and scripts use it
- Fields must be of the reflected types (`FieldType`: bool, int, uint, float, vectors, quaternions, strings, UUIDs); give limits and a description, and flag `Replicated` fields that clients need and `ReadOnly` ones only the engine may change
- State that's part of the simulation but not of files goes in an `Internal` component. Every piece of simulation state must be in a registered component - or, for a system with state of its own such as the physics world, in what its `StatefulSystem` saves - or rollback won't restore it
- Add custom handling only where the generic path isn't enough (the scene loader applies `Hierarchy` through `Scene::SetParent`)
- Test the registration, a save/load round trip, and anything custom. The `add-component` skill has the checklist

## Adding a Script Binding
Components need no bindings: scripts reach every registered component through reflection. Bindings are for what isn't a component - the `Scene`, `Input`, `Physics`, `Animation`, `Time` and `Log` tables, `Entity` methods, the math types and behavior callbacks:

```cpp
// In ScriptApi::BindPhysics() (Source/Core/Lodestone/Scripting/ScriptBindings.cpp)
physics["IsSleeping"] = [this](const ScriptEntity& entity, sol::this_state state)
{
	Resolve(state, entity);	// raises a Lua error for an entity that's gone
	return RequirePhysics(state).IsSleeping(entity.Id);
};
```

- Declare it with its annotation in `Scripting/LuaApi.lua` - its parameters, return type and a description; the annotations are the API's documentation, and a test fails for a function scripts can reach that they don't declare
- Raise Lua errors for bad arguments with `Raise(state, message)`, naming what was wrong; never assert on script input
- Call it from a test script in `Tests/Fixtures/Scripting/Assets/Scripts` with checks of its results and errors: a test fails for an annotated function no test script calls
- Update the committed annotations (`LS_UPDATE_LUA_ANNOTATIONS=1`). State a binding changes must be in components, so rollback restores it. The `add-script-binding` skill has the checklist

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
