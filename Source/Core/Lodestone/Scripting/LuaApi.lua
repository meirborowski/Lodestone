---@meta Lodestone

-- The Lodestone scripting API, for lua-language-server (see docs/Features/Scripting.md). The engine writes this file
-- into projects with the component classes, input names and component names generated from its registries appended.
-- Don't edit a project's copy: it's written again when the engine changes.

------------------------------------------------------------------------------------------------------------------------
-- Math
------------------------------------------------------------------------------------------------------------------------

---A 2D vector. Made with vec2() (zero), vec2(s) (every component s) or vec2(x, y); +, -, * and / work per component
---or with a number
---@class vec2
---@field x number
---@field y number
---@operator add(vec2): vec2
---@operator sub(vec2): vec2
---@operator mul(vec2|number): vec2
---@operator div(vec2|number): vec2
---@operator unm: vec2
---@overload fun(): vec2
---@overload fun(value: number): vec2
---@overload fun(x: number, y: number): vec2
vec2 = {}
---@return number
function vec2:Length() end
---The vector scaled to length 1; zero for a zero vector
---@return vec2
function vec2:Normalized() end
---@param other vec2
---@return number
function vec2:Dot(other) end
---@param other vec2
---@return number
function vec2:Distance(other) end
---This vector moved toward another: t = 0 is this one, 1 the other
---@param other vec2
---@param t number
---@return vec2
function vec2:Lerp(other, t) end

---A 3D vector: positions in meters, directions, colors. Made with vec3() (zero), vec3(s) or vec3(x, y, z); +, -, *
---and / work per component or with a number
---@class vec3
---@field x number
---@field y number
---@field z number
---@operator add(vec3): vec3
---@operator sub(vec3): vec3
---@operator mul(vec3|number): vec3
---@operator div(vec3|number): vec3
---@operator unm: vec3
---@overload fun(): vec3
---@overload fun(value: number): vec3
---@overload fun(x: number, y: number, z: number): vec3
vec3 = {}
---@return number
function vec3:Length() end
---The vector scaled to length 1; zero for a zero vector
---@return vec3
function vec3:Normalized() end
---@param other vec3
---@return number
function vec3:Dot(other) end
---@param other vec3
---@return vec3
function vec3:Cross(other) end
---@param other vec3
---@return number
function vec3:Distance(other) end
---This vector moved toward another: t = 0 is this one, 1 the other
---@param other vec3
---@param t number
---@return vec3
function vec3:Lerp(other, t) end

---A 4D vector. Made with vec4() (zero), vec4(s) or vec4(x, y, z, w)
---@class vec4
---@field x number
---@field y number
---@field z number
---@field w number
---@operator add(vec4): vec4
---@operator sub(vec4): vec4
---@operator mul(vec4|number): vec4
---@operator div(vec4|number): vec4
---@operator unm: vec4
---@overload fun(): vec4
---@overload fun(value: number): vec4
---@overload fun(x: number, y: number, z: number, w: number): vec4
vec4 = {}
---@return number
function vec4:Length() end
---@return vec4
function vec4:Normalized() end
---@param other vec4
---@return number
function vec4:Dot(other) end
---@param other vec4
---@return number
function vec4:Distance(other) end
---@param other vec4
---@param t number
---@return vec4
function vec4:Lerp(other, t) end

---A rotation. quat() is no rotation; quat(w, x, y, z) gives the components. a * b rotates by b, then a; q * v rotates
---a vector
---@class quat
---@field x number
---@field y number
---@field z number
---@field w number
---@operator mul(quat): quat
---@operator mul(vec3): vec3
---@overload fun(): quat
---@overload fun(w: number, x: number, y: number, z: number): quat
quat = {}
---A rotation from angles in degrees: around X (pitch), then Y (yaw), then Z (roll), each around the world's axes
---@param degrees vec3
---@return quat
function quat.FromEuler(degrees) end
---A rotation around an axis, in degrees
---@param degrees number
---@param axis vec3
---@return quat
function quat.AngleAxis(degrees, axis) end
---The rotation that turns -Z (forward) to face a direction, with up as close to up as it can be (by default +Y)
---@param forward vec3
---@param up? vec3
---@return quat
function quat.LookRotation(forward, up) end
---The rotation as quat.FromEuler's angles: pitch and roll from -180 to 180 degrees, yaw from -90 to 90. At a yaw of
---90 or -90, pitch and roll turn around the same axis, and the whole turn is given as pitch
---@return vec3
function quat:ToEuler() end
---@return quat
function quat:Inverse() end
---@return quat
function quat:Normalized() end
---This rotation turned toward another: t = 0 is this one, 1 the other
---@param other quat
---@param t number
---@return quat
function quat:Slerp(other, t) end

------------------------------------------------------------------------------------------------------------------------
-- Entities and components
------------------------------------------------------------------------------------------------------------------------

---An entity of the scene. A handle: once the entity is destroyed, using it raises an error (IsValid says)
---@class Entity
Entity = {}
---Whether the entity still exists
---@return boolean
function Entity:IsValid() end
---The entity's UUID, as text
---@return string
function Entity:GetId() end
---@return string
function Entity:GetName() end
---@param name string
function Entity:SetName(name) end
---Whether the entity has a component
---@param component ComponentName
---@return boolean
function Entity:Has(component) end
---Removes a component; whether it had one. The required ones (ID, Name, Transform, Hierarchy) stay
---@param component ComponentName
---@return boolean
function Entity:Remove(component) end
---The parent entity, or nil for a root entity
---@return Entity?
function Entity:GetParent() end
---Moves the entity under a parent, or to the root with nil, keeping its local transform
---@param parent? Entity
function Entity:SetParent(parent) end
---@return Entity[]
function Entity:GetChildren() end
---The first child with a name
---@param name string
---@return Entity?
function Entity:FindChild(name) end
---@return vec3
function Entity:GetWorldPosition() end
---@return quat
function Entity:GetWorldRotation() end
---The direction the entity faces in the world: its -Z
---@return vec3
function Entity:GetForward() end
---The entity's script instance (its self), or nil if it runs no script
---@return Behavior?
function Entity:GetScript() end
---Destroys the entity and its descendants
function Entity:Destroy() end

---One of an entity's components. Its fields are read and written by name, through the engine's reflection, with
---their limits checked: a field that's a vector or a rotation is assigned whole (t.Position = vec3(1, 0, 0)), since
---t.Position.x = 1 would change a copy
---@class Component

------------------------------------------------------------------------------------------------------------------------
-- The scene
------------------------------------------------------------------------------------------------------------------------

---The scene: its entities, found, created and destroyed
---@class Scene
Scene = {}
---Creates an entity, as a root or under a parent
---@param name? string
---@param parent? Entity
---@return Entity
function Scene.Create(name, parent) end
---The entity with a UUID, or nil
---@param id string
---@return Entity?
function Scene.Get(id) end
---The first entity with a name, or nil
---@param name string
---@return Entity?
function Scene.Find(name) end
---Every entity with a name
---@param name string
---@return Entity[]
function Scene.FindAll(name) end
---Every entity with a component
---@param component ComponentName
---@return Entity[]
function Scene.FindWith(component) end
---The root entities, in order
---@return Entity[]
function Scene.GetRoots() end
---Places a copy of a prefab, by its path (such as "Prefabs/Ball.lprefab") or UUID, as a root or under a parent.
---Scripts on it start next tick
---@param prefab string
---@param parent? Entity
---@return Entity
function Scene.Instantiate(prefab, parent) end
---Destroys an entity and its descendants
---@param entity Entity
function Scene.Destroy(entity) end

------------------------------------------------------------------------------------------------------------------------
-- Input: what a player did this tick. Players are numbered from 0, the default
------------------------------------------------------------------------------------------------------------------------

---The players' input this tick, from their input commands
---@class Input
Input = {}
---Whether a key is held at the end of the tick
---@param key KeyName
---@param player? integer
---@return boolean
function Input.IsKeyDown(key, player) end
---Whether a key went down during the tick
---@param key KeyName
---@param player? integer
---@return boolean
function Input.WasKeyPressed(key, player) end
---@param key KeyName
---@param player? integer
---@return boolean
function Input.WasKeyReleased(key, player) end
---@param button MouseButtonName
---@param player? integer
---@return boolean
function Input.IsMouseButtonDown(button, player) end
---@param button MouseButtonName
---@param player? integer
---@return boolean
function Input.WasMouseButtonPressed(button, player) end
---@param button MouseButtonName
---@param player? integer
---@return boolean
function Input.WasMouseButtonReleased(button, player) end
---In window pixels, from the top-left corner
---@param player? integer
---@return vec2
function Input.GetMousePosition(player) end
---How far the mouse moved during the tick, in pixels
---@param player? integer
---@return vec2
function Input.GetMouseDelta(player) end
---@param player? integer
---@return vec2
function Input.GetScrollDelta(player) end
---@param player? integer
---@return boolean
function Input.IsGamepadConnected(player) end
---@param button GamepadButtonName
---@param player? integer
---@return boolean
function Input.IsGamepadButtonDown(button, player) end
---@param button GamepadButtonName
---@param player? integer
---@return boolean
function Input.WasGamepadButtonPressed(button, player) end
---@param button GamepadButtonName
---@param player? integer
---@return boolean
function Input.WasGamepadButtonReleased(button, player) end
---Sticks from -1 to 1 (down and right are positive); triggers from -1 (released) to 1
---@param axis GamepadAxisName
---@param player? integer
---@return number
function Input.GetGamepadAxis(axis, player) end

------------------------------------------------------------------------------------------------------------------------
-- Physics
------------------------------------------------------------------------------------------------------------------------

---@class RaycastHit
---@field Entity Entity The entity whose body was hit
---@field Point vec3 Where, in the world
---@field Normal vec3 The surface's normal there
---@field Distance number How far along the ray, in meters

---@class RaycastOptions
---@field MaxDistance? number In meters, over 0; 1000 by default
---@field IncludeTriggers? boolean Whether triggers can be hit; false by default
---@field Ignore? Entity An entity whose body the ray passes through

---The physics world, as of the latest physics step: what this tick added or moved joins it when physics runs
---@class Physics
Physics = {}
---The closest body along a ray, or nil. The ray starts at a point, and goes in a direction that isn't zero
---@param origin vec3
---@param direction vec3
---@param options? RaycastOptions
---@return RaycastHit?
function Physics.Raycast(origin, direction, options) end
---Every body along a ray, nearest first
---@param origin vec3
---@param direction vec3
---@param options? RaycastOptions
---@return RaycastHit[]
function Physics.RaycastAll(origin, direction, options) end
---Changes a dynamic body's velocity by an impulse, in newton seconds (its mass divides it)
---@param entity Entity
---@param impulse vec3
function Physics.AddImpulse(entity, impulse) end
---Whether a body is at rest and no longer simulated, until something disturbs it
---@param entity Entity
---@return boolean
function Physics.IsSleeping(entity) end

------------------------------------------------------------------------------------------------------------------------
-- Animation: an entity's animator, or its model instance's. Clips are their index in the model, from 0, or their name
------------------------------------------------------------------------------------------------------------------------

---@class AnimationOptions
---@field Loop? boolean Whether the clip starts again when it ends
---@field Speed? number How fast time advances, from -100 to 100: 1 is normal, negative plays backwards

---Animators' clips, on a model instance or any of its nodes
---@class Animation
Animation = {}
---Plays a clip from its start
---@param entity Entity
---@param clip integer|string
---@param options? AnimationOptions
function Animation.Play(entity, clip, options) end
---Fades from the playing clip to another over a time, in seconds
---@param entity Entity
---@param clip integer|string
---@param duration number
function Animation.CrossFade(entity, clip, duration) end
---Stops at the clip's start
---@param entity Entity
function Animation.Stop(entity) end
---@param entity Entity
function Animation.Pause(entity) end
---@param entity Entity
function Animation.Resume(entity) end
---@param entity Entity
---@return boolean
function Animation.IsPlaying(entity) end
---1 is normal speed; negative plays backwards
---@param entity Entity
---@param speed number
function Animation.SetSpeed(entity, speed) end
---The playing clip's index
---@param entity Entity
---@return integer
function Animation.GetClip(entity) end
---How far into the clip, in seconds
---@param entity Entity
---@return number
function Animation.GetTime(entity) end
---@param entity Entity
---@return integer
function Animation.GetClipCount(entity) end
---A clip's name; the playing one's by default
---@param entity Entity
---@param clip? integer
---@return string
function Animation.GetClipName(entity, clip) end

------------------------------------------------------------------------------------------------------------------------
-- Time and the log
------------------------------------------------------------------------------------------------------------------------

---@class TimeTable
---@field Delta number The tick's length, in seconds
---@field Tick integer The tick's number, from 0
---@field Elapsed number Seconds simulated before this tick

---@type TimeTable
Time = {Delta = 0, Tick = 0, Elapsed = 0}

---The log, which the editor's console and MCP's log_read show
---@class Log
Log = {}
---@param ... any
function Log.Info(...) end
---@param ... any
function Log.Warn(...) end
---@param ... any
function Log.Error(...) end

------------------------------------------------------------------------------------------------------------------------
-- Scripts
------------------------------------------------------------------------------------------------------------------------

---What a script returns: a table of callbacks and property defaults, which each entity running the script gets an
---instance of, as self. Declare a script's class with ---@class MyScript : Behavior
---@class Behavior
---@field Entity Entity The entity the instance runs on
---@field Properties? table<string, number|boolean|string|vec2|vec3|vec4> Defaults the Script component can override
---@field OnCreate? fun(self) When the instance starts: the tick its Script component is first seen
---@field OnUpdate? fun(self, dt: number) Every tick, with its length in seconds
---@field OnDestroy? fun(self) When its entity, component or script goes away
---@field OnCollisionBegin? fun(self, other: Entity) When the entity's body starts touching another
---@field OnCollisionEnd? fun(self, other: Entity) When it stops
---@field OnTriggerEnter? fun(self, other: Entity) When a body enters this trigger, or this body enters one
---@field OnTriggerExit? fun(self, other: Entity) When it leaves. The other entity of an event may have been destroyed since the physics step: other:IsValid() says
---@field OnReload? fun(self) After the script's file changed and it loaded again, keeping the instance's fields
