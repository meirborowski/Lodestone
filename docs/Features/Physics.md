# 3D Physics

- Jolt Physics, stepped once per simulation tick (see [Simulation](../Architecture.md#simulation))
- Rigid bodies (static, dynamic, kinematic), colliders (box, sphere, capsule, convex hull, mesh), triggers, and raycasts
- Character controller component (Jolt `CharacterVirtual`), driven by input commands - this is what client-side prediction runs (see [Prediction](Networking.md#prediction))
- Physics state is part of the simulation's save/restore, so ticks can be rolled back and re-simulated
- Controllable via scripting, authored via components in the editor
- Collision and trigger events are delivered to scripts
