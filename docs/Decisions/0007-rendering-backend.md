# 0007 - Rendering Backend: NVRHI on Vulkan 1.3

## Status
Accepted

## Context
Milestone 2 brings up the window, device input and the rendering backend. [Tech Stack & Build](../TechStack.md#libraries) names GLFW and NVRHI, with Vulkan on every platform (MoltenVK on macOS). Bringing them up left several choices open: which Vulkan version to require, how Vulkan's functions are loaded, who owns the swapchain, and how API misuse is caught.

## Decision
- **Vulkan only** - NVRHI is built with its Vulkan backend alone; the Direct3D backends are off. One backend on every platform means one set of shaders (SPIR-V), one code path to test, and reference images that mean the same thing everywhere
- **Vulkan 1.3 is the minimum** - NVRHI's Vulkan backend renders with dynamic rendering and synchronization2, and uses timeline semaphores; all three are core in 1.3. A device without them is rejected with a reason that names the missing feature. Optional features (anisotropic filtering, BC textures, multi-draw indirect, buffer device address, ...) are turned on when the device has them. Current drivers on Windows and Linux, MoltenVK and lavapipe all support 1.3
- **Vulkan is loaded at runtime** - through vulkan.hpp's dynamic dispatcher (`VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1`), as NVRHI expects. The engine never links the Vulkan loader, so it starts - and reports a clear error - on machines without a Vulkan driver, and Dist builds have no hard dependency on a system library. GLFW is given the same loader (`glfwInitVulkanLoader`), so windows and the renderer always agree on it
- **A specific driver on request** - `GraphicsDeviceConfig::Driver` names a Vulkan driver library that the engine loads and hands to the loader directly (`VK_LUNARG_direct_driver_loading`), instead of the installed drivers. Rendering tests use it for lavapipe, and the runtime exposes it as `--vulkan-driver`
- **One graphics device at a time** - vulkan.hpp dispatches through one process-wide table, which holds the functions of one instance and device. `GraphicsDevice::Create` fails if a device already exists, rather than letting two devices overwrite each other's functions
- **Headless and windowed devices are the same class** - without presentation extensions the device renders offscreen only, which is what rendering tests, headless mode and the dedicated server need
- **Lodestone owns the swapchain** - `Swapchain` creates the Vulkan swapchain below NVRHI and wraps its images as NVRHI textures. It recreates itself when the window's size changes, skips frames while the window is minimized, limits the CPU to `MaxFramesInFlight` (2) frames ahead of the GPU, and uses one present semaphore per swapchain image (a semaphore can't be reused until the presentation that waits on it is done)
- **Validation in Debug** - Debug builds enable the Khronos validation layer when it's installed (it comes with the Vulkan SDK), and always enable NVRHI's validation layer. Errors are logged and counted, and the rendering tests check the count is zero. Without the Vulkan SDK, a warning says the Vulkan layer is missing
- **Portability** - on MoltenVK, the instance enables `VK_KHR_portability_enumeration` and the device enables `VK_KHR_portability_subset`, as the Vulkan specification requires

## Alternatives
- **NVRHI's Direct3D 12 backend on Windows** - would need DXIL shaders as well as SPIR-V, double the testing, and give reference images that differ between platforms. It can be added later behind the same interfaces if a Windows-only need appears
- **Vulkan 1.2 with extensions** - the same features through `VK_KHR_dynamic_rendering` and `VK_KHR_synchronization2`, for older drivers. Every platform Lodestone supports has 1.3 drivers, so the extra code paths aren't worth it
- **Linking the Vulkan loader** - simpler, but the executable then fails to start without it, instead of reporting an error
- **NVRHI's sample framework (Donut) for the device and swapchain** - a large dependency built for samples, not an engine; the device and swapchain code is small enough to own and test

## Consequences
- Vulkan code below NVRHI (device creation, the swapchain) goes through `Vulkan::Dispatch()`, and only one `GraphicsDevice` exists per process. Tests create and destroy devices one after another
- Running on a GPU or driver without Vulkan 1.3 fails with a clear error. Revisit if a supported platform's drivers fall short
- Exported macOS games will have to bundle the Vulkan loader and MoltenVK (Milestone 12)
