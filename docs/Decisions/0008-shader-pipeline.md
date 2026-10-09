# 0008 - Shader Pipeline: HLSL to Embedded SPIR-V with DXC and ShaderMake

## Status
Accepted

## Context
Shaders are written in HLSL and compiled to SPIR-V with DXC, via ShaderMake ([Tech Stack & Build](../TechStack.md#libraries)). Milestone 2 decides where DXC comes from on each platform, when shaders compile, and how compiled shaders reach the engine.

## Decision
- **Shaders compile at build time** - `ls_add_shaders()` in `cmake/Shaders.cmake` runs ShaderMake on a target's config file (`Shaders.cfg`, one line per entry point) before the target compiles. ShaderMake only recompiles shaders whose source or includes changed
- **Compiled shaders are embedded** - ShaderMake writes each entry point as a C array in a header (`Shaders/Triangle_VSMain.spirv.h` holds `g_Triangle_VSMain_spirv`), which the code that uses it includes. No shader files ship next to the executable, so exported games can't lose or mismatch them, and there's no loading code to test. Shader hot reload, if it's ever needed, would be an editor feature on top
- **DXC is pinned** - Windows and Linux use a pinned DXC release, fetched like any other dependency. DXC has no macOS release, so macOS uses the DXC in the Vulkan SDK (found through `VULKAN_SDK` or the `PATH`)
- **ShaderMake is built as a separate project** - with `ExternalProject`, in Release and without Lodestone's compiler flags or sanitizers. It's a build tool, not part of the engine
- **Fixed compile options** - shader model 6.5 targeting Vulkan 1.3, warnings as errors, and the register shifts NVRHI's Vulkan backend expects (`t` at 0, `s` at 128, `b` at 256, `u` at 384), so HLSL registers map to the binding slots NVRHI assigns
- **Clip space** - NVRHI flips the viewport, so shaders use the Direct3D convention: +Y is up in clip space, and depth goes from 0 to 1

## Alternatives
- **Compiling shaders at runtime** with the DXC library - needs DXC shipped with every game, and turns shader errors into runtime errors
- **Shader files next to the executable** - another set of files to package, find and version-check, for no benefit while shaders are fixed at build time
- **GLSL with glslang, or Slang** - the docs chose HLSL, which also keeps the option of a Direct3D 12 backend open

## Consequences
- A new shader is added to its target's `Shaders.cfg` and listed in `ls_add_shaders()`'s `SOURCES` and `OUTPUTS`
- Building on macOS needs the Vulkan SDK. Changing DXC's version is a dependency update, and may change rendering - reference images then need checking
- clang-tidy needs the generated headers, so CI builds the shader targets before running it
