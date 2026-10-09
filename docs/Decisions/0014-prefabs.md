# 0014 - Prefabs: Saved Entity Trees, Instanced as Independent Copies

## Status
Accepted

## Context
Milestone 4 adds prefabs: create one from an entity, edit it, and instance it into scenes (and, from Milestone 8, spawn it from scripts). Duplicating an entity, and undoing a delete, need the same thing - an entity and its descendants as data that can be copied back into a scene - so one mechanism should serve all of them. Instancing must never half-succeed, and references between a copy's entities must point within the copy.

## Decision
- **Entity lists** - `EntitySerializer` turns an entity and its descendants into the same JSON entity list scene files hold (every non-internal component, parents before children), with the tree's root saved as a root entity. `InstantiateTree()` copies a list into a scene under any parent, either keeping the UUIDs (undoing a delete) or giving every entity a new one and remapping the UUID fields that refer to entities in the tree (duplicates and prefab instances). The list is read into a scratch scene first, so the target scene changes only when all of it is valid
- **Prefab files** (`.lprefab`) - `{"format": "Lodestone.Prefab", "version": 1, "entities": [...]}`, versioned and migrated like every other file format (see [Decision 0011](0011-file-formats.md)), and checked as strictly as scenes when loaded: a prefab is instanced into a scratch scene as part of loading, so a prefab that loads always instances
- **Instances are independent copies** - an instance is a copy of the prefab's entities with new UUIDs; its root gets a `PrefabInstance` component that records the prefab asset's UUID. Editing a prefab (opened as a document of its own, whose root is its only root entity) changes future instances, not existing ones
- **Commands** - instancing, duplicating and deleting are commands, so they undo; redoing recreates exactly the same entities with the same UUIDs, because each command keeps the tree it created (see [Decision 0015](0015-editor-commands.md))

## Alternatives
- **Linked instances with overrides** (Unity's and Godot's model) - instances follow their prefab, and keep per-field overrides. Powerful, but it needs override tracking, merge rules, nested prefab resolution and change propagation, and every one of those is a source of subtle bugs. The `PrefabInstance` component records the link, so this can be added later without changing the file format of instances
- **Copying through snapshots** (EnTT's snapshot of the whole registry) - restores handles too, but can't copy part of a scene, remap UUIDs or place a copy under a new parent

## Consequences
- Changing a prefab doesn't update its instances. "Revert to prefab" and "update instances" can be built on `PrefabInstance` when needed
- A prefab's entities may refer to entities outside the prefab by UUID; those references are kept as they are, and point nowhere in scenes without those entities
