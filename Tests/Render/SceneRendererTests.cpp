#include "Lodestone/Renderer/SceneRenderer.h"

#include "Common/DescribeError.h"
#include "Common/LogCapture.h"
#include "Common/ReferenceImage.h"
#include "Lodestone/Animation/AnimationComponents.h"
#include "Lodestone/Animation/ScenePose.h"
#include "Lodestone/Asset/ModelImporter.h"
#include "Lodestone/Graphics/GraphicsDevice.h"
#include "Lodestone/Graphics/TextureReadback.h"
#include "Lodestone/Renderer/ImageBasedLighting.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/EntitySerializer.h"
#include "Lodestone/Scene/ModelEntities.h"
#include "Lodestone/Scene/Scene.h"
#include "Render/RenderTestDevice.h"

#include <doctest/doctest.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The scene renderer, end to end: a reference image for each feature on lavapipe, and checks that hold on any driver

namespace Lodestone {

	namespace {

		constexpr glm::uvec2 ImageSize{256, 256};
		// For checks that only need a few pixels
		constexpr glm::uvec2 SmallImageSize{128, 128};

		constexpr UUID PrimitivesId(0x1000000000004000ull, 0x8000000000000001ull);
		constexpr UUID SpheresId(0x1000000000004000ull, 0x8000000000000002ull);
		constexpr UUID TexturedCubeId(0x1000000000004000ull, 0x8000000000000003ull);
		constexpr UUID AlphaModesId(0x1000000000004000ull, 0x8000000000000004ull);
		constexpr UUID SkyId(0x1000000000004000ull, 0x8000000000000005ull);
		// Photographed HDRIs from Poly Haven
		constexpr UUID OutdoorId(0x1000000000004000ull, 0x8000000000000006ull);
		constexpr UUID StudioId(0x1000000000004000ull, 0x8000000000000007ull);
		constexpr UUID SunsetId(0x1000000000004000ull, 0x8000000000000008ull);
		constexpr UUID SkinnedArmId(0x1000000000004000ull, 0x8000000000000009ull);
		constexpr UUID MissingId(0x1000000000004000ull, 0x80000000000000FFull);

		// Meshes of Primitives.glb
		constexpr uint32_t Plane = 0;
		constexpr uint32_t Cube = 1;
		constexpr uint32_t Sphere = 2;
		// Meshes of MaterialSpheres.glb, whose metalness rises with the column and roughness with the row
		constexpr uint32_t PolishedMetal = 4;
		constexpr uint32_t SatinMetal = 14;
		constexpr uint32_t RoughDielectric = 20;

		// Where the sun shines, before it's made unit length
		constexpr glm::vec3 SunDirection{-0.5f, -1.0f, -0.35f};

		std::filesystem::path GetFixture(std::string_view directory, std::string_view file)
		{
			return std::filesystem::path(LS_TEST_FIXTURE_DIR) / directory / file;
		}

		// The test fixtures, by the IDs above
		RenderAssetSource MakeFixtureSource()
		{
			return {
				.LoadModel = [](UUID id) -> std::expected<Model, Error>
				{
					const std::array<std::pair<UUID, std::string_view>, 5> models = {{
						{PrimitivesId, "Primitives.glb"},
						{SpheresId, "MaterialSpheres.glb"},
						{TexturedCubeId, "TexturedCube.gltf"},
						{AlphaModesId, "AlphaModes.glb"},
						{SkinnedArmId, "SkinnedArm.glb"},
					}};
					for (const auto& [modelId, file] : models)
					{
						if (id == modelId)
							return ModelImporter::Import(GetFixture("Models", file));
					}
					return std::unexpected(Error(ErrorCode::NotFound, fmt::format("No model has the ID {}", id)));
				},
				.LoadEnvironment = [](UUID id) -> std::expected<HdrImage, Error>
				{
					const std::array<std::pair<UUID, std::string_view>, 4> environments = {{
						{SkyId, "Sky.hdr"},
						{OutdoorId, "PolyHaven/kloofendal_48d_partly_cloudy_puresky_1k.hdr"},
						{StudioId, "PolyHaven/studio_small_09_1k.hdr"},
						{SunsetId, "PolyHaven/venice_sunset_1k.hdr"},
					}};
					for (const auto& [environmentId, file] : environments)
					{
						if (id == environmentId)
							return HdrImage::Load(GetFixture("Environments", file));
					}
					return std::unexpected(Error(ErrorCode::NotFound, fmt::format("No environment has the ID {}", id)));
				},
			};
		}

		glm::mat4 Place(const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f), float yawDegrees = 0.0f)
		{
			return glm::translate(glm::mat4(1.0f), position) *
				glm::rotate(glm::mat4(1.0f), glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f)) *
				glm::scale(glm::mat4(1.0f), scale);
		}

		RenderView MakeView(const glm::vec3& eye, const glm::vec3& target, float fieldOfViewDegrees = 50.0f)
		{
			return {
				.View = glm::lookAtRH(eye, target, glm::vec3(0.0f, 1.0f, 0.0f)),
				.FieldOfView = glm::radians(fieldOfViewDegrees),
				.NearPlane = 0.1f,
				.FarPlane = 100.0f,
			};
		}

		// A world with a sun that casts shadows, lit by the sky environment
		RenderWorld MakeSunlitWorld()
		{
			RenderWorld world;
			world.DirectionalLights.push_back({.Direction = glm::normalize(SunDirection), .Color = glm::vec3(3.0f)});
			world.Environment.Environment = SkyId;
			return world;
		}

		void AddGround(RenderWorld& world, float size = 12.0f)
		{
			world.Meshes.push_back(
				{.Model = PrimitivesId, .Mesh = Plane, .World = Place(glm::vec3(0.0f), glm::vec3(size))});
		}

		// SkinnedArm.glb on the CPU, which poses scenes; the renderer loads its own copy
		const Model* FindArm(UUID id)
		{
			static const Model Arm = []
			{
				auto imported = ModelImporter::Import(GetFixture("Models", "SkinnedArm.glb"));
				REQUIRE_MESSAGE(imported.has_value(), Testing::DescribeError(imported));
				return std::move(*imported);
			}();
			return id == SkinnedArmId ? &Arm : nullptr;
		}

		// SkinnedArm.glb placed in a scene, its Bend clip held at a time: the column's top half turns a quarter turn
		// about Z over the clip's second, leaning toward -X
		Entity PlaceArm(Scene& scene, const glm::vec3& position, float time)
		{
			auto root = EntitySerializer::InstantiateTree(scene,
				BuildModelEntities(*FindArm(SkinnedArmId), SkinnedArmId, "Arm"),
				EntitySerializer::IdPolicy::Regenerate);
			REQUIRE_MESSAGE(root.has_value(), Testing::DescribeError(root));
			root->GetTransform().Position = position;
			auto& animator = root->Get<AnimatorComponent>();
			animator.Time = time;
			animator.Loop = false;
			return *root;
		}

		// What the renderer draws of a scene, posed by its animators
		RenderWorld ExtractPosed(const Scene& scene)
		{
			return ExtractRenderWorld(ScenePose(scene, {.Models = FindArm}));
		}

		size_t CountMessages(const Testing::LogCapture& log, std::string_view text)
		{
			const std::vector<std::string> messages = log.GetMessages();
			return static_cast<size_t>(
				std::ranges::count_if(messages, [text](const std::string& message) { return message.contains(text); }));
		}

		float Brightness(Rgba8 pixel)
		{
			return (static_cast<float>(pixel.R) + static_cast<float>(pixel.G) + static_cast<float>(pixel.B)) / 3.0f;
		}

		float Brightness(const glm::vec3& color)
		{
			return (color.r + color.g + color.b) / 3.0f;
		}

		// The average brightness of a square of pixels around a point
		float AreaBrightness(const Image& image, uint32_t centerX, uint32_t centerY, uint32_t radius = 3)
		{
			float sum = 0.0f;
			uint32_t count = 0;
			for (uint32_t y = centerY - radius; y <= centerY + radius; ++y)
			{
				for (uint32_t x = centerX - radius; x <= centerX + radius; ++x)
				{
					sum += Brightness(image.GetPixel(x, y));
					++count;
				}
			}
			return sum / static_cast<float>(count);
		}

		// The average color (0 to 255 per channel) of a square of pixels around a point
		glm::vec3 AreaColor(const Image& image, uint32_t centerX, uint32_t centerY, uint32_t radius)
		{
			glm::vec3 sum(0.0f);
			uint32_t count = 0;
			for (uint32_t y = centerY - radius; y <= centerY + radius; ++y)
			{
				for (uint32_t x = centerX - radius; x <= centerX + radius; ++x)
				{
					const Rgba8 pixel = image.GetPixel(x, y);
					sum += glm::vec3(pixel.R, pixel.G, pixel.B);
					++count;
				}
			}
			return sum / static_cast<float>(count);
		}

		// A device, a scene renderer whose assets are the test fixtures, and a target to render into
		class RendererFixture
		{
		public:
			explicit RendererFixture(glm::uvec2 size = ImageSize)
			{
				auto device = GraphicsDevice::Create(Testing::MakeRenderTestDeviceConfig());
				REQUIRE_MESSAGE(device.has_value(), Testing::DescribeError(device));
				m_Device = std::move(*device);
				// Small shadow maps keep software rendering quick
				auto renderer = SceneRenderer::Create(*m_Device, {.ShadowMapSize = 1024});
				REQUIRE_MESSAGE(renderer.has_value(), Testing::DescribeError(renderer));
				m_Renderer = std::move(*renderer);
				m_Renderer->GetAssets().SetSource(MakeFixtureSource());
				auto target = m_Renderer->CreateTarget(size, "Scene renderer test");
				REQUIRE_MESSAGE(target.has_value(), Testing::DescribeError(target));
				m_Target = std::move(*target);
				m_CommandList = m_Device->GetNvrhiDevice()->createCommandList();
				REQUIRE(m_CommandList);
			}

			RendererFixture(const RendererFixture&) = delete;
			RendererFixture& operator=(const RendererFixture&) = delete;
			RendererFixture(RendererFixture&&) = delete;
			RendererFixture& operator=(RendererFixture&&) = delete;

			~RendererFixture() { CHECK(m_Device->GetValidationErrorCount() == 0); }

			// Renders a frame and reads it back
			Image Render(const RenderWorld& world, const RenderView& view)
			{
				nvrhi::IDevice* device = m_Device->GetNvrhiDevice();
				m_CommandList->open();
				m_Renderer->Render(m_CommandList, *m_Target, world, view);
				m_CommandList->close();
				device->executeCommandList(m_CommandList);
				auto image = ReadTexture(device, m_Target->GetColor());
				REQUIRE_MESSAGE(image.has_value(), Testing::DescribeError(image));
				device->runGarbageCollection();
				return std::move(*image);
			}

			SceneRenderer& GetRenderer() { return *m_Renderer; }
			const RenderStats& GetStats() const { return m_Target->GetStats(); }
			const GraphicsDevice& GetDevice() const { return *m_Device; }

		private:
			Scope<GraphicsDevice> m_Device;
			Scope<SceneRenderer> m_Renderer;
			Scope<SceneRenderTarget> m_Target;
			nvrhi::CommandListHandle m_CommandList;
		};

	}

	TEST_CASE("Metalness and roughness shape reflections of the environment and the sun" *
		doctest::test_suite("ReferenceImages"))
	{
		RenderWorld world = MakeSunlitWorld();
		for (uint32_t row = 0; row < 5; ++row)
		{
			for (uint32_t column = 0; column < 5; ++column)
			{
				const glm::vec3 position(
					(static_cast<float>(column) - 2.0f) * 1.2f, (static_cast<float>(row) - 2.0f) * 1.2f, 0.0f);
				world.Meshes.push_back({.Model = SpheresId, .Mesh = row * 5 + column, .World = Place(position)});
			}
		}
		RendererFixture fixture;

		const Image image = fixture.Render(world, MakeView(glm::vec3(0.0f, 0.0f, 9.5f), glm::vec3(0.0f), 45.0f));

		Testing::CheckReferenceImage("PbrSpheres", image);
	}

	TEST_CASE("Textures give a model its base color, metalness, roughness and surface detail" *
		doctest::test_suite("ReferenceImages"))
	{
		RenderWorld world = MakeSunlitWorld();
		world.Meshes.push_back(
			{.Model = TexturedCubeId, .World = Place(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.6f), 30.0f)});
		RendererFixture fixture;

		const Image image = fixture.Render(world, MakeView(glm::vec3(2.2f, 2.0f, 3.2f), glm::vec3(0.0f)));

		Testing::CheckReferenceImage("TexturedCube", image);
	}

	TEST_CASE("Shadows are sharp where casters touch the ground and soften with distance" *
		doctest::test_suite("ReferenceImages"))
	{
		// A low sun casts a pillar's shadow far across the ground: sharp at its foot, blurred toward its tip. A sphere
		// floats above the ground, its shadow soft all round. Dim ambient light lets the shadows show
		RenderWorld world = MakeSunlitWorld();
		world.DirectionalLights[0].Direction = glm::normalize(glm::vec3(0.9f, -0.8f, -0.7f));
		world.Environment.Intensity = 0.4f;
		AddGround(world);
		world.Meshes.push_back({.Model = PrimitivesId,
			.Mesh = Cube,
			.World = Place(glm::vec3(-1.5f, 1.25f, 0.8f), glm::vec3(0.25f, 2.5f, 0.25f))});
		world.Meshes.push_back(
			{.Model = PrimitivesId, .Mesh = Sphere, .World = Place(glm::vec3(0.6f, 1.2f, 0.8f), glm::vec3(0.7f))});
		world.Settings.ShadowSoftness = 2.0f;
		RendererFixture fixture;

		const Image image = fixture.Render(world, MakeView(glm::vec3(0.0f, 5.0f, 6.5f), glm::vec3(0.3f, 0.0f, -0.8f)));

		Testing::CheckReferenceImage("SoftShadows", image);
	}

	TEST_CASE("Ambient occlusion darkens creases and corners" * doctest::test_suite("ReferenceImages"))
	{
		// Lit by the environment alone, which is the light ambient occlusion darkens: a corner of two walls, a cube in
		// it, and a sphere resting on the ground
		RenderWorld world;
		world.Environment.Environment = SkyId;
		AddGround(world);
		world.Meshes.push_back({.Model = PrimitivesId,
			.Mesh = Cube,
			.World = Place(glm::vec3(0.0f, 1.0f, -1.6f), glm::vec3(4.0f, 2.0f, 0.2f))});
		world.Meshes.push_back({.Model = PrimitivesId,
			.Mesh = Cube,
			.World = Place(glm::vec3(-1.9f, 1.0f, 0.0f), glm::vec3(0.2f, 2.0f, 3.0f))});
		world.Meshes.push_back({.Model = PrimitivesId, .Mesh = Cube, .World = Place(glm::vec3(-1.3f, 0.5f, -1.0f))});
		world.Meshes.push_back({.Model = PrimitivesId, .Mesh = Sphere, .World = Place(glm::vec3(0.6f, 0.5f, -0.2f))});
		RendererFixture fixture;

		const Image image = fixture.Render(world, MakeView(glm::vec3(1.8f, 2.2f, 3.2f), glm::vec3(-0.6f, 0.4f, -0.8f)));

		Testing::CheckReferenceImage("AmbientOcclusion", image);
	}

	TEST_CASE("Point and spot lights fall off to nothing at their range" * doctest::test_suite("ReferenceImages"))
	{
		RenderWorld world;
		world.Environment.Environment = SkyId;
		world.Environment.Intensity = 0.05f;
		world.Environment.ShowSky = false;
		AddGround(world);
		world.Meshes.push_back({.Model = PrimitivesId, .Mesh = Sphere, .World = Place(glm::vec3(0.0f, 0.5f, 0.0f))});
		world.PunctualLights.push_back(
			{.Position = glm::vec3(-1.5f, 1.0f, 0.5f), .Color = glm::vec3(6.0f, 1.5f, 1.0f), .Range = 4.0f});
		world.PunctualLights.push_back({
			.Position = glm::vec3(1.5f, 3.0f, 0.0f),
			.Color = glm::vec3(4.0f, 8.0f, 20.0f),
			.Range = 6.0f,
			.Spot = true,
			.Direction = glm::normalize(glm::vec3(0.2f, -1.0f, 0.0f)),
			.CosInnerAngle = std::cos(glm::radians(15.0f)),
			.CosOuterAngle = std::cos(glm::radians(25.0f)),
		});
		RendererFixture fixture;

		const Image image = fixture.Render(world, MakeView(glm::vec3(0.0f, 5.0f, 6.0f), glm::vec3(0.0f, 0.0f, 0.0f)));

		Testing::CheckReferenceImage("PunctualLights", image);
	}

	TEST_CASE("Opaque, alpha-tested, blended and double-sided surfaces" * doctest::test_suite("ReferenceImages"))
	{
		RenderWorld world = MakeSunlitWorld();
		world.Environment.ShowSky = false;
		// A backdrop, to see what's behind the cut-out and blended quads
		world.Meshes.push_back({.Model = PrimitivesId,
			.Mesh = Cube,
			.World = Place(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(6.0f, 2.0f, 0.2f))});
		for (uint32_t index = 0; index < 4; ++index)
		{
			const glm::vec3 position((static_cast<float>(index) - 1.5f) * 1.2f, 0.0f, 0.0f);
			// The double-sided quad is turned to show its back
			world.Meshes.push_back({.Model = AlphaModesId,
				.Mesh = index,
				.World = Place(position, glm::vec3(1.0f), index == 3 ? 150.0f : 0.0f)});
		}
		RendererFixture fixture;

		const Image image = fixture.Render(world, MakeView(glm::vec3(0.0f, 0.5f, 4.5f), glm::vec3(0.0f, 0.0f, 0.0f)));

		Testing::CheckReferenceImage("AlphaModes", image);
	}

	TEST_CASE("The sky shows the environment behind everything, turned by its rotation" *
		doctest::test_suite("ReferenceImages"))
	{
		RenderWorld world;
		world.Environment.Environment = SkyId;
		world.Environment.Rotation = 30.0f;
		world.Meshes.push_back({.Model = PrimitivesId, .Mesh = Sphere, .World = Place(glm::vec3(0.0f, 0.0f, 0.0f))});
		RendererFixture fixture;

		const Image image =
			fixture.Render(world, MakeView(glm::vec3(0.0f, 0.3f, 2.5f), glm::vec3(0.6f, 0.4f, 0.0f), 90.0f));

		Testing::CheckReferenceImage("Sky", image);
	}

	TEST_CASE("Each tonemapper brings bright light into range its own way" * doctest::test_suite("ReferenceImages"))
	{
		RenderWorld world = MakeSunlitWorld();
		world.DirectionalLights[0].Color = glm::vec3(12.0f);
		AddGround(world);
		world.Meshes.push_back({.Model = PrimitivesId, .Mesh = Sphere, .World = Place(glm::vec3(0.0f, 0.5f, 0.0f))});
		world.Meshes.push_back(
			{.Model = SpheresId, .Mesh = 4, .World = Place(glm::vec3(1.2f, 0.5f, 0.0f), glm::vec3(1.6f))});
		RendererFixture fixture(glm::uvec2(192, 128));
		const RenderView view = MakeView(glm::vec3(0.5f, 1.6f, 3.0f), glm::vec3(0.5f, 0.4f, 0.0f));

		for (const auto& [tonemapper, name] : {std::pair(Tonemapper::Aces, "TonemapAces"),
				 std::pair(Tonemapper::Neutral, "TonemapNeutral"), std::pair(Tonemapper::None, "TonemapNone")})
		{
			CAPTURE(name);
			world.Settings.Tonemapper = static_cast<uint32_t>(tonemapper);
			Testing::CheckReferenceImage(name, fixture.Render(world, view));
		}
	}

	TEST_CASE("Photographed environments light the scene and fill the sky: outdoors, a studio and a sunset" *
		doctest::test_suite("ReferenceImages"))
	{
		// Lit by the environment alone: polished metal mirrors it, satin metal blurs it, and a rough dielectric shows
		// its diffuse light
		RenderWorld world;
		world.Meshes.push_back(
			{.Model = SpheresId, .Mesh = PolishedMetal, .World = Place(glm::vec3(-1.2f, 0.0f, 0.0f))});
		world.Meshes.push_back({.Model = SpheresId, .Mesh = SatinMetal, .World = Place(glm::vec3(0.0f))});
		world.Meshes.push_back(
			{.Model = SpheresId, .Mesh = RoughDielectric, .World = Place(glm::vec3(1.2f, 0.0f, 0.0f))});
		RendererFixture fixture(glm::uvec2(192, 128));
		const RenderView view = MakeView(glm::vec3(0.0f, 0.5f, 3.4f), glm::vec3(0.0f, 0.2f, 0.0f), 60.0f);

		for (const auto& [environment, name] : {std::pair(OutdoorId, "EnvironmentOutdoor"),
				 std::pair(StudioId, "EnvironmentStudio"), std::pair(SunsetId, "EnvironmentSunset")})
		{
			CAPTURE(name);
			world.Environment.Environment = environment;
			Testing::CheckReferenceImage(name, fixture.Render(world, view));
		}
	}

	TEST_CASE("Skinned meshes bend with their joints, cast shadows and are shaded like any other" *
		doctest::test_suite("ReferenceImages"))
	{
		// Three arms bending a quarter turn: at its start, halfway and at its end
		Scene scene;
		PlaceArm(scene, glm::vec3(-2.0f, 0.0f, 0.0f), 0.0f);
		PlaceArm(scene, glm::vec3(0.0f), 0.5f);
		PlaceArm(scene, glm::vec3(2.0f, 0.0f, 0.0f), 1.0f);
		RenderWorld world = ExtractPosed(scene);
		REQUIRE(world.Meshes.size() == 3);
		CHECK(world.JointMatrices.size() == 6);
		world.DirectionalLights.push_back(
			{.Direction = glm::normalize(glm::vec3(0.6f, -1.0f, -0.5f)), .Color = glm::vec3(3.0f)});
		world.Environment.Environment = SkyId;
		AddGround(world);
		RendererFixture fixture;

		const Image image =
			fixture.Render(world, MakeView(glm::vec3(0.0f, 1.8f, 7.0f), glm::vec3(0.0f, 1.0f, 0.0f), 45.0f));

		Testing::CheckReferenceImage("SkinnedArms", image);
	}

	// These run on every driver, including MoltenVK on macOS, where reference images aren't compared

	TEST_CASE("Skinned meshes cast shadows where their joints move them")
	{
		// The sun nearly overhead, the arm bent a quarter turn: its top half's shadow lies on the ground along -X
		Scene scene;
		PlaceArm(scene, glm::vec3(0.0f), 1.0f);
		RenderWorld world = ExtractPosed(scene);
		world.DirectionalLights.push_back(
			{.Direction = glm::normalize(glm::vec3(0.05f, -1.0f, 0.05f)), .Color = glm::vec3(3.0f)});
		world.Environment.Intensity = 0.2f;
		AddGround(world);
		RendererFixture fixture(SmallImageSize);
		// Looking under the bent arm, at the ground below it
		const RenderView view = MakeView(glm::vec3(-0.6f, 3.0f, 4.0f), glm::vec3(-0.6f, 0.0f, 0.0f));
		const glm::mat4 viewProjection = view.GetProjection(1.0f) * view.View;
		const auto toPixel = [&viewProjection](const glm::vec3& point)
		{
			const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
			const glm::vec2 ndc = glm::vec2(clip) / clip.w;
			return glm::uvec2((ndc.x * 0.5f + 0.5f) * static_cast<float>(SmallImageSize.x),
				(0.5f - ndc.y * 0.5f) * static_cast<float>(SmallImageSize.y));
		};

		const Image image = fixture.Render(world, view);

		CHECK(fixture.GetStats().PassDrawCalls[static_cast<size_t>(RenderPass::Shadows)] > 0);
		const glm::uvec2 shadowed = toPixel(glm::vec3(-0.6f, 0.0f, 0.0f));
		const glm::uvec2 lit = toPixel(glm::vec3(-0.6f, 0.0f, 1.0f));
		const float shadowedBrightness = AreaBrightness(image, shadowed.x, shadowed.y, 2);
		const float litBrightness = AreaBrightness(image, lit.x, lit.y, 2);
		CAPTURE(shadowedBrightness);
		CAPTURE(litBrightness);
		CHECK(shadowedBrightness < 0.7f * litBrightness);
	}

	TEST_CASE("Skinned meshes are drawn, and culled, where their joints move them")
	{
		// At its clip's end the arm's top half lies along -X, a meter up. The view sees only there: the arm's bind
		// pose, upright, is out of it
		Scene scene;
		Entity arm = PlaceArm(scene, glm::vec3(0.0f), 1.0f);
		RendererFixture fixture(glm::uvec2(64, 64));
		const RenderView view = MakeView(glm::vec3(-0.8f, 1.0f, 5.0f), glm::vec3(-0.8f, 1.0f, 0.0f), 8.0f);

		const Image bent = fixture.Render(ExtractPosed(scene), view);

		CHECK(fixture.GetStats().VisibleMeshes == 1);
		// The arm's orange, not the grey sky
		const glm::vec3 center = AreaColor(bent, 32, 32, 2);
		CAPTURE(center.r);
		CAPTURE(center.b);
		CHECK(center.r > center.b + 30.0f);

		// At the clip's start the arm stands upright, out of view
		arm.Get<AnimatorComponent>().Time = 0.0f;
		const Image upright = fixture.Render(ExtractPosed(scene), view);
		CHECK(fixture.GetStats().VisibleMeshes == 0);
		const glm::vec3 sky = AreaColor(upright, 32, 32, 2);
		CHECK(std::abs(sky.r - sky.b) < 10.0f);
	}

	TEST_CASE("A photographed sunset lights what faces its sun brighter and warmer than what faces away")
	{
		// The sun is the brightest pixel of the photograph, low over the horizon
		const auto photograph = HdrImage::Load(GetFixture("Environments", "PolyHaven/venice_sunset_1k.hdr"));
		REQUIRE_MESSAGE(photograph.has_value(), Testing::DescribeError(photograph));
		glm::uvec2 sunPixel(0);
		float sunLuminance = 0.0f;
		for (uint32_t y = 0; y < photograph->GetHeight(); ++y)
		{
			for (uint32_t x = 0; x < photograph->GetWidth(); ++x)
			{
				const float luminance =
					glm::dot(glm::vec3(photograph->GetPixel(x, y)), glm::vec3(0.2126f, 0.7152f, 0.0722f));
				if (luminance > sunLuminance)
				{
					sunLuminance = luminance;
					sunPixel = {x, y};
				}
			}
		}
		const glm::vec3 sun = EquirectangularToDirection(
			(glm::vec2(sunPixel) + 0.5f) / glm::vec2(photograph->GetWidth(), photograph->GetHeight()));
		CAPTURE(sun.y);
		REQUIRE(std::abs(sun.y) < 0.3f);
		const glm::vec3 towardSun = glm::normalize(glm::vec3(sun.x, 0.0f, sun.z));

		// A grey cube lit by nothing but the sunset, a face turned to the sun, seen head-on from each side. The image
		// keeps the light's linear ratios: no tonemapping, and exposed so nothing clips
		RenderWorld world;
		world.Environment.Environment = SunsetId;
		world.Settings.Tonemapper = static_cast<uint32_t>(Tonemapper::None);
		world.Settings.Exposure = -3.0f;
		world.Meshes.push_back({.Model = PrimitivesId,
			.Mesh = Cube,
			.World = Place(glm::vec3(0.0f), glm::vec3(1.5f), glm::degrees(std::atan2(towardSun.x, towardSun.z)))});
		RendererFixture fixture(glm::uvec2(64, 64));
		const auto faceColor = [&](const glm::vec3& side)
		{
			const Image image = fixture.Render(world, MakeView(side * 3.0f, glm::vec3(0.0f)));
			return AreaColor(image, 32, 32, 6);
		};

		const glm::vec3 lit = faceColor(towardSun);
		const glm::vec3 unlit = faceColor(-towardSun);
		REQUIRE(fixture.GetRenderer().GetAssets().GetError(SunsetId) == nullptr);
		CAPTURE(Brightness(lit));
		CAPTURE(Brightness(unlit));
		CAPTURE(lit.r / lit.b);
		CAPTURE(unlit.r / std::max(unlit.b, 1.0f));
		CHECK(std::max({lit.r, lit.g, lit.b}) < 250.0f);
		CHECK(Brightness(unlit) > 2.0f);
		CHECK(Brightness(lit) > 1.5f * Brightness(unlit));
		// Redder against blue
		CHECK(lit.r / lit.b > 1.2f * (unlit.r / std::max(unlit.b, 1.0f)));
	}

	TEST_CASE("The scene renderer draws what the camera sees, front faces out, and culls the rest")
	{
		RenderWorld world;
		world.DirectionalLights.push_back({.Direction = glm::vec3(0.0f, 0.0f, -1.0f), .Color = glm::vec3(3.0f)});
		world.Environment.ShowSky = false;
		world.Environment.Intensity = 0.0f;
		world.Meshes.push_back({.Model = PrimitivesId, .Mesh = Cube, .World = Place(glm::vec3(0.0f, 0.0f, 0.0f))});
		// Behind the camera, and out of the light's way
		world.Meshes.push_back({.Model = PrimitivesId, .Mesh = Cube, .World = Place(glm::vec3(8.0f, 0.0f, 12.0f))});
		RendererFixture fixture;

		const Image image = fixture.Render(world, MakeView(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f)));

		// The face toward the camera and the light is bright; if back faces were drawn instead, it'd be the far face,
		// which faces away from both
		CHECK(AreaBrightness(image, 128, 128) > 150.0f);
		// Around it, nothing: no sky, no environment light
		CHECK(Brightness(image.GetPixel(4, 4)) < 3.0f);
		const RenderStats& stats = fixture.GetStats();
		CHECK(stats.Meshes == 2);
		CHECK(stats.VisibleMeshes == 1);
		CHECK(stats.PassDrawCalls[static_cast<size_t>(RenderPass::Opaque)] == 1);
		CHECK(stats.PassDrawCalls[static_cast<size_t>(RenderPass::Prepass)] == 1);
		// Ambient occlusion's two passes and tonemapping; no sky
		CHECK(stats.PassDrawCalls[static_cast<size_t>(RenderPass::AmbientOcclusion)] == 2);
		CHECK(stats.PassDrawCalls[static_cast<size_t>(RenderPass::Sky)] == 0);
		CHECK(stats.PassDrawCalls[static_cast<size_t>(RenderPass::Tonemap)] == 1);
		CHECK(stats.Triangles >= 24);
		CHECK(stats.CpuTime > 0.0);
	}

	TEST_CASE("A mirrored mesh looks like its mirror image, not inside out")
	{
		RenderWorld world;
		// The scene is symmetric about x = 0, so mirroring the cube along x mustn't change the image
		world.DirectionalLights.push_back(
			{.Direction = glm::normalize(glm::vec3(0.0f, -1.0f, -1.0f)), .Color = glm::vec3(3.0f)});
		world.Environment.ShowSky = false;
		world.Meshes.push_back({.Model = PrimitivesId, .Mesh = Cube, .World = Place(glm::vec3(0.0f))});
		RendererFixture fixture;
		const RenderView view = MakeView(glm::vec3(0.0f, 2.0f, 3.0f), glm::vec3(0.0f));

		const Image plain = fixture.Render(world, view);
		world.Meshes[0].World = Place(glm::vec3(0.0f), glm::vec3(-1.0f, 1.0f, 1.0f));
		const Image mirrored = fixture.Render(world, view);

		const Testing::ImageDifference difference = Testing::CompareImages(mirrored, plain, 4);
		CHECK(difference.MismatchedPixels < 50);
	}

	TEST_CASE("Shadows and ambient occlusion darken what they should")
	{
		RenderWorld world = MakeSunlitWorld();
		world.DirectionalLights[0].Direction = glm::vec3(0.0f, -1.0f, 0.0f);
		// Dim ambient light, so the sun's light - what shadows take away - dominates
		world.Environment.Intensity = 0.2f;
		AddGround(world);
		world.Meshes.push_back({.Model = PrimitivesId, .Mesh = Cube, .World = Place(glm::vec3(0.0f, 1.5f, 0.0f))});
		RendererFixture fixture;
		// Looking straight down: the cube hides the ground under it, so look beside it, where its shadow falls
		const RenderView view = MakeView(glm::vec3(0.0f, 6.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.5f));
		world.Settings.AmbientOcclusion = false;

		const Image shadowed = fixture.Render(world, view);
		world.Meshes[1].CastShadows = false;
		const Image unshadowed = fixture.Render(world, view);

		// The ground right under the cube's front edge: in shadow, then lit
		const glm::mat4 viewProjection = view.GetProjection(1.0f) * view.View;
		const glm::vec4 clip = viewProjection * glm::vec4(0.0f, 0.0f, 0.35f, 1.0f);
		const auto x = static_cast<uint32_t>((clip.x / clip.w * 0.5f + 0.5f) * ImageSize.x);
		const auto y = static_cast<uint32_t>((0.5f - clip.y / clip.w * 0.5f) * ImageSize.y);
		CHECK(AreaBrightness(shadowed, x, y) < AreaBrightness(unshadowed, x, y) * 0.75f);
		CHECK(fixture.GetStats().ShadowCascades == 4);

		// The cube resting on the ground makes a crease ambient occlusion darkens. Ambient light only, which is what
		// it darkens
		world.Meshes[1].World = Place(glm::vec3(0.0f, 0.5f, 0.0f));
		world.DirectionalLights.clear();
		world.Environment.Intensity = 1.0f;
		const Image withoutOcclusion = fixture.Render(world, view);
		world.Settings.AmbientOcclusion = true;
		world.Settings.AmbientOcclusionIntensity = 2.0f;
		const Image withOcclusion = fixture.Render(world, view);
		const glm::vec4 crease = viewProjection * glm::vec4(0.0f, 0.0f, 0.56f, 1.0f);
		const auto creaseX = static_cast<uint32_t>((crease.x / crease.w * 0.5f + 0.5f) * ImageSize.x);
		const auto creaseY = static_cast<uint32_t>((0.5f - crease.y / crease.w * 0.5f) * ImageSize.y);
		CHECK(
			AreaBrightness(withOcclusion, creaseX, creaseY, 1) < AreaBrightness(withoutOcclusion, creaseX, creaseY, 1));
	}

	TEST_CASE("Exposure brightens the image, and the environment's rotation turns the sky")
	{
		RenderWorld world;
		world.Environment.Environment = SkyId;
		world.Meshes.push_back({.Model = PrimitivesId, .Mesh = Sphere, .World = Place(glm::vec3(0.0f))});
		RendererFixture fixture(SmallImageSize);
		const RenderView view = MakeView(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 1.5f, 0.0f), 70.0f);
		// Sky.hdr's sun is 35 degrees up, 45 degrees from -Z toward +X. Turned 45 degrees, it's straight ahead
		const glm::vec3 turnedSun(0.0f, std::sin(glm::radians(35.0f)), -std::cos(glm::radians(35.0f)));
		const glm::vec4 clip = view.GetProjection(1.0f) * view.View * glm::vec4(view.GetPosition() + turnedSun, 1.0f);
		const auto sunX = static_cast<uint32_t>((clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(SmallImageSize.x));
		const auto sunY = static_cast<uint32_t>((0.5f - clip.y / clip.w * 0.5f) * static_cast<float>(SmallImageSize.y));

		const Image normal = fixture.Render(world, view);
		world.Settings.Exposure = 1.0f;
		const Image brighter = fixture.Render(world, view);
		world.Settings.Exposure = 0.0f;
		world.Environment.Rotation = 45.0f;
		const Image turned = fixture.Render(world, view);
		world.Environment.ShowSky = false;
		const Image noSky = fixture.Render(world, view);

		// A stop brighter, less what the tonemapper compresses
		CHECK(AreaBrightness(brighter, 64, 90) > AreaBrightness(normal, 64, 90) + 15.0f);
		CAPTURE(sunX);
		CAPTURE(sunY);
		CHECK(AreaBrightness(turned, sunX, sunY, 1) > 250.0f);
		CHECK(AreaBrightness(normal, sunX, sunY, 1) < 240.0f);
		// Without a sky, the background is one color
		CHECK(noSky.GetPixel(2, 2) == noSky.GetPixel(125, 5));
		CHECK(noSky.GetPixel(2, 2) == noSky.GetPixel(3, 30));
	}

	TEST_CASE("Assets that can't load draw nothing, are reported once, and fall back to the default environment")
	{
		const Testing::LogCapture log;
		RenderWorld world;
		world.Environment.Environment = MissingId;
		world.Meshes.push_back({.Model = MissingId, .World = Place(glm::vec3(0.0f))});
		world.Meshes.push_back({.Model = PrimitivesId, .Mesh = 7, .World = Place(glm::vec3(0.0f))});
		RendererFixture fixture(glm::uvec2(64, 64));
		const RenderView view = MakeView(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f));

		static_cast<void>(fixture.Render(world, view));
		static_cast<void>(fixture.Render(world, view));

		CHECK(fixture.GetStats().VisibleMeshes == 0);
		CHECK(fixture.GetStats().PassDrawCalls[static_cast<size_t>(RenderPass::Opaque)] == 0);
		const Error* error = fixture.GetRenderer().GetAssets().GetError(MissingId);
		REQUIRE(error != nullptr);
		CHECK(error->GetCode() == ErrorCode::NotFound);
		CHECK(CountMessages(log, "Can't draw model") == 1);
		CHECK(CountMessages(log, "Can't light the scene with environment") == 1);
		CHECK(CountMessages(log, "has no mesh 7") == 1);
	}

	TEST_CASE("Assets that failed to load are tried again once the renderer forgets the failures")
	{
		const Testing::LogCapture log;
		RenderWorld world;
		world.Meshes.push_back({.Model = PrimitivesId, .Mesh = Cube, .World = Place(glm::vec3(0.0f))});
		RendererFixture fixture(glm::uvec2(64, 64));
		const RenderView view = MakeView(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f));
		// A source whose model only becomes available later, as when its file is added to a project
		auto available = std::make_shared<bool>(false);
		RenderAssets& assets = fixture.GetRenderer().GetAssets();
		assets.SetSource({
			.LoadModel = [available](UUID) -> std::expected<Model, Error>
			{
				if (!*available)
					return std::unexpected(Error(ErrorCode::NotFound, "Not added yet"));
				return ModelImporter::Import(GetFixture("Models", "Primitives.glb"));
			},
		});

		static_cast<void>(fixture.Render(world, view));
		CHECK(fixture.GetStats().PassDrawCalls[static_cast<size_t>(RenderPass::Opaque)] == 0);
		CHECK(assets.GetError(PrimitivesId) != nullptr);

		// The failure is remembered, and logged once, until it's forgotten
		*available = true;
		static_cast<void>(fixture.Render(world, view));
		CHECK(fixture.GetStats().PassDrawCalls[static_cast<size_t>(RenderPass::Opaque)] == 0);
		CHECK(CountMessages(log, "Can't draw model") == 1);
		assets.ForgetFailures();
		static_cast<void>(fixture.Render(world, view));
		CHECK(fixture.GetStats().PassDrawCalls[static_cast<size_t>(RenderPass::Opaque)] == 1);
		CHECK(assets.GetError(PrimitivesId) == nullptr);
	}

	TEST_CASE("GPU timings arrive once the GPU finishes a frame, where the device can time work")
	{
		RenderWorld world;
		world.Meshes.push_back({.Model = PrimitivesId, .Mesh = Cube, .World = Place(glm::vec3(0.0f))});
		RendererFixture fixture(glm::uvec2(64, 64));
		const RenderView view = MakeView(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f));

		static_cast<void>(fixture.Render(world, view));
		CHECK_FALSE(fixture.GetStats().GpuPassTimes.has_value());
		// The first frame has finished (reading it back waited for it), so the next one reports its timings
		static_cast<void>(fixture.Render(world, view));

		if (!fixture.GetDevice().GetInfo().SupportsTimerQueries)
		{
			CHECK_FALSE(fixture.GetStats().GpuPassTimes.has_value());
			return;
		}
		const RenderStats& stats = fixture.GetStats();
#if defined(LS_LAVAPIPE_DRIVER)
		// lavapipe times work, so its timings arrive
		REQUIRE(stats.GpuPassTimes.has_value());
#endif
		// Some devices have timer queries whose timestamps don't advance within a frame (MoltenVK without GPU
		// counters). They report no timings, rather than timings of nothing
		if (stats.GpuPassTimes)
		{
			for (const double time : *stats.GpuPassTimes)
				CHECK(time >= 0.0);
			CHECK(stats.GetGpuTime().value_or(0.0) > 0.0);
		}
		else
		{
			// Once a device shows it can't time work, it stays untimed
			static_cast<void>(fixture.Render(world, view));
			static_cast<void>(fixture.Render(world, view));
			CHECK_FALSE(fixture.GetStats().GpuPassTimes.has_value());
		}
	}

	TEST_CASE("Scene render targets resize, and refuse empty sizes")
	{
		const auto device = GraphicsDevice::Create(Testing::MakeRenderTestDeviceConfig());
		REQUIRE_MESSAGE(device.has_value(), Testing::DescribeError(device));
		const auto renderer = SceneRenderer::Create(**device, {.ShadowMapSize = 256, .ShadowCascadeCount = 2});
		REQUIRE_MESSAGE(renderer.has_value(), Testing::DescribeError(renderer));

		auto target = (*renderer)->CreateTarget(glm::uvec2(32, 16), "Resize test");
		REQUIRE(target.has_value());
		CHECK((*target)->GetSize() == glm::uvec2(32, 16));
		REQUIRE((*target)->Resize(glm::uvec2(40, 20)).has_value());
		CHECK((*target)->GetColor()->getDesc().width == 40);
		CHECK((*target)->GetDepth()->getDesc().height == 20);
		CHECK_FALSE((*target)->Resize(glm::uvec2(0, 20)).has_value());
		CHECK((*target)->GetSize() == glm::uvec2(40, 20));
		CHECK_FALSE((*renderer)->CreateTarget(glm::uvec2(0, 0), "Empty").has_value());
		CHECK_FALSE(SceneRenderer::Create(**device, {.ShadowCascadeCount = 0}).has_value());
		CHECK_FALSE(SceneRenderer::Create(**device, {.ShadowCascadeCount = 5}).has_value());
	}

}
