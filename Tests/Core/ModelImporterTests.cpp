#include "Lodestone/Asset/ModelImporter.h"

#include "Common/DescribeError.h"
#include "Common/TemporaryDirectory.h"
#include "Lodestone/Core/Base64.h"
#include "Lodestone/Core/FileSystem.h"

#include <doctest/doctest.h>
#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>
#include <spdlog/fmt/fmt.h>

#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace Lodestone {

	namespace {

		std::filesystem::path GetModelPath(std::string_view name)
		{
			return std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Models" / name;
		}

		Model ImportFixture(std::string_view name)
		{
			auto model = ModelImporter::Import(GetModelPath(name));
			REQUIRE_MESSAGE(model.has_value(), Testing::DescribeError(model));
			return std::move(*model);
		}

		bool IsNear(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
		{
			return glm::length(a - b) <= tolerance;
		}

		// A glTF document with one triangle, its buffer embedded as a data URI. The JSON's parts can be replaced to
		// break it in one way
		std::string MakeTriangle(std::string_view extra = "", std::string_view positions = "")
		{
			std::vector<float> floats = {0, 0, 0, 1, 0, 0, 0, 1, 0};
			std::vector<uint8_t> bytes(floats.size() * sizeof(float));
			std::memcpy(bytes.data(), floats.data(), bytes.size());
			const std::string uri = "data:application/octet-stream;base64," + EncodeBase64(bytes);
			const std::string accessor = positions.empty()
				? std::string(R"({"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"})")
				: std::string(positions);
			return fmt::format(R"({{"asset": {{"version": "2.0"}}, {}
				"buffers": [{{"uri": "{}", "byteLength": {}}}],
				"bufferViews": [{{"buffer": 0, "byteLength": {}}}],
				"accessors": [{}],
				"meshes": [{{"primitives": [{{"attributes": {{"POSITION": 0}}}}]}}],
				"nodes": [{{"mesh": 0}}]}})",
				extra, uri, bytes.size(), bytes.size(), accessor);
		}

		std::expected<Model, Error> ImportText(std::string_view text)
		{
			const std::vector<uint8_t> bytes(text.begin(), text.end());
			return ModelImporter::Import(bytes);
		}

	}

	TEST_CASE("A glTF model imports with its meshes, material and textures from the files beside it")
	{
		const Model model = ImportFixture("TexturedCube.gltf");

		REQUIRE(model.Meshes.size() == 1);
		const ModelMesh& mesh = model.Meshes[0];
		CHECK(mesh.Name == "Cube");
		CHECK(mesh.Vertices.size() == 24);
		CHECK(mesh.Indices.size() == 36);
		REQUIRE(mesh.Primitives.size() == 1);
		CHECK(mesh.Primitives[0].IndexCount == 36);
		CHECK(mesh.Primitives[0].Material == 0);
		CHECK(IsNear(mesh.Bounds.Min, glm::vec3(-0.5f)));
		CHECK(IsNear(mesh.Bounds.Max, glm::vec3(0.5f)));

		REQUIRE(model.Materials.size() == 1);
		const ModelMaterial& material = model.Materials[0];
		CHECK(material.Name == "Checkered");
		CHECK(material.BaseColor == glm::vec4(1.0f, 0.9f, 0.8f, 1.0f));
		REQUIRE(material.BaseColorTexture.has_value());
		REQUIRE(material.MetallicRoughnessTexture.has_value());
		REQUIRE(material.NormalTexture.has_value());
		CHECK_FALSE(material.EmissiveTexture.has_value());
		REQUIRE(model.Textures.size() == 3);
		REQUIRE(model.Images.size() == 3);
		// The base color texture is a JPEG; the others are PNGs, one with a space in its name
		for (const Image& image : model.Images)
		{
			CHECK(image.GetWidth() == 64);
			CHECK(image.GetHeight() == 64);
		}
		const Image& checker = model.Images[model.Textures[*material.BaseColorTexture].Image];
		CHECK(checker.GetPixel(2, 2).R > 150);
		CHECK(checker.GetPixel(10, 2).R < 100);
		CHECK(model.Textures[0].WrapU == TextureWrap::Repeat);
		CHECK_FALSE(model.Textures[0].Nearest);

		REQUIRE(model.RootNodes.size() == 1);
		CHECK(model.Nodes[model.RootNodes[0]].Mesh == 0u);
	}

	TEST_CASE("Tangents are generated perpendicular to the normals when a model has none")
	{
		const Model model = ImportFixture("TexturedCube.gltf");

		for (const MeshVertex& vertex : model.Meshes[0].Vertices)
		{
			const glm::vec3 tangent(vertex.Tangent);
			CHECK(glm::length(tangent) == doctest::Approx(1.0f).epsilon(1e-4));
			CHECK(std::abs(glm::dot(tangent, vertex.Normal)) < 1e-4f);
			CHECK(std::abs(vertex.Tangent.w) == 1.0f);
		}
		// On the +Z face, texture U runs along +X
		bool found = false;
		for (const MeshVertex& vertex : model.Meshes[0].Vertices)
		{
			if (vertex.Normal == glm::vec3(0.0f, 0.0f, 1.0f))
			{
				CHECK(IsNear(glm::vec3(vertex.Tangent), glm::vec3(1.0f, 0.0f, 0.0f)));
				found = true;
			}
		}
		CHECK(found);
	}

	TEST_CASE("A binary glTF model imports with many meshes and materials")
	{
		const Model model = ImportFixture("MaterialSpheres.glb");

		CHECK(model.Meshes.size() == 25);
		REQUIRE(model.Materials.size() == 25);
		CHECK(model.Nodes.size() == 25);
		CHECK(model.RootNodes.size() == 25);
		CHECK(model.Materials[0].Metallic == 0.0f);
		CHECK(model.Materials[0].Roughness == 0.05f);
		CHECK(model.Materials[24].Metallic == 1.0f);
		CHECK(model.Materials[24].Roughness == 1.0f);
		CHECK(model.Nodes[24].Position == glm::vec3(2.4f, 2.4f, 0.0f));
		const BoundingBox bounds = model.GetBounds();
		CHECK(IsNear(bounds.Min, glm::vec3(-2.9f, -2.9f, -0.5f), 1e-3f));
		CHECK(IsNear(bounds.Max, glm::vec3(2.9f, 2.9f, 0.5f), 1e-3f));
	}

	TEST_CASE("Node transforms import from translations, rotations, scales and matrices")
	{
		const Model model = ImportFixture("Hierarchy.gltf");

		REQUIRE(model.Nodes.size() == 5);
		CHECK(model.RootNodes == std::vector<uint32_t>{0, 4});
		const ModelNode& root = model.Nodes[0];
		CHECK(root.Name == "Root");
		CHECK(root.Position == glm::vec3(1.0f, 0.0f, 0.0f));
		CHECK(root.Children == std::vector<uint32_t>{1, 3});
		CHECK_FALSE(root.Mesh.has_value());

		// A matrix: a quarter turn around Y, scaled by 2, raised by 3
		const ModelNode& turned = model.Nodes[1];
		CHECK(IsNear(turned.Position, glm::vec3(0.0f, 3.0f, 0.0f)));
		CHECK(IsNear(turned.Scale, glm::vec3(2.0f)));
		CHECK(IsNear(turned.Rotation * glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)));

		const ModelNode& parts = model.Nodes[2];
		CHECK(parts.Mesh == 0u);
		CHECK(IsNear(parts.Rotation * glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
		CHECK(model.Nodes[3].Name == "Marker");
	}

	TEST_CASE("Primitives import as triangle lists, strips without indices or normals included")
	{
		const Model model = ImportFixture("Hierarchy.gltf");

		REQUIRE(model.Meshes.size() == 2);
		const ModelMesh& parts = model.Meshes[0];
		REQUIRE(parts.Primitives.size() == 2);
		CHECK(parts.Primitives[0].IndexCount == 36);
		CHECK(parts.Primitives[0].Material == 0);
		// A strip of five vertices is three triangles, each with vertices of its own for flat normals
		const MeshPrimitive& strip = parts.Primitives[1];
		CHECK(strip.FirstIndex == 36);
		CHECK(strip.IndexCount == 9);
		CHECK(strip.Material == 1);
		CHECK(parts.Vertices.size() == 24 + 9);
		for (uint32_t index = strip.FirstIndex; index < strip.FirstIndex + strip.IndexCount; ++index)
		{
			const glm::vec3 normal = parts.Vertices[parts.Indices[index]].Normal;
			CHECK(std::abs(normal.z) == doctest::Approx(1.0f));
		}

		// The material without a source gets a default one, added after the model's own
		REQUIRE(model.Materials.size() == 3);
		CHECK(model.Meshes[1].Primitives[0].Material == 2);
		CHECK(model.Materials[2].Name == "Default");
		CHECK(model.Materials[2].Metallic == 0.0f);
	}

	TEST_CASE("Materials import their factors, emissive strength, alpha modes and sidedness")
	{
		const Model hierarchy = ImportFixture("Hierarchy.gltf");
		const ModelMaterial& blue = hierarchy.Materials[1];
		CHECK(blue.Name == "Blue");
		CHECK(blue.BaseColor == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
		CHECK(blue.Roughness == 0.25f);
		CHECK(blue.Emissive == glm::vec3(0.0f, 0.0f, 1.0f));
		CHECK(blue.DoubleSided);

		const Model alpha = ImportFixture("AlphaModes.glb");
		REQUIRE(alpha.Materials.size() == 4);
		CHECK(alpha.Materials[0].Alpha == AlphaMode::Opaque);
		CHECK(alpha.Materials[1].Alpha == AlphaMode::Mask);
		CHECK(alpha.Materials[1].AlphaCutoff == 0.5f);
		CHECK(alpha.Materials[2].Alpha == AlphaMode::Blend);
		CHECK(alpha.Materials[2].BaseColor.a == 0.5f);
		CHECK(alpha.Materials[3].DoubleSided);
		// The mask's texture is a PNG embedded in the binary file
		REQUIRE(alpha.Materials[1].BaseColorTexture.has_value());
		const Image& leaf = alpha.Images[alpha.Textures[*alpha.Materials[1].BaseColorTexture].Image];
		CHECK(leaf.GetWidth() == 32);
		CHECK(leaf.GetPixel(16, 16).A == 255);
		CHECK(leaf.GetPixel(0, 0).A == 0);
	}

	TEST_CASE("A model in memory imports when it embeds everything")
	{
		const auto bytes = ReadBinaryFile(GetModelPath("AlphaModes.glb"));
		REQUIRE(bytes.has_value());

		const auto model = ModelImporter::Import(*bytes);

		REQUIRE_MESSAGE(model.has_value(), Testing::DescribeError(model));
		CHECK(model->Meshes.size() == 4);
	}

	TEST_CASE("A model in memory can't refer to other files")
	{
		const auto bytes = ReadBinaryFile(GetModelPath("TexturedCube.gltf"));
		REQUIRE(bytes.has_value());

		const auto model = ModelImporter::Import(*bytes);

		REQUIRE_FALSE(model.has_value());
		CHECK(model.error().GetCode() == ErrorCode::NotFound);
	}

	TEST_CASE("A model can't read files outside its directory")
	{
		const Testing::TemporaryDirectory directory("ModelOutside");
		const std::filesystem::path models = directory.GetPath() / "Models";
		std::filesystem::create_directories(models);
		REQUIRE(WriteFileAtomically(directory.GetPath() / "Secret.bin", std::string(36, 'x')).has_value());

		for (const std::string_view uri : {"../Secret.bin", "Sub/../../Secret.bin", "/etc/passwd"})
		{
			CAPTURE(uri);
			const std::string text = fmt::format(R"({{"asset": {{"version": "2.0"}},
				"buffers": [{{"uri": "{}", "byteLength": 36}}]}})",
				uri);
			REQUIRE(WriteFileAtomically(models / "Escape.gltf", text).has_value());
			const auto model = ModelImporter::Import(models / "Escape.gltf");
			REQUIRE_FALSE(model.has_value());
			CHECK(model.error().GetCode() == ErrorCode::InvalidArgument);
		}
	}

	TEST_CASE("A minimal model imports, with flat normals and its root found without a scene")
	{
		const auto model = ImportText(MakeTriangle());

		REQUIRE_MESSAGE(model.has_value(), Testing::DescribeError(model));
		REQUIRE(model->Meshes.size() == 1);
		CHECK(model->Meshes[0].Indices == std::vector<uint32_t>{0, 1, 2});
		CHECK(model->Meshes[0].Vertices[0].Normal == glm::vec3(0.0f, 0.0f, 1.0f));
		CHECK(model->RootNodes == std::vector<uint32_t>{0});
	}

	TEST_CASE("Invalid models fail to import with a reason")
	{
		SUBCASE("Not glTF")
		{
			CHECK_FALSE(ImportText("not a model").has_value());
		}
		SUBCASE("A missing file")
		{
			const auto model = ModelImporter::Import(GetModelPath("Missing.gltf"));
			REQUIRE_FALSE(model.has_value());
			CHECK(model.error().GetCode() == ErrorCode::FileNotFound);
		}
		SUBCASE("A required extension that isn't supported")
		{
			const auto model = ImportText(MakeTriangle(R"("extensionsRequired": ["KHR_draco_mesh_compression"],)"));
			REQUIRE_FALSE(model.has_value());
			CHECK(model.error().GetCode() == ErrorCode::Unsupported);
		}
		SUBCASE("An accessor beyond its buffer")
		{
			CHECK_FALSE(
				ImportText(MakeTriangle("", R"({"bufferView": 0, "componentType": 5126, "count": 30, "type": "VEC3"})"))
					.has_value());
		}
		SUBCASE("Positions of the wrong type")
		{
			const auto model =
				ImportText(MakeTriangle("", R"({"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC2"})"));
			REQUIRE_FALSE(model.has_value());
			CHECK(model.error().GetMessageText().contains("POSITION"));
		}
		SUBCASE("glTF 1.0")
		{
			CHECK_FALSE(ImportText(R"({"asset": {"version": "1.0"}})").has_value());
		}
	}

	TEST_CASE("Positions that aren't finite fail to import")
	{
		std::vector<float> floats = {0, 0, 0, 1, 0, 0, 0, std::numeric_limits<float>::infinity(), 0};
		std::vector<uint8_t> bytes(floats.size() * sizeof(float));
		std::memcpy(bytes.data(), floats.data(), bytes.size());
		const std::string text = fmt::format(R"({{"asset": {{"version": "2.0"}},
			"buffers": [{{"uri": "data:application/octet-stream;base64,{}", "byteLength": 36}}],
			"bufferViews": [{{"buffer": 0, "byteLength": 36}}],
			"accessors": [{{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}}],
			"meshes": [{{"primitives": [{{"attributes": {{"POSITION": 0}}}}]}}]}})",
			EncodeBase64(bytes));

		const auto model = ImportText(text);

		REQUIRE_FALSE(model.has_value());
		CHECK(model.error().GetMessageText().contains("finite"));
	}

	TEST_CASE("Indices that refer to missing vertices fail to import")
	{
		std::vector<uint8_t> bytes(36 + 6, 0);
		const std::vector<uint16_t> indices = {0, 1, 7};
		std::memcpy(bytes.data() + 36, indices.data(), 6);
		const std::string text = fmt::format(R"({{"asset": {{"version": "2.0"}},
			"buffers": [{{"uri": "data:application/octet-stream;base64,{}", "byteLength": 42}}],
			"bufferViews": [{{"buffer": 0, "byteLength": 36}}, {{"buffer": 0, "byteOffset": 36, "byteLength": 6}}],
			"accessors": [{{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}},
				{{"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}}],
			"meshes": [{{"primitives": [{{"attributes": {{"POSITION": 0}}, "indices": 1}}]}}]}})",
			EncodeBase64(bytes));

		CHECK_FALSE(ImportText(text).has_value());
	}

	TEST_CASE("Limits count all a model makes: meshes sharing data, and the vertices of flat shading")
	{
		// Two meshes drawing the same two triangles, without normals: 12 indices, and 12 flat-shaded vertices made
		// from 3 in the file
		std::vector<float> floats = {0, 0, 0, 1, 0, 0, 0, 1, 0};
		std::vector<uint8_t> bytes(36 + 12, 0);
		std::memcpy(bytes.data(), floats.data(), 36);
		const std::vector<uint16_t> indices = {0, 1, 2, 0, 2, 1};
		std::memcpy(bytes.data() + 36, indices.data(), 12);
		const std::string text = fmt::format(R"({{"asset": {{"version": "2.0"}},
			"buffers": [{{"uri": "data:application/octet-stream;base64,{}", "byteLength": 48}}],
			"bufferViews": [{{"buffer": 0, "byteLength": 36}}, {{"buffer": 0, "byteOffset": 36, "byteLength": 12}}],
			"accessors": [{{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}},
				{{"bufferView": 1, "componentType": 5123, "count": 6, "type": "SCALAR"}}],
			"meshes": [{{"primitives": [{{"attributes": {{"POSITION": 0}}, "indices": 1}}]}},
				{{"primitives": [{{"attributes": {{"POSITION": 0}}, "indices": 1}}]}}]}})",
			EncodeBase64(bytes));
		const std::vector<uint8_t> model(text.begin(), text.end());
		const auto import = [&model](const ModelImportOptions& options)
		{ return ModelImporter::Import(model, {}, options); };

		const auto fits = import({.MaxVertexCount = 12, .MaxIndexCount = 12});
		REQUIRE_MESSAGE(fits.has_value(), Testing::DescribeError(fits));
		CHECK(fits->Meshes.size() == 2);
		CHECK(fits->Meshes[1].Vertices.size() == 6);

		const auto tooManyIndices = import({.MaxIndexCount = 11});
		REQUIRE_FALSE(tooManyIndices.has_value());
		CHECK(tooManyIndices.error().GetCode() == ErrorCode::Unsupported);
		CHECK(tooManyIndices.error().GetMessageText().contains("indices"));
		const auto tooManyVertices = import({.MaxVertexCount = 11});
		REQUIRE_FALSE(tooManyVertices.has_value());
		CHECK(tooManyVertices.error().GetCode() == ErrorCode::Unsupported);
		CHECK(tooManyVertices.error().GetMessageText().contains("vertices"));
		// Limits beyond what 32-bit indices address are refused
		const auto unaddressable = import({.MaxIndexCount = 1ull << 33});
		REQUIRE_FALSE(unaddressable.has_value());
		CHECK(unaddressable.error().GetCode() == ErrorCode::InvalidArgument);
	}

	TEST_CASE("Images count toward a limit over the whole model, and can be left out")
	{
		const auto png = Image(4, 4).EncodePng();
		REQUIRE(png.has_value());
		const std::string uri = "data:image/png;base64," + EncodeBase64(*png);
		// Two textures, each with an image of its own: 32 pixels
		const std::string text = MakeTriangle(fmt::format(R"("images": [{{"uri": "{0}"}}, {{"uri": "{0}"}}],
			"textures": [{{"source": 0}}, {{"source": 1}}],
			"materials": [{{"pbrMetallicRoughness": {{"baseColorTexture": {{"index": 0}}}},
				"emissiveTexture": {{"index": 1}}}}],)",
			uri));
		const std::vector<uint8_t> model(text.begin(), text.end());

		const auto fits = ModelImporter::Import(model, {}, {.MaxImagePixelCount = 32});
		REQUIRE_MESSAGE(fits.has_value(), Testing::DescribeError(fits));
		CHECK(fits->Images.size() == 2);
		const auto tooMany = ModelImporter::Import(model, {}, {.MaxImagePixelCount = 31});
		REQUIRE_FALSE(tooMany.has_value());
		CHECK(tooMany.error().GetCode() == ErrorCode::Unsupported);

		// Without textures, nothing is decoded and the materials refer to none
		const auto withoutTextures = ModelImporter::Import(model, {}, {.MaxImagePixelCount = 0, .LoadTextures = false});
		REQUIRE_MESSAGE(withoutTextures.has_value(), Testing::DescribeError(withoutTextures));
		CHECK(withoutTextures->Images.empty());
		CHECK(withoutTextures->Textures.empty());
		// The material, and the default one the triangle gets for having none
		REQUIRE(withoutTextures->Materials.size() == 2);
		CHECK_FALSE(withoutTextures->Materials[0].BaseColorTexture.has_value());
		CHECK_FALSE(withoutTextures->Materials[0].EmissiveTexture.has_value());
	}

	TEST_CASE("A model with more nodes than allowed fails to import")
	{
		// Hierarchy.gltf has 5 nodes
		CHECK(ModelImporter::Import(GetModelPath("Hierarchy.gltf"), {.MaxNodeCount = 5}).has_value());
		const auto model = ModelImporter::Import(GetModelPath("Hierarchy.gltf"), {.MaxNodeCount = 4});
		REQUIRE_FALSE(model.has_value());
		CHECK(model.error().GetCode() == ErrorCode::Unsupported);
	}

}
