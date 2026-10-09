# 0010 - Component Reflection: A Small Custom Registry

## Status
Accepted

## Context
Serialization, the inspector, MCP tools, script bindings and network replication all need to know each component's fields: names, types, defaults, limits and flags (see [Components and Reflection](../Architecture.md#components-and-reflection)). [Architecture](../Architecture.md) left open whether the registry builds on `entt::meta` or is a small custom one, to be decided in Milestone 3.

## Decision
- **A custom registry** - `ComponentRegistry` (`Lodestone/Reflection/ComponentRegistry.h`) holds a `ComponentType` per component, each with a list of `FieldInfo`. Components register once, at startup, by name: `registry.Register<TransformComponent>("Transform", ...).Field("Position", &TransformComponent::Position, {...})`
- **A closed set of field types** - `FieldType` and `FieldValue` (a `std::variant`) cover bool, 32-bit signed and unsigned integers, float, 2/3/4-component vectors, quaternions, strings and UUIDs. Every consumer handles exactly these, so a component made of them gets every feature from its registration alone. A type is added to the set when a component needs it, and every consumer is updated together
- **Validation in the registry** - setting a field checks its type, that floats are finite, that quaternions aren't zero, and the field's limits (`Min`/`Max`). Files, MCP and scripts all go through it, so no path can write an invalid value
- **Flags** - fields can be `ReadOnly` (to editing tools; loading still sets them) and `Replicated`. Components can be `Required` (every entity has one; tools can't add or remove it) or `Internal` (engine bookkeeping such as interpolation state: part of simulation snapshots, never saved or exposed)
- **Type-erased operations** - each `ComponentType` can add, find, remove and patch its component in an `entt::registry`, and write and read it in simulation snapshots, without callers knowing the C++ type. Changes made through the registry go through EnTT's `patch`, so observers (replication, the editor) see them
- **One engine registry** - `GetEngineComponentRegistry()` has the core components (ID, name, transform, hierarchy, and the internal previous transform); engine modules register theirs at startup. Scenes take a registry, so tests can use their own

## Alternatives
- **`entt::meta`** - general-purpose runtime reflection, but it knows nothing of limits, flags or validation, so those would be layered on top anyway, through its property system. Its API also changes between major versions (EnTT 4.0 reworked much of it), which would ripple through every consumer
- **Code generation from annotated headers** - no registration code to write, but a build step and a parser to maintain, and harder to debug

## Consequences
- Adding a component means writing its struct and registering it (see AGENTS.md). Custom handling is added where the generic path isn't enough, as the scene loader does for the hierarchy
- Components must be default-constructible and copyable, and their reflected fields must be of the field types. Data that isn't reflected isn't saved, edited or replicated - but it is part of simulation snapshots, which copy whole components
- Component and field names appear in files, MCP and scripts, so renaming one is a file format change with a migration
