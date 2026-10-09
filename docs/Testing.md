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
- `render` - rendering tests, including reference-image tests. They render on lavapipe, so they need no GPU
- `integration` - MCP end-to-end, networking over loopback, and export
- `fuzz` - fuzz tests, run for a fixed time budget (`LS_FUZZ_SECONDS` per fuzz test, 5 by default)

Run the `unit` tier on every change, and every tier before opening a pull request. CI runs every tier.

- Each test executable is added with `ls_add_test_executable()` in `Tests/CMakeLists.txt`, which registers every doctest test case with CTest under the executable's tier label
- Run a tier with `ctest --preset debug -L unit`, or every tier with `ctest --preset debug`
- Tests are built and run in every configuration, Dist included. Tests of behaviour that differs in Dist (asserts, developer logging) check the Dist behaviour there
- Shared test helpers live in `Tests/Common` (`LodestoneTestSupport`): capturing log output, temporary directories, reference-image comparison, the fuzzer, and `DescribeError()` for assertion messages about `std::expected` results
- Files that tests load - documents in older format versions, scenes, asset metadata - live in `Tests/Fixtures`. Every version of a file format keeps a fixture, and every migration is tested against the fixture of the version it upgrades from
- Fuzz tests (`LodestoneFuzzTests`) feed a loader its corpus in `Tests/Fuzz/Corpus/<format>`, then mutations of it, for the time budget. Each run prints its seed; `LS_FUZZ_SEED=<seed>` replays it. Anything that loads must also save and load again unchanged (see [Decision 0013](Decisions/0013-fuzz-testing.md))
- The configure step checks the layering: `LodestoneCore`, its tests and the server fail to configure if they link GLFW, nvrhi or `LodestoneClient` (`ls_forbid_dependencies()`)

## Test Integrity
Never delete, skip or weaken a test, loosen a tolerance, or regenerate a reference image just to get a build green. If a test really is wrong, fix it and explain why in the commit message.

## Reference Images
- Stored with Git LFS, and only updated deliberately, with the reason stated in the commit message
- Generated on lavapipe (Mesa's software Vulkan driver) at a pinned Mesa version - fetched or built at that exact version and cached, never a distro package that changes underneath us
- Rendering tests always compare on lavapipe, locally as well as on CI, so results don't depend on the GPU or driver
- Comparisons use a tolerance. On failure, the test writes the rendered image and a diff image, and CI uploads them as artifacts
- macOS skips reference-image comparisons, with the reason shown as a skipped test in every run; its other rendering tests run on MoltenVK (see [Decision 0009](Decisions/0009-lavapipe.md))

How it works:
- `tools/lavapipe.env` pins the Mesa version. On Windows, CMake downloads lavapipe; on Linux, build it once with `tools/build-lavapipe.sh`. Both install into `.cache/lavapipe`. Without lavapipe, reference-image tests are skipped with a message saying how to get it; CI configures with `LS_REQUIRE_LAVAPIPE=ON`, so there it's an error instead
- Rendering tests create their graphics devices with `MakeRenderTestDeviceConfig()` (`Tests/Render/RenderTestDevice.h`), which loads lavapipe directly, however the tests are started. Reference-image test cases are in the `ReferenceImages` doctest test suite
- A test renders offscreen, reads the image back (`ReadTexture()`) and calls `CheckReferenceImage("Name", image)` from `Tests/Common/ReferenceImage.h`, which compares it with `Tests/ReferenceImages/Name.png`. The default tolerance allows each channel to differ by 2, and 0.1% of the pixels to differ by more; a test that needs another tolerance passes one, and says why
- On a mismatch, the rendered image and a difference image are written to `<build tree>/RenderOutput`
- To create or deliberately update reference images, run the rendering tests with `LS_UPDATE_REFERENCE_IMAGES=1`, check every changed image by eye, and commit them with the reason (see the `update-reference-images` skill)

## Performance
- The 60 fps target in [3D Renderer](Features/Renderer.md#performance) can't be checked on CI, which renders in software. It's measured locally with a benchmark scene that enables every renderer feature, against a baseline recorded in the repo
- CI tracks the proxies that don't need a GPU on the benchmark scene: draw call counts (gated - any increase over the baseline fails) and CPU frame time (reported but not gated, because CI machines are too noisy)

## CI
- GitHub Actions CI on every pull request and every push to main, building and running all tests in Debug and Release, and building Dist (so code that differs in Dist can't rot unnoticed), on:
  - Windows (MSVC)
  - macOS (Apple Clang, MoltenVK)
  - Ubuntu 24.04 (GCC 14 and Clang 19)
- Once the runtime exists, CI also smoke-tests the Dist runtime headlessly
- Extra jobs: clang-format check, clang-tidy, AddressSanitizer + UndefinedBehaviorSanitizer (Clang on Linux, which also fuzzes longer), and ThreadSanitizer as soon as the engine uses more than one thread
- Rendering tests run on lavapipe on Windows and Linux (see [Reference Images](#reference-images)). The `lavapipe` job builds it for Linux once per Mesa version and caches it
- On macOS, the rendering tests are the smoke check: they run on MoltenVK, and fail with a clear message if it doesn't load or provides no Vulkan device
- When a rendering test fails, CI uploads `RenderOutput` (the rendered and difference images) as an artifact
- Caching keeps CI fast and within GitHub's quotas: ccache for compiles, the FetchContent dependencies (per OS), the lavapipe build, the macOS Vulkan SDK, and Git LFS objects (LFS bandwidth is metered, and every CI checkout counts)
- main is protected and keeps a linear history: changes are squash-merged through pull requests (the only merge method the repository allows), only after CI passes. The `CI passed` job succeeds only when every other job does, so it's the one check branch protection requires
- CI must stay green - a failing build gets fixed before any other work
- The workflow is `.github/workflows/ci.yml`. Actions are pinned to commit hashes
