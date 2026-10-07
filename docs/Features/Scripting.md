# Scripting

Lua 5.4, bound with sol2 (see [Decision 0001](../Decisions/0001-scripting-language.md)).

- Control entities/components, run in the update loop, and handle entity creation/destruction
- Spawn new entities/prefabs
- Access to input, physics, animation, audio, UI and networking from scripts
- Component bindings come from the reflection registry (see [Components and Reflection](../Architecture.md#components-and-reflection))
- Scripts hot reload in the editor
- Script errors are reported in the console with file and line number, and never crash the editor or the game
- The engine generates LuaLS (lua-language-server) annotation files from the bindings, so editors and agents get autocomplete and type checking. CI type-checks the test and sample scripts against them
- Scripts aren't re-simulated during client-side prediction (see [Prediction](Networking.md#prediction))
- The test scenes exercise the entire scripting API (see [Testing & CI](../Testing.md))

## Sandbox
Scripts come from game projects, which may not be trusted, so they run sandboxed:
- The `io` and `debug` libraries aren't loaded; from `os`, only `os.time`, `os.clock` and `os.date` are available
- `require` loads Lua scripts from the project only. Native modules can't be loaded (`package.loadlib` and the C loaders are removed), and `dofile` and `loadfile` are removed
- Precompiled bytecode is rejected (`load` accepts text only) - malicious bytecode can corrupt memory
- A memory limit (enforced through the Lua allocator) and an instruction-count hook stop runaway scripts, which are reported like any other script error
