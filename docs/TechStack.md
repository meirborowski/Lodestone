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
- Lua for scripting, bound with [sol2](https://github.com/ThePhD/sol2), though this is up for discussion - if you'd recommend something else, propose it with your reasoning before starting

When adding a dependency, add it to THIRD_PARTY_LICENSES.md. Only permissively licensed dependencies are allowed.

## C++ Standard
- C++23: set `CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_STANDARD_REQUIRED ON` and `CMAKE_CXX_EXTENSIONS OFF`
- Only use C++23 features that every supported compiler below implements. Language features are well supported, but library support varies (especially Apple's libc++) - check the [cppreference C++23 support table](https://en.cppreference.com/w/cpp/compiler_support/23) before using a newer library feature. CI on all platforms is the final check
- No C++26 features
- MSVC: the stable `/std:c++23` switch ships with MSVC Build Tools 14.52. Until it's available, CMake passes `/std:c++latest` on MSVC, which also enables unfinished C++26 features - the GCC and Clang CI builds (strict C++23) catch anything beyond C++23
- Useful C++23 features for this codebase: `std::expected` (see [Error Handling](CodeStyle.md#error-handling)), deducing `this` (removes duplicate const/non-const overloads), `std::to_underlying` (for `enum class`), `std::unreachable`, `if consteval`

## Platforms & Compilers
- Windows: MSVC Build Tools 14.52+ (Visual Studio 2026)
- macOS: Apple Clang (Xcode 16.3+)
- Ubuntu 24.04+: GCC 14+ and Clang 18+ (Ubuntu 24.04's default GCC 13 is too old - install the `gcc-14` / `g++-14` packages)

## Build Prerequisites
- CMake 3.28+
- The Vulkan SDK (which includes DXC)
- Git LFS

Everything else is fetched by CMake. Document the setup steps for each platform in AGENTS.md and the README.

## Build Configurations
- **Debug** - no optimization, asserts, Vulkan validation layers enabled
- **Release** - optimized, with asserts, logging and the editor
- **Dist** - fully optimized, no editor code, no asserts or developer logging. Used for exported games
