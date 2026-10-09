# 0011 - Scene and Asset File Formats

## Status
Accepted

## Context
Scenes, prefabs, the project file and asset metadata are JSON (see [Tech Stack & Build](../TechStack.md#libraries)), versioned and migrated step by step (see [File Formats](../Architecture.md#file-formats)), and assets are referred to by UUID (see [Asset System](../Architecture.md#asset-system)). They live in version control, are edited by people and agents, and may come from untrusted projects, so they must diff cleanly and load safely.

## Decision
- **Documents start with their format and version** - `{"format": "Lodestone.Scene", "version": 1, ...}`. `UpgradeDocument()` checks the format name, rejects versions newer than the engine reads (`UnsupportedVersion`), and runs migrations one version at a time. Each migration gets a test against a fixture file in the old version, in `Tests/Fixtures`, and every current version keeps a fixture too, so it stays loadable
- **Strict loading** - an unknown member, component or field, a value of the wrong type or out of range, a missing or duplicate UUID, a missing parent or a cycle fails the whole load, with a message naming where (`entities[3].components.Transform.Position`). A load never leaves a partly loaded scene behind. Fields that are missing keep their defaults, so files stay small and adding a field needs no migration
- **Bounded parsing** - documents are limited in size (64 MB) and nesting depth (64), checked before parsing, so hostile files can't exhaust memory or the stack. nlohmann/json's exceptions are caught at the boundary and returned as errors
- **Clean diffs** - two-space indentation, members in a fixed order (format and version first, components and their fields in the order they're registered), entities listed parents first in hierarchy order, and floats in the shortest form that reads back to the same float (`0.1`, not `0.10000000149011612`). Writes go to a temporary file that replaces the target, so a crash never leaves a half-written file
- **Identity** - entities and assets are identified by random (version 4) 128-bit UUIDs in their canonical text form. New entities get theirs from their scene's own generator (see [Decision 0012](0012-simulation-and-rollback.md))
- **Scenes** (`.lscene`) list entities, each with every non-internal component and every field. The hierarchy is saved as each entity's parent; children's order is the order they're listed in
- **Asset metadata** (`<asset>.meta`, next to the asset) holds the asset's UUID, type and importer settings. Asset types come from file extensions. Scanning creates metadata for new assets, gives a copied asset (and its copied metadata) a new UUID, and reports - but never changes - metadata it can't read or whose asset is gone, since that may be someone's unsaved work

## Alternatives
- **Lenient loading** that skips unknown components and fields - more forgiving of hand edits, but it silently drops data, and a typo becomes a lost value instead of an error
- **A binary format** - smaller and faster, but unreadable in diffs and by agents. Exported games pack assets in their own format anyway (Milestone 12)
- **YAML** - nicer to edit by hand, but harder to parse safely, and the docs chose JSON

## Consequences
- Changing a format means bumping its version, adding a migration and a fixture in the old version, and keeping the old fixtures
- Paths inside asset metadata and the registry are UTF-8 with forward slashes on every platform (`PathToUtf8`)
