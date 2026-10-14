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

How it's built, and why: [Decision 0022](../Decisions/0022-scripting.md). The code is in `Source/Core/Lodestone/Scripting`.

## Behaviors
A script is a `.lua` file in the project's assets that returns a table - a behavior. The `Script` component runs one on an entity: its `Script` field names the script asset, and its `Properties` field, a JSON object, overrides the behavior's defaults:

```lua
---@class Paddle : Behavior
---@field Speed number
local Paddle = {Properties = {Speed = 9, Up = "W", Down = "S"}}

function Paddle:OnUpdate(dt)
	local transform = self.Entity:Get("Transform")
	local position = transform.Position
	if Input.IsKeyDown(self.Up) then
		position.z = position.z - self.Speed * dt
	end
	transform.Position = position
end

return Paddle
```

Each entity gets an instance of its behavior: a table whose metatable is the behavior's, holding `self.Entity`, the properties, and whatever the script stores in it. The callbacks a behavior can have:

| Callback | When |
|---|---|
| `OnCreate()` | The tick its `Script` component is first seen. Every new instance exists before any of their `OnCreate`s run, so they can find each other |
| `OnUpdate(dt)` | Every tick, with its length in seconds, in the order of the `Script` components |
| `OnDestroy()` | When its entity, component or script goes away, and when play mode stops |
| `OnCollisionBegin(other)`, `OnCollisionEnd(other)` | When its body starts and stops touching another |
| `OnTriggerEnter(other)`, `OnTriggerExit(other)` | When a body enters or leaves its trigger, or its body enters or leaves one. Events come after physics steps, so `other` may have been destroyed since: `other:IsValid()` says |
| `OnReload()` | After its script changed and loaded again |

Properties are numbers, booleans, strings, and vectors (arrays of 2 to 4 numbers in JSON). A value of the wrong type, or for a property the behavior doesn't declare, is reported and ignored. Properties changed while the game runs are applied to the instance. One script runs per entity; more behaviors go on child entities. `require("Scripts.Util")` loads `Assets/Scripts/Util.lua` as a module.

## The API
Globals, with their full annotations in every project's `.lodestone/Lodestone.lua`:

| | |
|---|---|
| `Entity` | A handle: `GetName`/`SetName`, `Has`/`Get`/`Add`/`Remove` components by name, `GetParent`/`SetParent`/`GetChildren`/`FindChild`, `GetWorldPosition`/`GetWorldRotation`/`GetForward`, `GetScript` (another entity's instance, to call its functions), `Destroy`, `IsValid` and `GetId` |
| Components | Every registered component, through reflection: `entity:Get("RigidBody").Mass = 2`. Limits and read-only fields are enforced. Vectors and rotations are values: change one and assign it back |
| `Scene` | `Create`, `Get` (by UUID), `Find`/`FindAll` (by name), `FindWith` (by component), `GetRoots`, `Instantiate` (a prefab, by path or UUID, under a parent), `Destroy` |
| `Input` | A player's input this tick (player 0 by default): keys, mouse buttons and gamepad buttons down, pressed and released; the mouse's position, movement and scrolling; gamepad axes |
| `Physics` | `Raycast` and `RaycastAll` (with a maximum distance, triggers, and an entity to ignore), `AddImpulse`, `IsSleeping`. Queries see the physics world as the latest physics step left it - it starts in line with the scene - so what a script creates or moves joins it when physics runs |
| `Animation` | `Play` and `CrossFade` clips by name or index, `Stop`, `Pause`, `Resume`, `SetSpeed`, and the playing clip, its time, and the model's clips. Any node of a model instance finds its animator |
| `Time` | `Delta`, the tick's length; `Tick`, its number; `Elapsed`, the seconds before it |
| `Log` | `Info`, `Warn` and `Error`, to the editor's console and MCP's `log_read`; `print` logs too |
| `vec2`, `vec3`, `vec4`, `quat` | Math: arithmetic, `Length`, `Normalized`, `Dot`, `Cross`, `Distance`, `Lerp`; `quat.FromEuler` (degrees), `AngleAxis`, `LookRotation`, `ToEuler`, `Inverse`, `Slerp` |

Audio, UI and networking join the API in their milestones.

## Errors
An error in a callback is logged with the script, the line and a traceback, and stops only the instance that raised it: it gets no more callbacks until the scripts reload. A script that fails to load is reported the same way, and its entities run nothing. The editor's `script_write` tool compiles a script before writing it, so a syntax error comes back with its line.

## Hot Reload
While play mode runs, the editor checks the files of the scripts it loaded - and the modules they required - twice a second; when one changes, every script loads again. Each instance takes its script's new table, keeps its fields, runs again if an error had stopped it, and gets `OnReload`. A script that fails to load keeps its old version. Writing a script with MCP's `script_write` reloads at once.

## Sandbox
Scripts come from game projects, which may not be trusted, so they run sandboxed:
- The `io` and `debug` libraries aren't loaded; from `os`, only `os.time`, `os.clock` and `os.date` are available
- `require` loads Lua scripts from the project only. Native modules can't be loaded (`package.loadlib` and the C loaders are removed), and `dofile` and `loadfile` are removed
- Precompiled bytecode is rejected (`load` accepts text only) - malicious bytecode can corrupt memory
- A memory limit (enforced through the Lua allocator) and an instruction-count hook stop runaway scripts, which are reported like any other script error

## Determinism
Scripts run in the simulation, so the same state and input must give the same result: Lua's string hashing is seeded the same way every run, and `math.random` starts from the same seed. Tables keyed by strings and numbers iterate in the same order every run; tables keyed by tables or functions follow addresses, so scripts that need an order sort, or use arrays. Script instances aren't part of the simulation's saved state: what must survive a rollback belongs in components.

## Editor Support
Opening or creating a project writes `.lodestone/Lodestone.lua`, the API's annotations for lua-language-server - generated from the bindings and the component registry, so every component has a class with its fields - and, if the project has none, a `.luarc.json` that checks the project's scripts against them as Lua 5.4 with only what the sandbox allows. Editors with lua-language-server (VS Code's Lua extension, Neovim, and others) then complete and check scripts. `Entity:Get("Transform")` and `Entity:Add("Transform")` give `TransformComponent`, and names of components, keys, mouse buttons and gamepad buttons and axes are checked. Agents get the same annotations from MCP's `script_api`.

## Testing
- The runtime: the sandbox, the memory and instruction limits, determinism, the lifecycle, properties, errors and hot reload (`Tests/Core/ScriptSystemTests.cpp`)
- The API, from Lua: the test scripts in `Tests/Fixtures/Scripting` run in a scene with physics and an animated model, each checking an area of the API (`Tests/Core/ScriptingApiTests.cpp`). A test checks that together they call every function and callback the annotations declare, and another that every function scripts can reach is annotated
- The annotations: their content, the files projects get, and that the committed copies (the test scripts' and the samples') match the engine's - `LS_UPDATE_LUA_ANNOTATIONS=1` updates them
- The type check: lua-language-server, fetched by CMake (pinned in `tools/luals.env`), checks the test and sample scripts with `tools/check-lua.py`, which fails on any warning; it's also run on scripts full of marked mistakes, and must find exactly those. Where lua-language-server isn't available, the check is skipped with the reason; CI requires it
- The sample games play from start to finish (`Tests/Editor/SampleGameTests.cpp`)
