# 0001 - Scripting Language: Lua 5.4 with sol2

## Status
Accepted

## Context
Games need a scripting language for gameplay logic. Much of that code will be written by AI agents through MCP, so mistakes should be caught before a script runs where possible. The language must:
- Be permissively licensed and embeddable on Windows, macOS and Linux
- Support hot reload, and report errors with file and line number without crashing the engine
- Be sandboxable, since scripts come from game projects that may not be trusted
- Bind cleanly to C++23 engine code

## Decision
Lua 5.4, bound with sol2 (both MIT):
- sol2 releases are infrequent, so it's pinned to a commit that builds cleanly on every supported compiler
- The engine generates LuaLS (lua-language-server) annotation files from the bindings, so editors and agents get autocomplete and type checking, and CI type-checks the test and sample scripts against them
- Scripts run in the sandbox described in [Scripting](../Features/Scripting.md#sandbox)

## Alternatives
- **Luau** - Roblox's Lua dialect (MIT), with a built-in gradual type checker and sandboxing. It's the strongest alternative, but sol2 doesn't support it, so we'd write and maintain the binding layer ourselves. Generated LuaLS annotations give us most of the type-checking benefit
- **LuaJIT** - faster, but limited to the Lua 5.1 language, and released as a rolling branch rather than tagged versions. Gameplay scripts at this engine's scale don't need the extra speed
- **C# (.NET hosting)** - strong typing and tooling, but a heavy runtime, complex cross-platform hosting and hot reload, and much larger exported games
- **AngelScript, Wren** - smaller ecosystems, and AI agents know Lua far better

## Consequences
- The script binding layer is written against sol2, driven by the component reflection registry where possible
- Revisit if the LuaLS annotations prove too weak for type safety in practice, or if sol2 stops building on supported compilers
