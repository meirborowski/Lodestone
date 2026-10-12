#include "Lodestone/Asset/ModelImporter.h"

#include "Lodestone/Core/Base64.h"
#include "Lodestone/Core/FileSystem.h"

#include <cgltf.h>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace Lodestone {

	namespace {

		// The extensions a model may require: the ones whose data the importer reads correctly
		constexpr std::array<std::string_view, 1> SupportedRequiredExtensions = {"KHR_mesh_quantization"};

		// GL sampler constants, as glTF uses them
		constexpr int GlNearest = 9728;
		constexpr int GlClampToEdge = 33071;
		constexpr int GlMirroredRepeat = 33648;

		using GltfData = std::unique_ptr<cgltf_data, decltype(&cgltf_free)>;

		// Passed to cgltf's file callbacks: the reader, and why it failed
		struct ReadContext
		{
			const ModelImporter::FileReader* Reader = nullptr;
			std::optional<Error> Failure;
		};

		cgltf_result ReadModelFile(const cgltf_memory_options* /*memory*/, const cgltf_file_options* file,
			const char* path, cgltf_size* size, void** data)
		{
			auto* context = static_cast<ReadContext*>(file->user_data);
			if (context->Reader == nullptr || !*context->Reader)
			{
				context->Failure = Error(ErrorCode::NotFound,
					fmt::format("The model refers to the file '{}', but it was loaded from memory", path));
				return cgltf_result_file_not_found;
			}
			auto bytes = (*context->Reader)(path);
			if (!bytes)
			{
				context->Failure = std::move(bytes).error();
				return cgltf_result_file_not_found;
			}
			// The buffer's declared size; the file must have at least that much
			if (*size != 0 && bytes->size() < *size)
			{
				context->Failure = Error(ErrorCode::ParseError,
					fmt::format(
						"'{}' is {} bytes, shorter than the {} its buffer declares", path, bytes->size(), *size));
				return cgltf_result_data_too_short;
			}
			void* memory = std::malloc(std::max<size_t>(bytes->size(), 1));
			if (memory == nullptr)
				return cgltf_result_out_of_memory;
			if (!bytes->empty())
				std::memcpy(memory, bytes->data(), bytes->size());
			*size = bytes->size();
			*data = memory;
			return cgltf_result_success;
		}

		void ReleaseModelFile(const cgltf_memory_options* /*memory*/, const cgltf_file_options* /*file*/, void* data)
		{
			std::free(data); // NOLINT(cppcoreguidelines-no-malloc): allocated with malloc for cgltf
		}

		std::string_view DescribeResult(cgltf_result result)
		{
			switch (result)
			{
				case cgltf_result_success:
					return "success";
				case cgltf_result_data_too_short:
					return "the data is too short";
				case cgltf_result_unknown_format:
					return "the format isn't glTF";
				case cgltf_result_invalid_json:
					return "the JSON is invalid";
				case cgltf_result_invalid_gltf:
					return "it isn't valid glTF";
				case cgltf_result_invalid_options:
					return "invalid options";
				case cgltf_result_file_not_found:
					return "a file it refers to wasn't found";
				case cgltf_result_io_error:
					return "a file couldn't be read";
				case cgltf_result_out_of_memory:
					return "out of memory";
				case cgltf_result_legacy_gltf:
					return "it's glTF 1.0, which isn't supported";
				case cgltf_result_max_enum:
					break;
			}
			return "an unknown error";
		}

		Error MakeError(std::string message)
		{
			return Error(ErrorCode::ParseError, std::move(message));
		}

		std::string GetName(const char* name, std::string_view fallback, size_t index)
		{
			return name != nullptr && *name != '\0' ? std::string(name) : fmt::format("{} {}", fallback, index);
		}

		Error MakeLimitError(std::string_view what, uint64_t limit)
		{
			return Error(
				ErrorCode::Unsupported, fmt::format("The model has more than the {} {} supported", limit, what));
		}

		// Reads an accessor's elements as floats, checking they have the expected type and aren't too many
		std::expected<std::vector<float>, Error> ReadFloats(
			const cgltf_accessor& accessor, cgltf_type type, uint64_t maxCount, std::string_view context)
		{
			if (accessor.type != type)
				return std::unexpected(MakeError(fmt::format("{} has the wrong element type", context)));
			if (accessor.count > maxCount)
				return std::unexpected(Error(ErrorCode::Unsupported,
					fmt::format("{} has {} elements, more than the {} supported", context, accessor.count, maxCount)));
			const cgltf_size floatCount = accessor.count * cgltf_num_components(type);
			std::vector<float> values(floatCount);
			if (cgltf_accessor_unpack_floats(&accessor, values.data(), floatCount) != floatCount)
				return std::unexpected(MakeError(fmt::format("{} can't be read", context)));
			if (!std::ranges::all_of(values, [](float value) { return std::isfinite(value); }))
				return std::unexpected(MakeError(fmt::format("{} has values that aren't finite numbers", context)));
			return values;
		}

		std::expected<std::vector<uint32_t>, Error> ReadIndices(
			const cgltf_accessor& accessor, size_t vertexCount, uint64_t maxCount, std::string_view context)
		{
			if (accessor.count > maxCount)
				return std::unexpected(Error(ErrorCode::Unsupported,
					fmt::format("{} has {} indices, more than the {} supported", context, accessor.count, maxCount)));
			std::vector<uint32_t> indices(accessor.count);
			if (cgltf_accessor_unpack_indices(&accessor, indices.data(), sizeof(uint32_t), accessor.count) !=
				accessor.count)
				return std::unexpected(MakeError(fmt::format("{} can't be read", context)));
			if (std::ranges::any_of(indices, [vertexCount](uint32_t index) { return index >= vertexCount; }))
				return std::unexpected(MakeError(fmt::format("{} refer to vertices that don't exist", context)));
			return indices;
		}

		// How many indices ToTriangleList() makes from a primitive's
		uint64_t GetTriangleListSize(cgltf_primitive_type type, uint64_t indexCount)
		{
			if (type == cgltf_primitive_type_triangles)
				return indexCount / 3 * 3;
			return indexCount < 3 ? 0 : (indexCount - 2) * 3;
		}

		// Triangle lists from strips and fans
		std::vector<uint32_t> ToTriangleList(cgltf_primitive_type type, std::vector<uint32_t> indices)
		{
			if (type == cgltf_primitive_type_triangles)
			{
				indices.resize(indices.size() / 3 * 3);
				return indices;
			}
			std::vector<uint32_t> triangles;
			if (indices.size() < 3)
				return triangles;
			triangles.reserve((indices.size() - 2) * 3);
			for (size_t index = 2; index < indices.size(); ++index)
			{
				if (type == cgltf_primitive_type_triangle_strip)
				{
					// Every other triangle of a strip is wound the other way
					const bool even = index % 2 == 0;
					triangles.push_back(indices[index - 2]);
					triangles.push_back(even ? indices[index - 1] : indices[index]);
					triangles.push_back(even ? indices[index] : indices[index - 1]);
				}
				else
				{
					triangles.push_back(indices[0]);
					triangles.push_back(indices[index - 1]);
					triangles.push_back(indices[index]);
				}
			}
			return triangles;
		}

		AlphaMode ToAlphaMode(cgltf_alpha_mode mode)
		{
			switch (mode)
			{
				case cgltf_alpha_mode_mask:
					return AlphaMode::Mask;
				case cgltf_alpha_mode_blend:
					return AlphaMode::Blend;
				case cgltf_alpha_mode_opaque:
				case cgltf_alpha_mode_max_enum:
					break;
			}
			return AlphaMode::Opaque;
		}

		glm::vec3 GetAnyPerpendicular(const glm::vec3& normal)
		{
			const glm::vec3 axis =
				std::abs(normal.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
			return glm::normalize(glm::cross(normal, axis));
		}

		// Gives every vertex its own normal when a primitive has none, as the glTF specification asks: flat shading,
		// so each triangle gets vertices of its own
		void MakeFlatNormals(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices)
		{
			std::vector<MeshVertex> flat;
			flat.reserve(indices.size());
			for (size_t index = 0; index + 2 < indices.size(); index += 3)
			{
				std::array<MeshVertex, 3> triangle = {
					vertices[indices[index]], vertices[indices[index + 1]], vertices[indices[index + 2]]};
				const glm::vec3 cross = glm::cross(
					triangle[1].Position - triangle[0].Position, triangle[2].Position - triangle[0].Position);
				const float length = glm::length(cross);
				const glm::vec3 normal = length > 0.0f ? cross / length : glm::vec3(0.0f, 0.0f, 1.0f);
				for (MeshVertex& vertex : triangle)
				{
					vertex.Normal = normal;
					flat.push_back(vertex);
				}
			}
			vertices = std::move(flat);
			for (size_t index = 0; index < indices.size(); ++index)
				indices[index] = static_cast<uint32_t>(index);
		}

		// Tangents from texture coordinates when a primitive has none, for normal mapping: each triangle's tangent
		// and bitangent, accumulated per vertex and made perpendicular to the normal
		void GenerateTangents(std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices)
		{
			std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0.0f));
			std::vector<glm::vec3> bitangents(vertices.size(), glm::vec3(0.0f));
			for (size_t index = 0; index + 2 < indices.size(); index += 3)
			{
				const std::array<uint32_t, 3> corners = {indices[index], indices[index + 1], indices[index + 2]};
				const glm::vec3 edge1 = vertices[corners[1]].Position - vertices[corners[0]].Position;
				const glm::vec3 edge2 = vertices[corners[2]].Position - vertices[corners[0]].Position;
				const glm::vec2 delta1 = vertices[corners[1]].TexCoord - vertices[corners[0]].TexCoord;
				const glm::vec2 delta2 = vertices[corners[2]].TexCoord - vertices[corners[0]].TexCoord;
				const float determinant = delta1.x * delta2.y - delta2.x * delta1.y;
				if (std::abs(determinant) < 1e-12f)
					continue;
				const glm::vec3 tangent = (edge1 * delta2.y - edge2 * delta1.y) / determinant;
				const glm::vec3 bitangent = (edge2 * delta1.x - edge1 * delta2.x) / determinant;
				for (const uint32_t corner : corners)
				{
					tangents[corner] += tangent;
					bitangents[corner] += bitangent;
				}
			}
			for (size_t index = 0; index < vertices.size(); ++index)
			{
				MeshVertex& vertex = vertices[index];
				glm::vec3 tangent = tangents[index] - vertex.Normal * glm::dot(vertex.Normal, tangents[index]);
				const float length = glm::length(tangent);
				tangent =
					length > 1e-12f && std::isfinite(length) ? tangent / length : GetAnyPerpendicular(vertex.Normal);
				const float handedness =
					glm::dot(glm::cross(vertex.Normal, tangent), bitangents[index]) < 0.0f ? -1.0f : 1.0f;
				vertex.Tangent = glm::vec4(tangent, handedness);
			}
		}

		// The importer's working state for one model
		class Importer
		{
		public:
			Importer(
				const cgltf_data& data, const ModelImporter::FileReader& readFile, const ModelImportOptions& options)
				: m_Data(&data), m_ReadFile(&readFile), m_Options(&options)
			{
			}

			std::expected<Model, Error> Run()
			{
				if (auto images = ImportTextures(); !images)
					return std::unexpected(images.error());
				ImportMaterials();
				if (auto meshes = ImportMeshes(); !meshes)
					return std::unexpected(meshes.error());
				if (auto nodes = ImportNodes(); !nodes)
					return std::unexpected(nodes.error());
				return std::move(m_Model);
			}

		private:
			std::expected<Image, Error> LoadImage(const cgltf_image& image, std::string_view context) const
			{
				std::vector<uint8_t> encoded;
				if (image.buffer_view != nullptr)
				{
					const uint8_t* data = cgltf_buffer_view_data(image.buffer_view);
					if (data == nullptr)
						return std::unexpected(MakeError(fmt::format("{}: its buffer has no data", context)));
					encoded.assign(data, data + image.buffer_view->size);
				}
				else if (image.uri != nullptr)
				{
					const std::string_view uri = image.uri;
					if (uri.starts_with("data:"))
					{
						const size_t base64 = uri.find(";base64,");
						if (base64 == std::string_view::npos)
							return std::unexpected(
								MakeError(fmt::format("{}: only Base64 data URIs are supported", context)));
						auto decoded = DecodeBase64(uri.substr(base64 + 8));
						if (!decoded)
							return std::unexpected(decoded.error().WithContext(context));
						encoded = std::move(*decoded);
					}
					else
					{
						std::string path(uri);
						path.resize(cgltf_decode_uri(path.data()));
						if (!*m_ReadFile)
							return std::unexpected(Error(ErrorCode::NotFound,
								fmt::format("{} refers to the file '{}', but the model was loaded from memory", context,
									path)));
						auto bytes = (*m_ReadFile)(path);
						if (!bytes)
							return std::unexpected(bytes.error().WithContext(context));
						encoded = std::move(*bytes);
					}
				}
				else
				{
					return std::unexpected(MakeError(fmt::format("{} has neither a URI nor a buffer view", context)));
				}
				auto decoded = Image::Decode(encoded);
				if (!decoded)
					return std::unexpected(decoded.error().WithContext(context));
				return decoded;
			}

			std::expected<void, Error> ImportTextures()
			{
				// Images are decoded once, however many textures use them, and only if a texture does
				std::vector<std::optional<uint32_t>> imageIndices(m_Data->images_count);
				m_TextureIndices.assign(m_Data->textures_count, std::nullopt);
				if (!m_Options->LoadTextures)
					return {};
				uint64_t pixelCount = 0;
				for (size_t textureIndex = 0; textureIndex < m_Data->textures_count; ++textureIndex)
				{
					const cgltf_texture& texture = m_Data->textures[textureIndex];
					// Textures with only an extension's image format (Basis, WebP) have no image Lodestone reads
					if (texture.image == nullptr)
						continue;
					const size_t imageIndex = cgltf_image_index(m_Data, texture.image);
					std::optional<uint32_t>& modelImage = imageIndices[imageIndex];
					if (!modelImage)
					{
						auto image = LoadImage(*texture.image, fmt::format("images[{}]", imageIndex));
						if (!image)
							return std::unexpected(image.error());
						pixelCount += static_cast<uint64_t>(image->GetWidth()) * image->GetHeight();
						if (pixelCount > m_Options->MaxImagePixelCount)
							return std::unexpected(MakeLimitError("image pixels", m_Options->MaxImagePixelCount));
						modelImage = static_cast<uint32_t>(m_Model.Images.size());
						m_Model.Images.push_back(std::move(*image));
					}

					ModelTexture imported{.Image = *modelImage};
					if (const cgltf_sampler* sampler = texture.sampler)
					{
						imported.WrapU = ToWrap(sampler->wrap_s);
						imported.WrapV = ToWrap(sampler->wrap_t);
						imported.Nearest = sampler->mag_filter == GlNearest;
					}
					m_TextureIndices[textureIndex] = static_cast<uint32_t>(m_Model.Textures.size());
					m_Model.Textures.push_back(imported);
				}
				return {};
			}

			static TextureWrap ToWrap(int wrap)
			{
				if (wrap == GlClampToEdge)
					return TextureWrap::ClampToEdge;
				if (wrap == GlMirroredRepeat)
					return TextureWrap::MirroredRepeat;
				return TextureWrap::Repeat;
			}

			// A material's texture, if it has one Lodestone can use: an image it read, on the first texture
			// coordinates
			std::optional<uint32_t> GetTexture(const cgltf_texture_view& view) const
			{
				if (view.texture == nullptr || view.texcoord != 0)
					return std::nullopt;
				return m_TextureIndices[cgltf_texture_index(m_Data, view.texture)];
			}

			void ImportMaterials()
			{
				for (size_t index = 0; index < m_Data->materials_count; ++index)
				{
					const cgltf_material& material = m_Data->materials[index];
					ModelMaterial imported;
					imported.Name = GetName(material.name, "Material", index);
					if (material.has_pbr_metallic_roughness)
					{
						const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
						imported.BaseColor = glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
							pbr.base_color_factor[2], pbr.base_color_factor[3]);
						imported.Metallic = std::clamp(pbr.metallic_factor, 0.0f, 1.0f);
						imported.Roughness = std::clamp(pbr.roughness_factor, 0.0f, 1.0f);
						imported.BaseColorTexture = GetTexture(pbr.base_color_texture);
						imported.MetallicRoughnessTexture = GetTexture(pbr.metallic_roughness_texture);
					}
					const float emissiveStrength =
						material.has_emissive_strength ? material.emissive_strength.emissive_strength : 1.0f;
					imported.Emissive = glm::vec3(material.emissive_factor[0], material.emissive_factor[1],
											material.emissive_factor[2]) *
						emissiveStrength;
					imported.EmissiveTexture = GetTexture(material.emissive_texture);
					imported.NormalTexture = GetTexture(material.normal_texture);
					imported.NormalScale = material.normal_texture.scale;
					imported.OcclusionTexture = GetTexture(material.occlusion_texture);
					imported.OcclusionStrength = std::clamp(material.occlusion_texture.scale, 0.0f, 1.0f);
					imported.Alpha = ToAlphaMode(material.alpha_mode);
					imported.AlphaCutoff = material.alpha_cutoff;
					imported.DoubleSided = material.double_sided;
					m_Model.Materials.push_back(std::move(imported));
				}
			}

			// The material for primitives without one, added the first time one needs it
			uint32_t GetDefaultMaterial()
			{
				if (!m_DefaultMaterial)
				{
					m_DefaultMaterial = static_cast<uint32_t>(m_Model.Materials.size());
					m_Model.Materials.push_back(ModelMaterial{.Name = "Default", .Metallic = 0.0f});
				}
				return *m_DefaultMaterial;
			}

			std::expected<void, Error> ImportMeshes()
			{
				for (size_t meshIndex = 0; meshIndex < m_Data->meshes_count; ++meshIndex)
				{
					const cgltf_mesh& mesh = m_Data->meshes[meshIndex];
					ModelMesh imported;
					imported.Name = GetName(mesh.name, "Mesh", meshIndex);
					for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex)
					{
						const std::string context = fmt::format("meshes[{}].primitives[{}]", meshIndex, primitiveIndex);
						if (auto added = AddPrimitive(mesh.primitives[primitiveIndex], context, imported); !added)
							return std::unexpected(added.error());
					}
					m_Model.Meshes.push_back(std::move(imported));
				}
				return {};
			}

			std::expected<void, Error> AddPrimitive(
				const cgltf_primitive& primitive, const std::string& context, ModelMesh& mesh)
			{
				// Points and lines aren't drawn
				if (primitive.type != cgltf_primitive_type_triangles &&
					primitive.type != cgltf_primitive_type_triangle_strip &&
					primitive.type != cgltf_primitive_type_triangle_fan)
					return {};
				if (primitive.has_draco_mesh_compression)
					return std::unexpected(Error(ErrorCode::Unsupported,
						fmt::format("{} is compressed with Draco, which isn't supported", context)));

				const cgltf_accessor* positions = nullptr;
				const cgltf_accessor* normals = nullptr;
				const cgltf_accessor* tangents = nullptr;
				const cgltf_accessor* texCoords = nullptr;
				for (size_t index = 0; index < primitive.attributes_count; ++index)
				{
					const cgltf_attribute& attribute = primitive.attributes[index];
					if (attribute.type == cgltf_attribute_type_position)
						positions = attribute.data;
					else if (attribute.type == cgltf_attribute_type_normal)
						normals = attribute.data;
					else if (attribute.type == cgltf_attribute_type_tangent)
						tangents = attribute.data;
					else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0)
						texCoords = attribute.data;
				}
				if (positions == nullptr)
					return std::unexpected(MakeError(fmt::format("{} has no POSITION attribute", context)));

				// What the primitive makes counts toward the model's limits before anything is read: its triangles'
				// indices, and its vertices - one for every index when it has no normals and is shaded flat
				const uint64_t vertexCount = positions->count;
				const uint64_t indexCount = GetTriangleListSize(
					primitive.type, primitive.indices != nullptr ? primitive.indices->count : vertexCount);
				const uint64_t producedVertices = normals != nullptr ? vertexCount : indexCount;
				if (vertexCount > m_Options->MaxVertexCount ||
					m_VertexCount + producedVertices > m_Options->MaxVertexCount)
					return std::unexpected(MakeLimitError("vertices", m_Options->MaxVertexCount));
				if (m_IndexCount + indexCount > m_Options->MaxIndexCount)
					return std::unexpected(MakeLimitError("indices", m_Options->MaxIndexCount));
				m_VertexCount += producedVertices;
				m_IndexCount += indexCount;

				const auto positionValues =
					ReadFloats(*positions, cgltf_type_vec3, m_Options->MaxVertexCount, context + " POSITION");
				if (!positionValues)
					return std::unexpected(positionValues.error());

				std::vector<MeshVertex> vertices(vertexCount);
				for (size_t index = 0; index < vertexCount; ++index)
				{
					vertices[index].Position = glm::vec3((*positionValues)[index * 3], (*positionValues)[index * 3 + 1],
						(*positionValues)[index * 3 + 2]);
				}
				if (normals != nullptr)
				{
					const auto values = ReadFloats(*normals, cgltf_type_vec3, vertexCount, context + " NORMAL");
					if (!values)
						return std::unexpected(values.error());
					for (size_t index = 0; index < vertexCount; ++index)
					{
						const glm::vec3 normal(
							(*values)[index * 3], (*values)[index * 3 + 1], (*values)[index * 3 + 2]);
						const float length = glm::length(normal);
						vertices[index].Normal = length > 1e-12f ? normal / length : glm::vec3(0.0f, 0.0f, 1.0f);
					}
				}
				if (texCoords != nullptr)
				{
					const auto values = ReadFloats(*texCoords, cgltf_type_vec2, vertexCount, context + " TEXCOORD_0");
					if (!values)
						return std::unexpected(values.error());
					for (size_t index = 0; index < vertexCount; ++index)
						vertices[index].TexCoord = glm::vec2((*values)[index * 2], (*values)[index * 2 + 1]);
				}

				std::vector<uint32_t> indices;
				if (primitive.indices != nullptr)
				{
					auto read =
						ReadIndices(*primitive.indices, vertexCount, m_Options->MaxIndexCount, context + " indices");
					if (!read)
						return std::unexpected(read.error());
					indices = std::move(*read);
				}
				else
				{
					indices.resize(vertexCount);
					for (size_t index = 0; index < vertexCount; ++index)
						indices[index] = static_cast<uint32_t>(index);
				}
				indices = ToTriangleList(primitive.type, std::move(indices));

				// Tangents are only kept from the file if the normals are too: flat normals make new vertices
				bool hasTangents = false;
				if (normals == nullptr)
				{
					MakeFlatNormals(vertices, indices);
				}
				else if (tangents != nullptr)
				{
					const auto values = ReadFloats(*tangents, cgltf_type_vec4, vertexCount, context + " TANGENT");
					if (!values)
						return std::unexpected(values.error());
					for (size_t index = 0; index < vertexCount; ++index)
					{
						const glm::vec3 tangent(
							(*values)[index * 4], (*values)[index * 4 + 1], (*values)[index * 4 + 2]);
						const float length = glm::length(tangent);
						vertices[index].Tangent =
							glm::vec4(length > 1e-12f ? tangent / length : GetAnyPerpendicular(vertices[index].Normal),
								(*values)[index * 4 + 3] < 0.0f ? -1.0f : 1.0f);
					}
					hasTangents = true;
				}
				if (!hasTangents)
					GenerateTangents(vertices, indices);

				MeshPrimitive imported;
				imported.FirstIndex = static_cast<uint32_t>(mesh.Indices.size());
				imported.IndexCount = static_cast<uint32_t>(indices.size());
				imported.Material = primitive.material != nullptr
					? static_cast<uint32_t>(cgltf_material_index(m_Data, primitive.material))
					: GetDefaultMaterial();
				const auto baseVertex = static_cast<uint32_t>(mesh.Vertices.size());
				for (const uint32_t index : indices)
				{
					mesh.Indices.push_back(baseVertex + index);
					imported.Bounds.Add(vertices[index].Position);
				}
				mesh.Vertices.insert(mesh.Vertices.end(), vertices.begin(), vertices.end());
				mesh.Bounds.Add(imported.Bounds);
				mesh.Primitives.push_back(imported);
				return {};
			}

			std::expected<void, Error> ImportNodes()
			{
				if (m_Data->nodes_count > m_Options->MaxNodeCount)
					return std::unexpected(MakeLimitError("nodes", m_Options->MaxNodeCount));
				for (size_t index = 0; index < m_Data->nodes_count; ++index)
				{
					const cgltf_node& node = m_Data->nodes[index];
					ModelNode imported;
					imported.Name = GetName(node.name, "Node", index);
					if (node.has_matrix)
					{
						const glm::mat4 matrix = glm::make_mat4(node.matrix);
						Decompose(matrix, imported);
					}
					else
					{
						if (node.has_translation)
							imported.Position =
								glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
						if (node.has_rotation)
						{
							const glm::quat rotation =
								glm::quat::wxyz(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
							const float length = glm::length(rotation);
							imported.Rotation = length > 1e-12f && std::isfinite(length)
								? rotation / length
								: glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
						}
						if (node.has_scale)
							imported.Scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
					}
					if (!std::isfinite(imported.Position.x + imported.Position.y + imported.Position.z +
							imported.Scale.x + imported.Scale.y + imported.Scale.z))
						return std::unexpected(
							MakeError(fmt::format("nodes[{}] has a transform that isn't finite", index)));
					if (node.mesh != nullptr)
						imported.Mesh = static_cast<uint32_t>(cgltf_mesh_index(m_Data, node.mesh));
					for (size_t child = 0; child < node.children_count; ++child)
						imported.Children.push_back(
							static_cast<uint32_t>(cgltf_node_index(m_Data, node.children[child])));
					m_Model.Nodes.push_back(std::move(imported));
				}

				// The default scene's nodes, or every node without a parent
				const cgltf_scene* scene = m_Data->scene;
				if (scene == nullptr && m_Data->scenes_count > 0)
					scene = &m_Data->scenes[0];
				if (scene != nullptr)
				{
					for (size_t index = 0; index < scene->nodes_count; ++index)
						m_Model.RootNodes.push_back(
							static_cast<uint32_t>(cgltf_node_index(m_Data, scene->nodes[index])));
				}
				else
				{
					for (size_t index = 0; index < m_Data->nodes_count; ++index)
					{
						if (m_Data->nodes[index].parent == nullptr)
							m_Model.RootNodes.push_back(static_cast<uint32_t>(index));
					}
				}
				return {};
			}

			// Position, rotation and scale from a matrix. Shear can't be represented, and is lost
			static void Decompose(const glm::mat4& matrix, ModelNode& node)
			{
				node.Position = glm::vec3(matrix[3]);
				glm::vec3 scale(glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])),
					glm::length(glm::vec3(matrix[2])));
				if (glm::determinant(glm::mat3(matrix)) < 0.0f)
					scale.x = -scale.x;
				node.Scale = scale;
				if (std::abs(scale.x) < 1e-12f || std::abs(scale.y) < 1e-12f || std::abs(scale.z) < 1e-12f)
					return;
				const glm::vec3 xAxis = glm::vec3(matrix[0]) / scale.x;
				const glm::vec3 yAxis = glm::vec3(matrix[1]) / scale.y;
				const glm::vec3 zAxis = glm::vec3(matrix[2]) / scale.z;
				node.Rotation = glm::normalize(glm::quat_cast(glm::mat3{xAxis, yAxis, zAxis}));
			}

		private:
			const cgltf_data* m_Data;
			const ModelImporter::FileReader* m_ReadFile;
			const ModelImportOptions* m_Options;
			Model m_Model;
			// glTF texture index to Model::Textures index, for textures with an image
			std::vector<std::optional<uint32_t>> m_TextureIndices;
			std::optional<uint32_t> m_DefaultMaterial;
			// What the model holds so far, against the limits
			uint64_t m_VertexCount = 0;
			uint64_t m_IndexCount = 0;
		};

	}

	std::expected<Model, Error> ModelImporter::Import(
		const std::filesystem::path& path, const ModelImportOptions& options)
	{
		const auto data = ReadBinaryFile(path, MaxFileSize);
		if (!data)
			return std::unexpected(data.error().WithContext(fmt::format("Importing model {}", path)));
		return Import(*data, MakeDirectoryReader(path.parent_path()), options)
			.transform_error(
				[&path](const Error& error) { return error.WithContext(fmt::format("Importing model {}", path)); });
	}

	std::expected<Model, Error> ModelImporter::Import(
		std::span<const uint8_t> data, const FileReader& readFile, const ModelImportOptions& options)
	{
		// Vertices and indices are addressed with 32 bits
		constexpr uint64_t addressable = std::numeric_limits<uint32_t>::max();
		if (options.MaxVertexCount > addressable || options.MaxIndexCount > addressable)
			return std::unexpected(Error(ErrorCode::InvalidArgument,
				fmt::format("Models can't hold more than {} vertices or indices", addressable)));

		ReadContext readContext{.Reader = &readFile};
		cgltf_options parseOptions{};
		parseOptions.file.read = &ReadModelFile;
		parseOptions.file.release = &ReleaseModelFile;
		parseOptions.file.user_data = &readContext;

		cgltf_data* parsed = nullptr;
		if (const cgltf_result result = cgltf_parse(&parseOptions, data.data(), data.size(), &parsed);
			result != cgltf_result_success)
			return std::unexpected(MakeError(fmt::format("The model can't be read: {}", DescribeResult(result))));
		const GltfData gltf(parsed, &cgltf_free);

		for (size_t index = 0; index < gltf->extensions_required_count; ++index)
		{
			const std::string_view extension = gltf->extensions_required[index];
			if (std::ranges::find(SupportedRequiredExtensions, extension) == SupportedRequiredExtensions.end())
				return std::unexpected(Error(ErrorCode::Unsupported,
					fmt::format("The model requires the {} extension, which isn't supported", extension)));
		}

		// Without a slash in the "model path", cgltf hands each buffer's URI to the reader on its own
		if (const cgltf_result result = cgltf_load_buffers(&parseOptions, gltf.get(), "model");
			result != cgltf_result_success)
		{
			if (readContext.Failure)
				return std::unexpected(*readContext.Failure);
			return std::unexpected(
				MakeError(fmt::format("The model's buffers can't be loaded: {}", DescribeResult(result))));
		}
		if (const cgltf_result result = cgltf_validate(gltf.get()); result != cgltf_result_success)
			return std::unexpected(MakeError(fmt::format("The model isn't valid: {}", DescribeResult(result))));

		return Importer(*gltf, readFile, options).Run();
	}

	ModelImporter::FileReader ModelImporter::MakeDirectoryReader(const std::filesystem::path& directory)
	{
		return [directory = directory.lexically_normal()](
				   std::string_view uri) -> std::expected<std::vector<uint8_t>, Error>
		{
			const std::filesystem::path relative = PathFromUtf8(uri);
			if (uri.empty() || relative.has_root_name() || relative.has_root_directory())
				return std::unexpected(Error(ErrorCode::InvalidArgument,
					fmt::format("The model refers to '{}', which isn't a path relative to it", uri)));
			const std::filesystem::path resolved = (directory / relative).lexically_normal();
			const std::filesystem::path inside = resolved.lexically_relative(directory);
			if (inside.empty() || *inside.begin() == "..")
				return std::unexpected(Error(ErrorCode::InvalidArgument,
					fmt::format("The model refers to '{}', which is outside its directory", uri)));
			return ReadBinaryFile(resolved, MaxFileSize);
		};
	}

}
