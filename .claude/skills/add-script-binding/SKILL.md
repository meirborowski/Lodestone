---
name: add-script-binding
description: Add to Lodestone's Lua scripting API - a function on a global table (Scene, Input, Physics, Animation, Time, Log), an Entity method, a math type's function, a new global table, or a behavior callback - with its binding, its lua-language-server annotation and its tests. Use whenever scripts need something the API doesn't have. Components need no bindings.
---

# Add a Script Binding

Scripts reach every registered component through reflection (`entity:Get("RigidBody").Mass`), so a component needs no binding - registering it is enough (see the `add-component` skill). Bindings are for everything else. The design is [Decision 0022](../../../docs/Decisions/0022-scripting.md); the API is documented by its annotations and [Scripting](../../../docs/Features/Scripting.md).

## 1. Bind it
In `Source/Core/Lodestone/Scripting/ScriptBindings.cpp`, in the `ScriptApi::Bind...()` function for its area:
```cpp
physics["IsSleeping"] = [this](const ScriptEntity& entity, sol::this_state state)
{
	Resolve(state, entity);
	return RequirePhysics(state).IsSleeping(entity.Id);
};
```
- Take entities as `const ScriptEntity&` and resolve them with `Resolve(state, entity)`, which raises a Lua error for one that's gone; return entities as `ScriptEntity{.Id = ...}`
- Raise errors for bad arguments with `Raise(state, message)`, saying what was wrong and with what (`DescribeEntity()` names an entity). Never assert on script input, and never let a C++ exception carry a script's mistake
- Names are PascalCase, like the engine's; optional arguments are `sol::optional<T>`, and a player number defaults to 0
- What a binding changes must be in components - rollback restores components, not engine state hidden elsewhere. Change components through the registry (`GetRegistry().patch<T>()`), so physics and the other systems see the change
- It must be deterministic: no wall-clock time, no addresses, no unordered iteration over pointers
- A new global table is created in `ScriptApi::Bind()`; add its name to the list in "Every function scripts can call is in the API's annotations" (`Tests/Core/ScriptingApiTests.cpp`). A new callback is called by `ScriptSystem` (see `CallMethod()`), and declared on `Behavior`

## 2. Annotate it
In `Source/Core/Lodestone/Scripting/LuaApi.lua`, beside its area: a description, `---@param` and `---@return` with exact types (`Entity`, `vec3`, `KeyName`, optional `?`), then `function Table.Name(arguments) end` (or `function Entity:Name(...)` for methods). Annotations are what editors complete and check, and what agents read with MCP's `script_api`: write them for someone who can't see the C++.

The generated part (components, `Entity:Get`/`Add` overloads, the name aliases) comes from `GenerateLuaAnnotations()` in `LuaAnnotations.cpp`.

## 3. Test it
- Call it from the test script for its area in `Tests/Fixtures/Scripting/Assets/Scripts` (or a new one, run from `Tests/Core/ScriptingApiTests.cpp`), with `Check.*` checks of its results and of every error a script can cause (`Check.Fails`). Mark a deliberately wrong call for the type-check with `---@diagnostic disable-next-line: <code>`
- Two tests keep the API honest: every annotated function and callback is called by a test script, and every function scripts can reach is annotated
- Update the committed annotations - the test scripts' and the samples' - by running `LodestoneCoreTests` with `LS_UPDATE_LUA_ANNOTATIONS=1`, and review the diff
- The script type-check (`Scripting.*` tests, with lua-language-server) must stay clean

Then follow `build-and-test` and `ship-change`, and update `docs/Features/Scripting.md` if the API's shape changed.
