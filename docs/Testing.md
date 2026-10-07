# Testing & CI

## Tests
- Lots of unit tests with doctest, run on every change
- Set up a scene used for testing, which tests every single feature in the engine - all components, and the entire scripting API. Extend it as each feature lands
- Rendering tests: render test scenes offscreen and compare the result against reference images with a tolerance. Reference images are stored with Git LFS, and are only updated deliberately, with the reason stated in the commit message
- Every MCP tool has automated tests (see [AI Control](AIControl.md))
- Animation: clip sampling and blending tests, plus reference-image tests of skinned meshes (see [Animation](Features/Animation.md))
- Networking: server and multiple clients running over loopback, including under simulated latency, packet loss and reordering, plus fuzz tests with malformed packets (see [Networking](Features/Networking.md))

## CI
- GitHub Actions CI on every push, building and running all tests in Debug and Release on:
  - Windows (MSVC)
  - macOS (Apple Clang, MoltenVK)
  - Ubuntu 24.04 (GCC 14 and Clang 18)
- Where no GPU is available, rendering tests run on Mesa lavapipe (software Vulkan)
- CI must stay green - a failing build gets fixed before any other work
