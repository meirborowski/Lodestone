# 3D Physics

## Features
- Jolt Physics, stepped once per simulation tick (see [Simulation](../Architecture.md#simulation))
- Rigid bodies (static, dynamic, kinematic), colliders (box, sphere, capsule, convex hull, mesh), triggers, and raycasts
- Character controller component (Jolt `CharacterVirtual`), driven by input commands - this is what client-side prediction runs (see [Prediction](Networking.md#prediction))
- Physics state is part of the simulation's save/restore, so ticks can be rolled back and re-simulated
- Controllable via scripting, authored via components in the editor
- Collision and trigger events are delivered to scripts

## Components
- `RigidBody` makes an entity a body: static, kinematic (follows its transform) or dynamic (simulated), with mass, friction, restitution, damping, gravity factor, continuous collision and sleeping. `IsTrigger` makes it detect what overlaps it instead of colliding. Its linear and angular velocity are fields: physics writes them each tick, and setting them changes the velocity
- `BoxCollider`, `SphereCollider`, `CapsuleCollider` and `MeshCollider` (a model's mesh as triangles, or its convex hull) shape the body: the colliders on its entity and on descendants without a body of their own, scaled with their entities. Colliders with no body above them are static bodies of their own
- `CharacterController` is a capsule standing on its entity's position, kept upright, moved by its player's input commands: WASD, the arrow keys or the left stick walk where it faces, the mouse or the right stick turn it, and Space or gamepad A jump; in the air it keeps its momentum, steered at its air acceleration. It walks up steps and slopes up to its limits, pushes dynamic bodies, and is hit by raycasts and detected by triggers
- `PhysicsSettings` sets gravity for the scene

How bodies are made from components, kept in step with them, saved and restored, and how events are found: [Decision 0021](../Decisions/0021-physics.md). The code is in `Source/Core/Lodestone/Physics` (`PhysicsWorld`, a stateful simulation system).

## Testing
- Unit tests for bodies falling and resting, kinematic and static bodies following their entities, compound and scaled colliders, mesh colliders, raycasts, triggers, collision events, the character controller, and component changes taking effect
- Rollback: restoring a saved state and re-simulating the same input commands reproduces the same scene, physics state and events, with bodies spawned, destroyed and changed after the save; and results don't depend on how many threads step the world
- The editor's play mode, raycasts and events, through the MCP tools and the agent smoke test
