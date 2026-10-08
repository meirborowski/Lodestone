# 0004 - MSVC: /std:c++latest Until Build Tools 14.52 Is Stable

## Status
Accepted

## Context
[Tech Stack & Build](../TechStack.md#c-standard) plans on MSVC Build Tools 14.52, which ships the stable `/std:c++23` switch, and asks CI to install the Build Tools if GitHub's Windows image lacks them. When Milestone 1 was built (October 2026):
- 14.52 was still a preview release. The newest stable Build Tools were 14.51 (compiler 19.51), which offer C++23 only through `/std:c++23preview` or `/std:c++latest`
- GitHub's `windows-2025-vs2026` image has Visual Studio 2026 with the latest stable Build Tools
- CMake passes `/std:c++latest` to MSVC for `CMAKE_CXX_STANDARD 23`

## Decision
- MSVC builds use CMake's standard handling of `CMAKE_CXX_STANDARD 23`, which is `/std:c++latest` until CMake and MSVC support `/std:c++23`. That also enables unfinished C++26 features, but the GCC and Clang builds compile in strict C++23 mode and catch any use of them
- The minimum supported MSVC is the compiler of Build Tools 14.50 (Visual Studio 2026)
- CI uses the image's MSVC rather than installing preview Build Tools. It reports the compiler version, and adds a notice to the run while it's older than 14.52, so the switch is noticed when 14.52 reaches the image

## Alternatives
- **Installing the 14.52 preview on CI** - preview compilers aren't production toolchains, and installing them costs several minutes per Windows job
- **`/std:c++23preview`** - stricter than `/std:c++latest`, but CMake doesn't use it, so it would mean overriding CMake's standard flags for MSVC. The strict GCC and Clang builds already keep the code within C++23

## Consequences
- Revisit when Build Tools 14.52 is stable: check that CMake passes `/std:c++23`, and raise the minimum MSVC version to 14.52 if needed
