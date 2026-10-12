#include "Benchmark/BenchmarkScene.h"

#include "Lodestone/Asset/ModelImporter.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/RenderComponents.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <spdlog/fmt/fmt.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace Lodestone::Benchmark {

	namespace {

		constexpr UUID PrimitivesId(0x4000000000004000ull, 0x8000000000000001ull);
		constexpr UUID SpheresId(0x4000000000004000ull, 0x8000000000000002ull);
		constexpr UUID TexturedCubeId(0x4000000000004000ull, 0x8000000000000003ull);
		constexpr UUID AlphaModesId(0x4000000000004000ull, 0x8000000000000004ull);
		constexpr UUID SkyId(0x4000000000004000ull, 0x8000000000000005ull);

		// Meshes of Primitives.glb and AlphaModes.glb
		constexpr uint32_t Plane = 0;
		constexpr uint32_t Cube = 1;
		constexpr uint32_t Sphere = 2;
		constexpr uint32_t MaskedQuad = 1;
		constexpr uint32_t BlendedQuad = 2;

		constexpr int BlockCount = 6;
		constexpr float BlockSpacing = 12.0f;
		constexpr float CameraRadius = 34.0f;

		// The same numbers from 0 to 1 every run (a 32-bit hash of an integer)
		float Hash(uint32_t value)
		{
			value ^= value >> 16u;
			value *= 0x7FEB352Du;
			value ^= value >> 15u;
			value *= 0x846CA68Bu;
			value ^= value >> 16u;
			return static_cast<float>(value & 0xFFFFFFu) / static_cast<float>(0x1000000u);
		}

		Entity AddMesh(Scene& scene, std::string_view name, UUID model, uint32_t mesh, const glm::vec3& position,
			const glm::vec3& scale = glm::vec3(1.0f), float yawDegrees = 0.0f)
		{
			Entity entity = scene.CreateEntity(name);
			TransformComponent& transform = entity.GetTransform();
			transform.Position = position;
			transform.Scale = scale;
			transform.Rotation = glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
			entity.Add<MeshRendererComponent>(MeshRendererComponent{.Model = model, .Mesh = mesh});
			return entity;
		}

		// A block of four buildings around a small square with a lamp, a crate and a fence
		void AddBlock(Scene& scene, int blockX, int blockZ, uint32_t& seed)
		{
			// The blocks are centered on the origin
			const float first = -static_cast<float>(BlockCount - 1) * 0.5f;
			const glm::vec3 center((first + static_cast<float>(blockX)) * BlockSpacing, 0.0f,
				(first + static_cast<float>(blockZ)) * BlockSpacing);
			const std::array<glm::vec2, 4> corners = {
				glm::vec2(-3.0f, -3.0f), glm::vec2(3.0f, -3.0f), glm::vec2(-3.0f, 3.0f), glm::vec2(3.0f, 3.0f)};
			for (const glm::vec2 corner : corners)
			{
				const float height = 2.0f + 8.0f * Hash(seed++);
				const float width = 2.5f + 1.5f * Hash(seed++);
				AddMesh(scene, "Building", PrimitivesId, Cube, center + glm::vec3(corner.x, height * 0.5f, corner.y),
					glm::vec3(width, height, width));
				// A ball on the roof, in one of the material spheres' materials
				const auto material = static_cast<uint32_t>(Hash(seed++) * 25.0f) % 25;
				AddMesh(scene, "Ornament", SpheresId, material, center + glm::vec3(corner.x, height + 0.5f, corner.y));
			}

			AddMesh(scene, "Crate", TexturedCubeId, 0, center + glm::vec3(0.6f, 0.4f, 0.0f), glm::vec3(0.8f),
				Hash(seed++) * 90.0f);
			for (int panel = 0; panel < 3; ++panel)
			{
				const float x = static_cast<float>(panel - 1) * 1.1f;
				AddMesh(scene, "Fence", AlphaModesId, panel == 1 ? BlendedQuad : MaskedQuad,
					center + glm::vec3(x, 0.5f, -1.2f));
			}

			Entity lamp = scene.CreateEntity("Lamp");
			lamp.GetTransform().Position = center + glm::vec3(-0.8f, 2.0f, 0.8f);
			const glm::vec3 color(0.5f + 0.5f * Hash(seed++), 0.5f + 0.5f * Hash(seed++), 0.5f + 0.5f * Hash(seed++));
			lamp.Add<PointLightComponent>(PointLightComponent{.Color = color, .Intensity = 25.0f, .Range = 8.0f});

			Entity spot = scene.CreateEntity("Spot");
			spot.GetTransform().Position = center + glm::vec3(0.0f, 6.0f, 6.0f);
			spot.GetTransform().Rotation = glm::angleAxis(glm::radians(-70.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			spot.Add<SpotLightComponent>(SpotLightComponent{.Intensity = 60.0f, .Range = 12.0f});
		}

	}

	void BuildScene(Scene& scene)
	{
		Entity settings = scene.CreateEntity("Settings");
		settings.Add<EnvironmentComponent>(EnvironmentComponent{.Environment = SkyId});
		settings.Add<RenderSettingsComponent>();

		Entity sun = scene.CreateEntity("Sun");
		sun.GetTransform().Rotation = glm::angleAxis(glm::radians(35.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::angleAxis(glm::radians(-50.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		sun.Add<DirectionalLightComponent>();

		const float groundSize = static_cast<float>(BlockCount) * BlockSpacing + 20.0f;
		AddMesh(scene, "Ground", PrimitivesId, Plane, glm::vec3(0.0f), glm::vec3(groundSize));

		uint32_t seed = 1;
		for (int z = 0; z < BlockCount; ++z)
		{
			for (int x = 0; x < BlockCount; ++x)
				AddBlock(scene, x, z, seed);
		}

		// A row of every material in the middle of the square
		for (uint32_t material = 0; material < 25; ++material)
			AddMesh(scene, "Sample", SpheresId, material,
				glm::vec3((static_cast<float>(material) - 12.0f) * 1.2f, 0.6f, 0.0f));
	}

	RenderAssetSource MakeAssetSource(const std::filesystem::path& fixtureDirectory)
	{
		const auto directory = std::make_shared<const std::filesystem::path>(fixtureDirectory);
		return {
			.LoadModel = [directory](UUID id) -> std::expected<Model, Error>
			{
				const std::array<std::pair<UUID, std::string_view>, 4> models = {{
					{PrimitivesId, "Primitives.glb"},
					{SpheresId, "MaterialSpheres.glb"},
					{TexturedCubeId, "TexturedCube.gltf"},
					{AlphaModesId, "AlphaModes.glb"},
				}};
				for (const auto& [modelId, file] : models)
				{
					if (id == modelId)
						return ModelImporter::Import(*directory / "Models" / file);
				}
				return std::unexpected(Error(ErrorCode::NotFound, fmt::format("No model has the ID {}", id)));
			},
			.LoadEnvironment = [directory](UUID id) -> std::expected<HdrImage, Error>
			{
				if (id == SkyId)
					return HdrImage::Load(*directory / "Environments" / "Sky.hdr");
				return std::unexpected(Error(ErrorCode::NotFound, fmt::format("No environment has the ID {}", id)));
			},
		};
	}

	RenderView GetCameraView(float time)
	{
		const float angle = time * 2.0f * glm::pi<float>();
		const float height = 9.0f + 5.0f * std::sin(angle * 2.0f);
		const glm::vec3 position(std::cos(angle) * CameraRadius, height, std::sin(angle) * CameraRadius);
		return {
			.View = glm::lookAtRH(position, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
			.FieldOfView = glm::radians(60.0f),
			.NearPlane = 0.1f,
			.FarPlane = 300.0f,
		};
	}

}
