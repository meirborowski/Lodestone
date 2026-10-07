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

## Git
- Work on a branch, push it to the [GitHub repo](https://github.com/meirborowski/Lodestone), and merge it into main through a pull request once CI passes - never push directly to main
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
