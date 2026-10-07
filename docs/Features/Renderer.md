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
- Sustain 60 fps at 1920x1080 on a mid-range GPU (e.g. RTX 3060 class) with all renderer features enabled in the test scene
- Use the stats overlay and GPU timings to catch regressions
