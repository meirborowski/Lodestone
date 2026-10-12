# 0019 - Model Import: glTF 2.0 with cgltf, into a CPU Model, Instanced as Entities

## Status
Accepted

## Context
Milestone 5 imports models with their meshes, materials and textures; later milestones add skeletons and animation (Milestone 6) and physics shapes (Milestone 7) from the same files. Models come from anywhere, so the importer is untrusted-input code: hostile files must fail cleanly, never crash or exhaust memory, and the fuzzer runs it (see [Decision 0013](0013-fuzz-testing.md)). LodestoneCore can't link the GPU, but physics and the editor's picking need meshes too. HDR environment maps (.hdr) arrive the same milestone.

## Decision
- **glTF 2.0 only, parsed by cgltf** (MIT, a single C header) - `.gltf` with its buffers and images beside it or in data URIs, and binary `.glb`. glTF is the interchange format every DCC tool exports, and it carries PBR materials directly
- **Into a CPU `Model`** (`Lodestone/Asset/Model.h`, Core): meshes (interleaved position, normal, tangent and UV vertices, 32-bit indices, primitives with a material and bounds), metallic-roughness materials, textures (an image and its sampler's wrap and filter), decoded RGBA8 images, and the node hierarchy (TRS). The GPU upload is the renderer's (`GpuModel`); physics and picking read the same `Model`
- **Normalized on import** - strips and fans become triangle lists; points and lines are skipped; missing normals become flat ones (unwelded), missing tangents are generated from UVs; primitives without a material get a default one; node matrices are decomposed into TRS. Only texture coordinate set 0 is used. Of the extensions that may be required, only `KHR_mesh_quantization` is supported (others, such as Draco compression, fail with `Unsupported`); `KHR_materials_emissive_strength` is applied
- **Limits and containment** - files up to 512 MB, 16M vertices, 48M indices and 65536 nodes; images up to 64 megapixels, checked from their headers before decoding; every accessor and index is bounds-checked, and non-finite numbers rejected. Files a model refers to are read through a `FileReader`, which for files on disk only reads inside the model's own directory - a model can't reach `../` or absolute paths
- **HDR images** (`Core/HdrImage.h`) are Radiance `.hdr`, decoded by stb_image to RGBA float32, at most 8192x4096, with an uncompressed RGBE writer for tests and tools
- **Placed in scenes as entities** - `BuildModelEntities()` (`Scene/ModelEntities.h`) turns a model into an entity tree: a root named after the model, an entity per node with its name and transform, and a `MeshRenderer` (model asset UUID and mesh index) on nodes with a mesh. The editor instances it as an undoable command (`EditorContext::InstantiateModel()`, the `model_instantiate` MCP tool, and dragging a model into the viewport or the hierarchy). Instances don't link back to the model file: like prefab instances, they're independent copies (see [Decision 0014](0014-prefabs.md))

## Alternatives
- **Assimp** - many formats, but large, slow to build, and a big attack surface for untrusted files; glTF covers what's needed
- **tinygltf** - pulls in its own JSON parser and image loader, and throws exceptions
- **Converting models to an engine format at import** - faster loading for big projects, and likely with export (Milestone 12) packing assets; for now the renderer imports the source file when a scene first uses it
- **A model component that instances its nodes at runtime** - keeps a link to the file, but complicates the hierarchy and serialization; entity trees are what prefabs already are

## Consequences
- Re-importing a changed model doesn't update entities already placed from it: their `MeshRenderer`s still point at the asset by UUID and mesh index, so the meshes update, but added or removed nodes don't
- Mesh indices are positions in the file's mesh list; reordering meshes in the source file changes what entities draw
- Skins, animations and morph targets are ignored until Milestone 6 adds them to `Model`; cameras and lights in glTF files aren't imported
- The importer, the image decoder and the HDR decoder each have a fuzz test with a corpus of real files (`Tests/Fuzz/Corpus`)
