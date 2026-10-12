# 3D Renderer

## Features
- Import glTF meshes with materials and textures
- Skinned meshes with GPU skinning (see [Animation](Animation.md))
- Editor has a gizmo to position them in the world
- PBR material workflow
- IBL with HDRIs from https://polyhaven.com/hdris (commit a small set of CC0 HDRIs at 1k-2k resolution via Git LFS)
- Soft shadow maps
- Good SSAO
- HDR pipeline with tonemapping

## Performance
- Sustain 60 fps at 1920x1080 on a mid-range GPU (e.g. RTX 3060 class) with all renderer features enabled in the benchmark scene
- CI renders in software, so it can't check this - it's measured with a local benchmark against a recorded baseline (see [Performance](../Testing.md#performance))
- Use the stats overlay and GPU timings to catch regressions

## Using the Renderer
Entities draw and light a scene through components (`Source/Core/Lodestone/Scene/RenderComponents.h`): `MeshRenderer` draws a mesh of a model asset, `DirectionalLight`, `PointLight` and `SpotLight` light it (shining along the entity's -Z), `Environment` lights it from an HDR environment map and shows it as the sky, `RenderSettings` sets exposure, the tonemapper (ACES, Khronos PBR Neutral or none), ambient occlusion and shadows, and `Camera` is what a game renders from. A scene without an environment gets a plain grey sky.

`SceneRenderer` (`Source/Client/Lodestone/Renderer`) draws a `RenderWorld` - what `ExtractRenderWorld()` collects from a scene - from a `RenderView` into a `SceneRenderTarget`, and reports `RenderStats`. How it works, and what it leaves for later: [Decision 0018](../Decisions/0018-scene-renderer.md). How models are imported: [Decision 0019](../Decisions/0019-model-import.md).
