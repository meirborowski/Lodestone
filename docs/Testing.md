# Testing & CI

## Tests
- Lots of unit tests with doctest, run on every change
- Test scenes that together exercise every feature of the engine - all components, and the entire scripting API: one scene per feature area, plus an integration scene that combines them. Extend them as each feature lands
- Rendering tests: render test scenes offscreen and compare the result against reference images (see [Reference Images](#reference-images))
- Every MCP tool has automated tests (see [AI Control](AIControl.md#testing))
- Animation: clip sampling and blending tests, plus reference-image tests of skinned meshes (see [Animation](Features/Animation.md))
- Networking: server and multiple clients running over loopback, including under simulated latency, packet loss and reordering, plus fuzz tests with malformed packets (see [Networking](Features/Networking.md))
- Fuzz tests for everything that parses untrusted input: network packets, scene, prefab, project and asset metadata files, and the glTF, image, HDRI, audio and font importers. Corrupt input must never crash the engine

## Test Tiers
Tests carry CTest labels, so the right set runs at the right time:
- `unit` - fast, with no GPU or network; run on every change
- `render` - reference-image tests
- `integration` - MCP end-to-end, networking over loopback, and export
- `fuzz` - fuzz tests, run for a fixed time budget

Run the `unit` tier on every change, and every tier before opening a pull request. CI runs every tier.

## Test Integrity
Never delete, skip or weaken a test, loosen a tolerance, or regenerate a reference image just to get a build green. If a test really is wrong, fix it and explain why in the commit message.

## Reference Images
- Stored with Git LFS, and only updated deliberately, with the reason stated in the commit message
- Generated on lavapipe (Mesa's software Vulkan driver) at a pinned Mesa version - fetched or built at that exact version and cached, never a distro package that changes underneath us
- Rendering tests always compare on lavapipe, locally as well as on CI, so results don't depend on the GPU or driver
- Comparisons use a tolerance. On failure, the test writes the rendered image and a diff image, and CI uploads them as artifacts
- macOS follows whatever Milestone 2's smoke check establishes - lavapipe, or skipped with a logged reason - as recorded in [Decisions](Decisions/README.md)

## Performance
- The 60 fps target in [3D Renderer](Features/Renderer.md#performance) can't be checked on CI, which renders in software. It's measured locally with a benchmark scene that enables every renderer feature, against a baseline recorded in the repo
- CI tracks the proxies that don't need a GPU on the benchmark scene: draw call counts (gated - any increase over the baseline fails) and CPU frame time (reported but not gated, because CI machines are too noisy)

## CI
- GitHub Actions CI on every pull request and every push to main, building and running all tests in Debug and Release, and building Dist (so code that differs in Dist can't rot unnoticed), on:
  - Windows (MSVC)
  - macOS (Apple Clang, MoltenVK)
  - Ubuntu 24.04 (GCC 14 and Clang 19)
- Once the runtime exists, CI also smoke-tests the Dist runtime headlessly
- Extra jobs: clang-format check, clang-tidy, AddressSanitizer + UndefinedBehaviorSanitizer (Clang on Linux), and ThreadSanitizer as soon as the engine uses more than one thread
- Where no GPU is available, rendering tests run on lavapipe (see [Reference Images](#reference-images))
- On macOS, a smoke step checks that MoltenVK loads and a Vulkan device is found, and fails with a clear message otherwise
- Caching keeps CI fast and within GitHub's quotas: ccache or sccache for compiles, the FetchContent dependencies, and Git LFS objects (LFS bandwidth is metered, and every CI checkout counts)
- main is protected: changes merge through pull requests, only after CI passes
- CI must stay green - a failing build gets fixed before any other work
