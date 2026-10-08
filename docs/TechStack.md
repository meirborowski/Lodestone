# Tech Stack & Build

## Libraries
- C++23 and CMake for the core engine (see [C++ Standard](#c-standard))
- Dependencies are fetched with CMake `FetchContent`, pinned to exact release tags or commit hashes - no floating branches
- [GLFW](https://github.com/glfw/glfw) and [nvrhi](https://github.com/NVIDIA-RTX/NVRHI), using Vulkan primarily on all platforms (MoltenVK on macOS)
- Shaders written in HLSL and compiled to SPIR-V with DXC, via nvrhi's [ShaderMake](https://github.com/NVIDIA-RTX/ShaderMake)
- [glm](https://github.com/g-truc/glm) for math
- [EnTT](https://github.com/skypjack/entt) for the ECS
- [Dear ImGui](https://github.com/ocornut/imgui) (docking branch) for the editor UI, with [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) for the transform gizmo
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics) for physics
- [miniaudio](https://github.com/mackron/miniaudio) for audio
- [ENet](https://github.com/lsalzman/enet) for networking
- [cgltf](https://github.com/jkuhlmann/cgltf) for glTF import; [stb_image](https://github.com/nothings/stb) for textures and HDRIs; [stb_truetype](https://github.com/nothings/stb) for fonts
- [spdlog](https://github.com/gabime/spdlog) for logging
- [nlohmann/json](https://github.com/nlohmann/json) for scene, prefab, project and asset metadata files (pretty-printed so they diff cleanly in git)
- [doctest](https://github.com/doctest/doctest) for unit tests
- [Lua 5.4](https://www.lua.org/) for scripting, bound with [sol2](https://github.com/ThePhD/sol2) (see [Decision 0001](Decisions/0001-scripting-language.md)). sol2 releases are infrequent - pin a commit that builds cleanly on every supported compiler
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) for the MCP server's HTTP transport
- [efsw](https://github.com/SpartanJ/efsw) for file watching (hot reload)
- [stb_image_write](https://github.com/nothings/stb) for saving rendered images (reference-image tests and screenshots)
- [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended) for the editor's native file dialogs

When adding a dependency, add it to THIRD_PARTY_LICENSES.md. Only permissively licensed dependencies are allowed.

## C++ Standard
- C++23: set `CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_STANDARD_REQUIRED ON` and `CMAKE_CXX_EXTENSIONS OFF`
- Only use C++23 features that every supported compiler below implements. Language features are well supported, but library support varies (especially Apple's libc++) - check the [cppreference C++23 support table](https://en.cppreference.com/w/cpp/compiler_support/23) before using a newer library feature. CI on all platforms is the final check
- No C++26 features
- MSVC: the stable `/std:c++23` switch ships with MSVC Build Tools 14.52. Until it's available, CMake passes `/std:c++latest` on MSVC, which also enables unfinished C++26 features - the GCC and Clang CI builds (strict C++23) catch anything beyond C++23. As of October 2026, 14.52 is still a preview, so CI builds with the newest stable Build Tools (see [Decision 0004](Decisions/0004-msvc-cpp23-switch.md))
- `Tests/Core/LanguageSupportTests.cpp` checks that every C++23 feature the engine uses compiles and works on each compiler. Add a feature there when engine code starts using it
- Useful C++23 features for this codebase: `std::expected` (see [Error Handling](CodeStyle.md#error-handling)), deducing `this` (removes duplicate const/non-const overloads), `std::to_underlying` (for `enum class`), `std::unreachable`, `if consteval`

## Platforms & Compilers
- Windows: MSVC Build Tools 14.50+ (Visual Studio 2026), moving to 14.52+ once it's stable. CI uses the `windows-2025-vs2026` image's MSVC and reports its version (see [Decision 0004](Decisions/0004-msvc-cpp23-switch.md))
- macOS: Apple Clang (Xcode 16.3+). CI uses Xcode 16.4
- Ubuntu 24.04+: GCC 14+ and Clang 19+ (Ubuntu 24.04's default GCC 13 is too old - install the `gcc-14` / `g++-14` packages)
  - Clang 18 can't use `std::expected` with libstdc++: libstdc++ only enables `<expected>` when `__cpp_concepts >= 202002L`, and Clang 18 reports a lower value. Milestone 1's CI confirms that Clang 19 works

## Build Prerequisites
- CMake 3.28+ and Ninja
- The Vulkan SDK (which includes DXC) - from Milestone 2
- Git LFS
- Python 3.12+, for the formatting and clang-tidy scripts in `tools/`
- clang-format and clang-tidy, at the LLVM version CI pins: `pip install -r tools/requirements.txt` (see [Enforcement](CodeStyle.md#enforcement))

Everything else is fetched by CMake. Document the setup steps for each platform in AGENTS.md and the README.

## Dependencies
- Declared in `cmake/Dependencies.cmake` with `ls_declare_dependency()`: an archive URL (a release tag, or a commit for sol2) and its SHA256 hash
- Archives are downloaded once into `.cache/dependencies`, shared by every build tree and cached on CI (see [Decision 0002](Decisions/0002-build-system.md))

## Build Configurations
- **Debug** - no optimization, asserts, Vulkan validation layers enabled
- **Release** - optimized, with debug information, asserts, logging and the editor
- **Dist** - fully optimized (with link-time optimization), no editor code, no asserts or developer logging. Used for exported games. CI builds it from Milestone 1 onward, so code that differs in Dist can't rot unnoticed

Each configuration has a CMake preset (`debug`, `release`, `dist`) that builds into `build/<preset>`, plus `asan` for a Debug build with AddressSanitizer and UndefinedBehaviorSanitizer. Code sees the configuration as one of `LS_CONFIG_DEBUG`, `LS_CONFIG_RELEASE` and `LS_CONFIG_DIST`.
