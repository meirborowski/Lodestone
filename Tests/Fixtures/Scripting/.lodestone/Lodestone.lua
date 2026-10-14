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

------------------------------------------------------------------------------------------------------------------------
-- Generated from the engine's registries
------------------------------------------------------------------------------------------------------------------------

---The entity's stable identity, which files and the network refer to it by
---@class IDComponent : Component
---@field ID string? Unique and permanent (read-only)

---The entity's display name
---@class NameComponent : Component
---@field Name string Doesn't need to be unique

---Position, rotation and scale relative to the parent entity
---@class TransformComponent : Component
---@field Position vec3 In meters, relative to the parent
---@field Rotation quat A unit quaternion, relative to the parent
---@field Scale vec3 Relative to the parent

---The entity's place in the scene hierarchy
---@class HierarchyComponent : Component
---@field Parent string? The parent entity, or nil for a root entity (read-only)

---Marks the root of a prefab instance with the prefab it was copied from
---@class PrefabInstanceComponent : Component
---@field Prefab string? The prefab asset's UUID (read-only)

---Draws a mesh of a model asset
---@class MeshRendererComponent : Component
---@field Model string? The model asset's UUID
---@field Mesh integer Which of the model's meshes, from 0
---@field CastShadows boolean Whether the mesh casts shadows

---A camera the game renders from, looking along -Z
---@class CameraComponent : Component
---@field FieldOfView number Vertical, in degrees
---@field NearPlane number Nothing closer is drawn, in meters
---@field FarPlane number Nothing farther is drawn, in meters
---@field Primary boolean The camera the game renders from, when the scene has several

---Light from far away, like the sun, shining along the entity's -Z
---@class DirectionalLightComponent : Component
---@field Color vec3 Linear RGB
---@field Intensity number Brightness
---@field CastShadows boolean Whether it casts shadows

---Light from a point in every direction
---@class PointLightComponent : Component
---@field Color vec3 Linear RGB
---@field Intensity number Brightness
---@field Range number The light fades to nothing at this distance, in meters

---Light from a point, in a cone along the entity's -Z
---@class SpotLightComponent : Component
---@field Color vec3 Linear RGB
---@field Intensity number Brightness
---@field Range number The light fades to nothing at this distance, in meters
---@field InnerAngle number Full brightness within this angle of the axis, in degrees
---@field OuterAngle number No light beyond this angle from the axis, in degrees

---Image-based lighting and the sky, from an HDR environment map. One per scene
---@class EnvironmentComponent : Component
---@field Environment string? The environment asset's UUID (an .hdr file); nil for a plain grey sky
---@field Intensity number Brightness of the lighting and the sky
---@field Rotation number Turns the environment around the vertical axis, in degrees
---@field ShowSky boolean Whether the environment is drawn behind everything

---Exposure, tonemapping, ambient occlusion and shadows. One per scene
---@class RenderSettingsComponent : Component
---@field Exposure number In stops: +1 doubles the brightness
---@field Tonemapper integer 0: ACES filmic, 1: Khronos PBR Neutral, 2: none (clamped)
---@field AmbientOcclusion boolean Darkens creases and corners (SSAO)
---@field AmbientOcclusionRadius number How far occlusion reaches, in meters
---@field AmbientOcclusionIntensity number How dark occlusion gets
---@field ShadowDistance number Shadows are drawn up to this far from the camera, in meters
---@field ShadowSoftness number The light's size: larger makes shadows softer away from their casters

---The root of entities placed from a model: the entities below it with a ModelNode are its nodes
---@class ModelInstanceComponent : Component
---@field Model string? The model asset's UUID

---Which node of its model instance's model the entity is
---@class ModelNodeComponent : Component
---@field Node integer Into the model's nodes, from 0

---Skins the entity's mesh with a skin of its model, whose joints are the instance's nodes
---@class SkinComponent : Component
---@field Skin integer Into the model's skins, from 0

---Plays the animation clips of the model instance it's on, with crossfades between them
---@class AnimatorComponent : Component
---@field Clip integer The playing clip, into the model's animations from 0
---@field Time number How far into the clip, in seconds
---@field Speed number How fast time advances: 1 is normal, negative plays backwards
---@field Playing boolean Whether time advances
---@field Loop boolean Whether the clip starts over when it ends; otherwise it stops there
---@field FadeClip integer The clip fading out during a crossfade (read-only)
---@field FadeTime number How far into the clip fading out, in seconds (read-only)
---@field FadeDuration number How long the crossfade lasts, in seconds: 0 when there's none (read-only)
---@field FadeElapsed number How far into the crossfade, in seconds (read-only)

---Makes the entity a body in the physics world, shaped by the colliders on it and its descendants
---@class RigidBodyComponent : Component
---@field Type integer 0: static, 1: kinematic (follows its transform), 2: dynamic (simulated)
---@field Mass number In kilograms, for dynamic bodies
---@field Friction number How much it resists sliding along surfaces: 0 is ice
---@field Restitution number How much speed it keeps after a hit: 0 doesn't bounce, 1 bounces back fully
---@field LinearDamping number How quickly its movement slows down by itself
---@field AngularDamping number How quickly its spinning slows down by itself
---@field GravityFactor number How much gravity pulls it: 0 floats, 1 falls normally
---@field IsTrigger boolean Detects bodies and characters overlapping it instead of colliding
---@field ContinuousCollision boolean Sweeps its motion so it can't pass through thin objects when fast. Costs more
---@field AllowSleeping boolean Lets it stop simulating at rest, until something disturbs it
---@field LinearVelocity vec3 In meters per second, in world space
---@field AngularVelocity vec3 In degrees per second, around each world axis

---A box collider, scaled with the entity
---@class BoxColliderComponent : Component
---@field Size vec3 Edge lengths along X, Y and Z, in meters
---@field Center vec3 Relative to the entity

---A sphere collider, scaled with the entity
---@class SphereColliderComponent : Component
---@field Radius number In meters
---@field Center vec3 Relative to the entity

---A capsule collider along the entity's Y axis, scaled with the entity
---@class CapsuleColliderComponent : Component
---@field Height number End to end, in meters; under twice the radius makes a sphere
---@field Radius number In meters
---@field Center vec3 Relative to the entity

---A collider from a model's mesh: its triangles, or their convex hull
---@class MeshColliderComponent : Component
---@field Model string? The model asset's UUID
---@field Mesh integer Which of the model's meshes, from 0
---@field Convex boolean Uses the mesh's convex hull: faster, and the only kind dynamic bodies can have

---Moves the entity as a character its player controls: WASD, arrows or left stick walk, mouse or right stick turn, Space or gamepad A jump
---@class CharacterControllerComponent : Component
---@field Player integer Whose input commands drive it
---@field Height number From its feet to the top of its head, in meters
---@field Radius number In meters
---@field MoveSpeed number In meters per second
---@field JumpSpeed number Upward speed of a jump, in meters per second: 0 doesn't jump
---@field AirAcceleration number How quickly the player steers it in the air, in meters per second squared: 0 keeps its momentum
---@field MaxSlope number The steepest slope it walks up, in degrees
---@field StepHeight number The tallest step it walks up, in meters
---@field MouseTurnSpeed number Degrees turned per pixel of mouse movement: 0 ignores the mouse
---@field StickTurnSpeed number Degrees turned per second with the right stick all the way over
---@field Mass number In kilograms, for pushing dynamic bodies
---@field PushStrength number The most force it pushes dynamic bodies with, in newtons
---@field Velocity vec3 In meters per second, in world space
---@field Grounded boolean Whether it stands on ground it can walk on (read-only)

---The physics world's settings. One per scene
---@class PhysicsSettingsComponent : Component
---@field Gravity vec3 In meters per second squared

---Runs a Lua script on the entity
---@class ScriptComponent : Component
---@field Script string? The script asset's UUID (a .lua file)
---@field Properties string Values for the script's properties, overriding its defaults: a JSON object, such as {"Speed": 6}

---The entity's component, or nil if it has none
---@overload fun(self: Entity, component: "Animator"): AnimatorComponent?
---@overload fun(self: Entity, component: "BoxCollider"): BoxColliderComponent?
---@overload fun(self: Entity, component: "Camera"): CameraComponent?
---@overload fun(self: Entity, component: "CapsuleCollider"): CapsuleColliderComponent?
---@overload fun(self: Entity, component: "CharacterController"): CharacterControllerComponent?
---@overload fun(self: Entity, component: "DirectionalLight"): DirectionalLightComponent?
---@overload fun(self: Entity, component: "Environment"): EnvironmentComponent?
---@overload fun(self: Entity, component: "Hierarchy"): HierarchyComponent?
---@overload fun(self: Entity, component: "ID"): IDComponent?
---@overload fun(self: Entity, component: "MeshCollider"): MeshColliderComponent?
---@overload fun(self: Entity, component: "MeshRenderer"): MeshRendererComponent?
---@overload fun(self: Entity, component: "ModelInstance"): ModelInstanceComponent?
---@overload fun(self: Entity, component: "ModelNode"): ModelNodeComponent?
---@overload fun(self: Entity, component: "Name"): NameComponent?
---@overload fun(self: Entity, component: "PhysicsSettings"): PhysicsSettingsComponent?
---@overload fun(self: Entity, component: "PointLight"): PointLightComponent?
---@overload fun(self: Entity, component: "PrefabInstance"): PrefabInstanceComponent?
---@overload fun(self: Entity, component: "RenderSettings"): RenderSettingsComponent?
---@overload fun(self: Entity, component: "RigidBody"): RigidBodyComponent?
---@overload fun(self: Entity, component: "Script"): ScriptComponent?
---@overload fun(self: Entity, component: "Skin"): SkinComponent?
---@overload fun(self: Entity, component: "SphereCollider"): SphereColliderComponent?
---@overload fun(self: Entity, component: "SpotLight"): SpotLightComponent?
---@overload fun(self: Entity, component: "Transform"): TransformComponent?
---@param component ComponentName
---@return Component?
function Entity:Get(component) end

---Adds a component, optionally with values for its fields. Every field is checked first: a bad one adds nothing
---@overload fun(self: Entity, component: "Animator", fields?: table<string, any>): AnimatorComponent
---@overload fun(self: Entity, component: "BoxCollider", fields?: table<string, any>): BoxColliderComponent
---@overload fun(self: Entity, component: "Camera", fields?: table<string, any>): CameraComponent
---@overload fun(self: Entity, component: "CapsuleCollider", fields?: table<string, any>): CapsuleColliderComponent
---@overload fun(self: Entity, component: "CharacterController", fields?: table<string, any>): CharacterControllerComponent
---@overload fun(self: Entity, component: "DirectionalLight", fields?: table<string, any>): DirectionalLightComponent
---@overload fun(self: Entity, component: "Environment", fields?: table<string, any>): EnvironmentComponent
---@overload fun(self: Entity, component: "Hierarchy", fields?: table<string, any>): HierarchyComponent
---@overload fun(self: Entity, component: "ID", fields?: table<string, any>): IDComponent
---@overload fun(self: Entity, component: "MeshCollider", fields?: table<string, any>): MeshColliderComponent
---@overload fun(self: Entity, component: "MeshRenderer", fields?: table<string, any>): MeshRendererComponent
---@overload fun(self: Entity, component: "ModelInstance", fields?: table<string, any>): ModelInstanceComponent
---@overload fun(self: Entity, component: "ModelNode", fields?: table<string, any>): ModelNodeComponent
---@overload fun(self: Entity, component: "Name", fields?: table<string, any>): NameComponent
---@overload fun(self: Entity, component: "PhysicsSettings", fields?: table<string, any>): PhysicsSettingsComponent
---@overload fun(self: Entity, component: "PointLight", fields?: table<string, any>): PointLightComponent
---@overload fun(self: Entity, component: "PrefabInstance", fields?: table<string, any>): PrefabInstanceComponent
---@overload fun(self: Entity, component: "RenderSettings", fields?: table<string, any>): RenderSettingsComponent
---@overload fun(self: Entity, component: "RigidBody", fields?: table<string, any>): RigidBodyComponent
---@overload fun(self: Entity, component: "Script", fields?: table<string, any>): ScriptComponent
---@overload fun(self: Entity, component: "Skin", fields?: table<string, any>): SkinComponent
---@overload fun(self: Entity, component: "SphereCollider", fields?: table<string, any>): SphereColliderComponent
---@overload fun(self: Entity, component: "SpotLight", fields?: table<string, any>): SpotLightComponent
---@overload fun(self: Entity, component: "Transform", fields?: table<string, any>): TransformComponent
---@param component ComponentName
---@param fields? table<string, any>
---@return Component
function Entity:Add(component, fields) end

---A component's name
---@alias ComponentName
---| "Animator"
---| "BoxCollider"
---| "Camera"
---| "CapsuleCollider"
---| "CharacterController"
---| "DirectionalLight"
---| "Environment"
---| "Hierarchy"
---| "ID"
---| "MeshCollider"
---| "MeshRenderer"
---| "ModelInstance"
---| "ModelNode"
---| "Name"
---| "PhysicsSettings"
---| "PointLight"
---| "PrefabInstance"
---| "RenderSettings"
---| "RigidBody"
---| "Script"
---| "Skin"
---| "SphereCollider"
---| "SpotLight"
---| "Transform"

---A key's name
---@alias KeyName
---| "A"
---| "Apostrophe"
---| "B"
---| "Backslash"
---| "Backspace"
---| "C"
---| "CapsLock"
---| "Comma"
---| "D"
---| "D0"
---| "D1"
---| "D2"
---| "D3"
---| "D4"
---| "D5"
---| "D6"
---| "D7"
---| "D8"
---| "D9"
---| "Delete"
---| "Down"
---| "E"
---| "End"
---| "Enter"
---| "Equal"
---| "Escape"
---| "F"
---| "F1"
---| "F10"
---| "F11"
---| "F12"
---| "F13"
---| "F14"
---| "F15"
---| "F16"
---| "F17"
---| "F18"
---| "F19"
---| "F2"
---| "F20"
---| "F21"
---| "F22"
---| "F23"
---| "F24"
---| "F25"
---| "F3"
---| "F4"
---| "F5"
---| "F6"
---| "F7"
---| "F8"
---| "F9"
---| "G"
---| "GraveAccent"
---| "H"
---| "Home"
---| "I"
---| "Insert"
---| "J"
---| "K"
---| "Keypad0"
---| "Keypad1"
---| "Keypad2"
---| "Keypad3"
---| "Keypad4"
---| "Keypad5"
---| "Keypad6"
---| "Keypad7"
---| "Keypad8"
---| "Keypad9"
---| "KeypadAdd"
---| "KeypadDecimal"
---| "KeypadDivide"
---| "KeypadEnter"
---| "KeypadEqual"
---| "KeypadMultiply"
---| "KeypadSubtract"
---| "L"
---| "Left"
---| "LeftAlt"
---| "LeftBracket"
---| "LeftControl"
---| "LeftShift"
---| "LeftSuper"
---| "M"
---| "Menu"
---| "Minus"
---| "N"
---| "NumLock"
---| "O"
---| "P"
---| "PageDown"
---| "PageUp"
---| "Pause"
---| "Period"
---| "PrintScreen"
---| "Q"
---| "R"
---| "Right"
---| "RightAlt"
---| "RightBracket"
---| "RightControl"
---| "RightShift"
---| "RightSuper"
---| "S"
---| "ScrollLock"
---| "Semicolon"
---| "Slash"
---| "Space"
---| "T"
---| "Tab"
---| "U"
---| "Up"
---| "V"
---| "W"
---| "World1"
---| "World2"
---| "X"
---| "Y"
---| "Z"

---A mouse button's name
---@alias MouseButtonName
---| "Button4"
---| "Button5"
---| "Button6"
---| "Button7"
---| "Button8"
---| "Left"
---| "Middle"
---| "Right"

---A gamepad button's name
---@alias GamepadButtonName
---| "A"
---| "B"
---| "Back"
---| "DpadDown"
---| "DpadLeft"
---| "DpadRight"
---| "DpadUp"
---| "Guide"
---| "LeftBumper"
---| "LeftThumb"
---| "RightBumper"
---| "RightThumb"
---| "Start"
---| "X"
---| "Y"

---A gamepad axis's name
---@alias GamepadAxisName
---| "LeftTrigger"
---| "LeftX"
---| "LeftY"
---| "RightTrigger"
---| "RightX"
---| "RightY"

