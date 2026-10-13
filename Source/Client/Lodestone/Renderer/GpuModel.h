#pragma once

#include "Lodestone/Asset/Model.h"
#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/BoundingBox.h"
#include "Lodestone/Core/Error.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace Lodestone {

	class RenderResources;

	struct GpuPrimitive
	{
		uint32_t FirstIndex = 0;
		uint32_t IndexCount = 0;
		// Into GpuModel::GetMaterials()
		uint32_t Material = 0;
		BoundingBox Bounds;
	};

	struct GpuMesh
	{
		// MeshVertex, 48 bytes each; null for a mesh without triangles
		nvrhi::BufferHandle VertexBuffer;
		// SkinVertex, 24 bytes each, for meshes that can be skinned; null otherwise
		nvrhi::BufferHandle SkinBuffer;
		// 32-bit indices
		nvrhi::BufferHandle IndexBuffer;
		std::vector<GpuPrimitive> Primitives;
		BoundingBox Bounds;
	};

	struct GpuMaterial
	{
		AlphaMode Alpha = AlphaMode::Opaque;
		bool DoubleSided = false;
		nvrhi::BufferHandle Constants;
		// The material's descriptor set (RenderResources::GetMaterialLayout()), with its constants, textures and
		// samplers - a white texture where it has none
		nvrhi::BindingSetHandle Bindings;
	};

	// A model on the GPU: each mesh's vertices and indices, and each material's constants and textures, with mip
	// levels. Textures that hold colors (base color and emissive) are sRGB; the rest are linear. It keeps the CPU
	// model, without its images, for picking and bounds
	class GpuModel
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		// The vertex layouts GpuMesh::VertexBuffer and SkinBuffer have
		static constexpr uint32_t VertexStride = sizeof(MeshVertex);
		static constexpr uint32_t SkinVertexStride = sizeof(SkinVertex);
		// Textures larger than this are uploaded from the first mip level that fits, which every GPU can sample
		static constexpr uint32_t MaxTextureSize = 8192;

		// Creates the GPU resources and records their upload into an open command list
		[[nodiscard]] static std::expected<Ref<GpuModel>, Error> Create(const RenderResources& resources,
			nvrhi::ICommandList* commandList, Model model, std::string_view debugName);

		GpuModel(Passkey passkey, Model model);

		const Model& GetModel() const { return m_Model; }
		std::span<const GpuMesh> GetMeshes() const { return m_Meshes; }
		std::span<const GpuMaterial> GetMaterials() const { return m_Materials; }

	private:
		[[nodiscard]] std::expected<nvrhi::TextureHandle, Error> CreateTexture(
			nvrhi::IDevice* device, nvrhi::ICommandList* commandList, uint32_t image, bool srgb) const;

	private:
		Model m_Model;
		std::vector<GpuMesh> m_Meshes;
		std::vector<GpuMaterial> m_Materials;
		std::vector<nvrhi::TextureHandle> m_Textures;
	};

}
