#include "Lodestone/Renderer/GpuModel.h"

#include "Lodestone/Renderer/MipChain.h"
#include "Lodestone/Renderer/RenderResources.h"
#include "Lodestone/Renderer/ShaderTypes.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace Lodestone {

	namespace {

		// The vertex layout the mesh shaders read (Shaders/Mesh.hlsl)
		static_assert(sizeof(MeshVertex) == 48);
		static_assert(offsetof(MeshVertex, Normal) == 12);
		static_assert(offsetof(MeshVertex, Tangent) == 24);
		static_assert(offsetof(MeshVertex, TexCoord) == 40);

		struct TextureSlot
		{
			std::optional<uint32_t> Texture;
			bool Srgb = false;
		};

	}

	std::expected<Ref<GpuModel>, Error> GpuModel::Create(
		const RenderResources& resources, nvrhi::ICommandList* commandList, Model model, std::string_view debugName)
	{
		nvrhi::IDevice* device = resources.GetDevice();
		auto gpuModel = CreateRef<GpuModel>(Passkey(), std::move(model));
		const Model& cpu = gpuModel->m_Model;

		for (size_t index = 0; index < cpu.Meshes.size(); ++index)
		{
			const ModelMesh& mesh = cpu.Meshes[index];
			GpuMesh& gpuMesh = gpuModel->m_Meshes.emplace_back();
			gpuMesh.Bounds = mesh.Bounds;
			if (mesh.Vertices.empty() || mesh.Indices.empty())
				continue;

			const std::string vertexName = fmt::format("{} mesh {} vertices", debugName, index);
			nvrhi::BufferDesc vertices;
			vertices.byteSize = mesh.Vertices.size() * sizeof(MeshVertex);
			vertices.debugName = vertexName;
			vertices.isVertexBuffer = true;
			vertices.initialState = nvrhi::ResourceStates::VertexBuffer;
			vertices.keepInitialState = true;
			gpuMesh.VertexBuffer = device->createBuffer(vertices);

			const std::string indexName = fmt::format("{} mesh {} indices", debugName, index);
			nvrhi::BufferDesc indices;
			indices.byteSize = mesh.Indices.size() * sizeof(uint32_t);
			indices.debugName = indexName;
			indices.isIndexBuffer = true;
			indices.initialState = nvrhi::ResourceStates::IndexBuffer;
			indices.keepInitialState = true;
			gpuMesh.IndexBuffer = device->createBuffer(indices);

			if (!gpuMesh.VertexBuffer || !gpuMesh.IndexBuffer)
				return std::unexpected(Error(ErrorCode::DeviceError,
					fmt::format("Creating the buffers of mesh {} of '{}' failed", index, debugName)));
			commandList->writeBuffer(gpuMesh.VertexBuffer, mesh.Vertices.data(), vertices.byteSize);
			commandList->writeBuffer(gpuMesh.IndexBuffer, mesh.Indices.data(), indices.byteSize);

			for (const MeshPrimitive& primitive : mesh.Primitives)
			{
				if (primitive.IndexCount == 0 || primitive.Material >= cpu.Materials.size())
					continue;
				gpuMesh.Primitives.push_back({
					.FirstIndex = primitive.FirstIndex,
					.IndexCount = primitive.IndexCount,
					.Material = primitive.Material,
					.Bounds = primitive.Bounds,
				});
			}
		}

		// Each image becomes a texture once for each color space it's sampled in
		std::map<std::pair<uint32_t, bool>, nvrhi::ITexture*> textures;
		const auto getTexture = [&](const TextureSlot& slot) -> std::expected<nvrhi::ITexture*, Error>
		{
			if (!slot.Texture || *slot.Texture >= cpu.Textures.size() ||
				cpu.Textures[*slot.Texture].Image >= cpu.Images.size())
				return resources.GetWhiteTexture();
			const auto key = std::make_pair(cpu.Textures[*slot.Texture].Image, slot.Srgb);
			if (const auto found = textures.find(key); found != textures.end())
				return found->second;
			auto texture = gpuModel->CreateTexture(device, commandList, key.first, key.second);
			if (!texture)
				return std::unexpected(texture.error());
			gpuModel->m_Textures.push_back(*texture);
			textures.emplace(key, texture->Get());
			return texture->Get();
		};

		for (size_t index = 0; index < cpu.Materials.size(); ++index)
		{
			const ModelMaterial& material = cpu.Materials[index];
			GpuMaterial& gpuMaterial = gpuModel->m_Materials.emplace_back();
			gpuMaterial.Alpha = material.Alpha;
			gpuMaterial.DoubleSided = material.DoubleSided;

			const ShaderTypes::MaterialConstants constants{
				.BaseColor = material.BaseColor,
				.Emissive = glm::vec4(material.Emissive, 0.0f),
				.Metallic = material.Metallic,
				.Roughness = material.Roughness,
				.NormalScale = material.NormalScale,
				.OcclusionStrength = material.OcclusionStrength,
				.AlphaCutoff = material.AlphaCutoff,
				.Flags = material.NormalTexture ? ShaderTypes::MaterialHasNormalTexture : 0u,
			};
			const std::string constantsName = fmt::format("{} material {}", debugName, index);
			nvrhi::BufferDesc buffer;
			buffer.byteSize = sizeof(constants);
			buffer.debugName = constantsName;
			buffer.isConstantBuffer = true;
			buffer.initialState = nvrhi::ResourceStates::ConstantBuffer;
			buffer.keepInitialState = true;
			gpuMaterial.Constants = device->createBuffer(buffer);
			if (!gpuMaterial.Constants)
				return std::unexpected(Error(ErrorCode::DeviceError,
					fmt::format("Creating the constants of material {} of '{}' failed", index, debugName)));
			commandList->writeBuffer(gpuMaterial.Constants, &constants, sizeof(constants));

			// In Mesh.hlsl's slot order
			const std::array<TextureSlot, RenderResources::MaterialTextureCount> slots = {{
				{material.BaseColorTexture, true},
				{material.MetallicRoughnessTexture, false},
				{material.NormalTexture, false},
				{material.OcclusionTexture, false},
				{material.EmissiveTexture, true},
			}};
			nvrhi::BindingSetDesc bindings;
			bindings.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, gpuMaterial.Constants));
			for (uint32_t slot = 0; slot < slots.size(); ++slot)
			{
				auto texture = getTexture(slots[slot]);
				if (!texture)
					return std::unexpected(texture.error());
				nvrhi::ISampler* sampler =
					resources.GetMaterialSampler(TextureWrap::Repeat, TextureWrap::Repeat, false);
				if (slots[slot].Texture && *slots[slot].Texture < cpu.Textures.size())
				{
					const ModelTexture& modelTexture = cpu.Textures[*slots[slot].Texture];
					sampler =
						resources.GetMaterialSampler(modelTexture.WrapU, modelTexture.WrapV, modelTexture.Nearest);
				}
				bindings.addItem(nvrhi::BindingSetItem::Texture_SRV(slot, *texture))
					.addItem(nvrhi::BindingSetItem::Sampler(slot, sampler));
			}
			bindings.addItem(nvrhi::BindingSetItem::PushConstants(1, sizeof(ShaderTypes::DrawConstants)));
			gpuMaterial.Bindings = device->createBindingSet(bindings, resources.GetMaterialLayout());
			if (!gpuMaterial.Bindings)
				return std::unexpected(Error(ErrorCode::DeviceError,
					fmt::format("Creating the binding set of material {} of '{}' failed", index, debugName)));
		}

		// The images are on the GPU now; the CPU copies would only take memory
		gpuModel->m_Model.Images.clear();
		gpuModel->m_Model.Images.shrink_to_fit();
		return gpuModel;
	}

	GpuModel::GpuModel(Passkey /*passkey*/, Model model)
		: m_Model(std::move(model))
	{
	}

	std::expected<nvrhi::TextureHandle, Error> GpuModel::CreateTexture(
		nvrhi::IDevice* device, nvrhi::ICommandList* commandList, uint32_t image, bool srgb) const
	{
		const Image& source = m_Model.Images[image];
		if (source.IsEmpty())
			return std::unexpected(Error(ErrorCode::InvalidArgument, fmt::format("Image {} is empty", image)));
		std::vector<Image> mips = GenerateMipChain(source, srgb);

		// The levels to upload: from the first that fits the size limit
		std::vector<const Image*> levels;
		if (source.GetWidth() <= MaxTextureSize && source.GetHeight() <= MaxTextureSize)
			levels.push_back(&source);
		for (const Image& mip : mips)
		{
			if (mip.GetWidth() <= MaxTextureSize && mip.GetHeight() <= MaxTextureSize)
				levels.push_back(&mip);
		}

		const std::string name = fmt::format("Model image {}{}", image, srgb ? " (sRGB)" : "");
		nvrhi::TextureDesc desc;
		desc.width = levels.front()->GetWidth();
		desc.height = levels.front()->GetHeight();
		desc.mipLevels = static_cast<uint32_t>(levels.size());
		desc.format = srgb ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM;
		desc.debugName = name;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		nvrhi::TextureHandle texture = device->createTexture(desc);
		if (!texture)
			return std::unexpected(Error(ErrorCode::DeviceError,
				fmt::format("Creating a {}x{} texture for image {} failed", desc.width, desc.height, image)));
		for (uint32_t level = 0; level < levels.size(); ++level)
		{
			const Image& mip = *levels[level];
			commandList->writeTexture(
				texture, 0, level, mip.GetPixels().data(), static_cast<size_t>(mip.GetWidth()) * 4);
		}
		return texture;
	}

}
