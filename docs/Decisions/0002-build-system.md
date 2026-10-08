# 0002 - Build System: Presets, Configurations and Dependencies

## Status
Accepted

## Context
Milestone 1 sets up the build for three platforms and three configurations (see [Tech Stack & Build](../TechStack.md#build-configurations)). The build must:
- Keep editor code out of Dist binaries
- Keep CI fast and within GitHub's quotas, with compiler, dependency and Git LFS caching
- Pin every dependency to an exact version
- Make Dist-only differences (compiled-out asserts and logging) visible to tests, so they can't rot

## Decision
- **Presets** - `CMakePresets.json` defines single-config Ninja presets (`debug`, `release`, `dist`, plus `asan`), each with its own build tree under `build/<preset>`. Workflow presets configure, build and test in one command
- **No editor in Dist** - with a single-config generator, the `LodestoneEditor` target isn't defined in Dist builds at all. Multi-config generators still work: the editor is excluded from Dist with a generator expression, and its sources fail to compile (`#error`) if `LS_CONFIG_DIST` is defined
- **Configurations** - Release is optimized and keeps debug information (for profiling and debugging optimized builds). Dist starts from the Release flags, adds link-time optimization where the toolchain supports it, and has no debug information. The configuration reaches code as exactly one of `LS_CONFIG_DEBUG`, `LS_CONFIG_RELEASE` or `LS_CONFIG_DIST` - not `LS_DEBUG`, which is a log macro
- **Tests in every configuration** - tests are built and run in Debug, Release and Dist. Tests whose behaviour depends on the configuration (asserts, developer logging) check the Dist behaviour in Dist
- **Dependencies** - fetched with `FetchContent` as archives verified against a SHA256 hash, not as Git clones. Archives are downloaded once into `.cache/dependencies`, which every build tree shares and CI caches. Dependency headers are `SYSTEM` includes, so their warnings don't fail the build
- **Compiler caching** - ccache on every CI platform, MSVC included. MSVC embeds debug information in object files (`/Z7`), which ccache needs
- **C++20 module scanning** is turned off - Lodestone doesn't use modules, and the scan slows builds and defeats compiler caches

## Alternatives
- **Ninja Multi-Config presets** - one build tree for all configurations, but a target can't be omitted from a single configuration, so the editor would only be excluded rather than absent
- **Git clones for dependencies** - slower, need network access on every fresh build tree, and a tag can be moved; a hashed archive can't change
- **CPM.cmake** - provides a shared source cache, but is another dependency; `FetchContent`'s `DOWNLOAD_DIR` gives the same result
- **sccache** - an alternative compiler cache; ccache has mature MSVC support and a well-maintained GitHub Action

## Consequences
- A new dependency is added with `ls_declare_dependency()` in `cmake/Dependencies.cmake`, with its archive URL and SHA256 hash, and listed in THIRD_PARTY_LICENSES.md
- Dist builds take longer to link because of link-time optimization. MSVC reports optimizer warning C4702 (unreachable code) for inlined third-party code during link-time code generation, so it's disabled for Dist only
- Revisit link-time optimization if Dist link times become a problem, and the debug information choice when exported games need crash reporting (Milestone 12)
