# 0009 - Reference Images on lavapipe, and Skipped on macOS

## Status
Accepted

## Context
Reference-image tests must give the same result on every machine (see [Reference Images](../Testing.md#reference-images)). GPUs and drivers rasterize slightly differently, and CI runners have no GPU, so rendering tests use lavapipe, Mesa's software Vulkan driver, at one pinned Mesa version. Milestone 2 decides how lavapipe gets onto each platform, and what macOS does, since Mesa's lavapipe isn't available there.

## Decision
- **One pinned Mesa version** - `tools/lavapipe.env` pins Mesa 26.2.4, with the hashes of its source release and of the prebuilt Windows release. CMake, the build script and CI all read it
- **Windows: the prebuilt release** - CMake downloads [mesa-dist-win](https://github.com/pal1000/mesa-dist-win)'s release of that Mesa version, checks its hash, and extracts only lavapipe into `.cache/lavapipe`
- **Linux: built from source** - `tools/build-lavapipe.sh` builds lavapipe alone (no OpenGL, other drivers or window systems) from the pinned source release, with LLVM linked statically, into `.cache/lavapipe`. The driver manifest uses a relative path, so the build can be restored anywhere. CI builds it once in its own job and caches it, keyed on the pinned version and the script
- **Selected in-process** - the rendering test executable points the Vulkan loader at lavapipe's manifest (`VK_DRIVER_FILES`) before Vulkan starts, so rendering tests use lavapipe however they're run - from CTest, an IDE or the command line - and never the machine's GPU
- **Required on CI** - Windows and Linux CI configure with `LS_REQUIRE_LAVAPIPE=ON`, so a missing lavapipe fails the build instead of skipping tests. Locally, without lavapipe, reference-image tests are skipped with the reason and the command to build it
- **macOS: skipped, with a logged reason** - no reference-image comparison runs on macOS. A skipped test, `Render.Reference-image tests`, shows the reason in every test run. The other rendering tests (device creation, offscreen rendering, readback) run on MoltenVK, which is the macOS smoke check that MoltenVK loads and provides a Vulkan device

## Alternatives
- **lavapipe on macOS** - Mesa can build lavapipe for macOS, but the build isn't routinely tested upstream and would need its own pinned toolchain and maintenance. The images would still be the same Mesa rasterizer as on Windows and Linux, so they would add little coverage for the cost
- **Reference images per platform or GPU** - several sets of images to keep up to date, and differences between them would hide real regressions
- **Comparing on whatever GPU is present** - results would depend on the machine, so failures couldn't be trusted
- **A distribution's Mesa package** - its version changes with the distribution, which would change the images underneath us

## Consequences
- Updating Mesa means changing `tools/lavapipe.env` and regenerating every reference image in the same change, with the reason in the commit message
- macOS rendering is checked by the smoke tests and by running the windowed app, not by image comparison. Revisit if Mesa's macOS lavapipe support becomes reliable, or if rendering bugs specific to MoltenVK appear
- Linux developers need LLVM's development files and Mesa's build tools to build lavapipe once (see AGENTS.md)
