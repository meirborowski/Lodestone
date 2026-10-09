#include "Common/DescribeError.h"
#include "Common/ReferenceImage.h"
#include "Lodestone/Graphics/DebugSceneRenderer.h"
#include "Lodestone/Graphics/GraphicsDevice.h"
#include "Lodestone/Graphics/RenderTarget.h"
#include "Lodestone/Graphics/TextureReadback.h"
#include "Render/RenderTestDevice.h"

#include <doctest/doctest.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <span>
#include <utility>
#include <vector>

namespace Lodestone {

	namespace {

		constexpr glm::uvec2 ImageSize{256, 256};

		glm::mat4 MakeViewProjection(const glm::vec3& eye, const glm::vec3& target)
		{
			const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.05f, 500.0f);
			return projection * glm::lookAtRH(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
		}

		glm::mat4 MakeWorld(const glm::vec3& position, const glm::vec3& scale, float yawDegrees = 0.0f)
		{
			return glm::translate(glm::mat4(1.0f), position) *
				glm::rotate(glm::mat4(1.0f), glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f)) *
				glm::scale(glm::mat4(1.0f), scale);
		}

		// Renders a view on a device of its own, and reads it back
		Image Render(const DebugView& view, std::span<const DebugCube> cubes)
		{
			auto device = GraphicsDevice::Create(Testing::MakeRenderTestDeviceConfig());
			REQUIRE_MESSAGE(device.has_value(), Testing::DescribeError(device));
			nvrhi::IDevice* nvrhiDevice = (*device)->GetNvrhiDevice();
			const auto target = RenderTarget::Create(nvrhiDevice, ImageSize, "Debug scene test");
			REQUIRE_MESSAGE(target.has_value(), Testing::DescribeError(target));
			const auto renderer = DebugSceneRenderer::Create(nvrhiDevice);
			REQUIRE_MESSAGE(renderer.has_value(), Testing::DescribeError(renderer));

			const nvrhi::CommandListHandle commandList = nvrhiDevice->createCommandList();
			commandList->open();
			(*renderer)->Render(commandList, (*target)->GetFramebuffer(), view, cubes);
			commandList->close();
			nvrhiDevice->executeCommandList(commandList);

			auto image = ReadTexture(nvrhiDevice, (*target)->GetColor());
			REQUIRE_MESSAGE(image.has_value(), Testing::DescribeError(image));
			CHECK((*device)->GetValidationErrorCount() == 0);
			return std::move(*image);
		}

		bool IsBackground(Rgba8 pixel, const glm::vec4& clear)
		{
			const auto matches = [](uint8_t channel, float value)
			{ return std::abs(static_cast<float>(channel) - value * 255.0f) <= 2.0f; };
			return matches(pixel.R, clear.r) && matches(pixel.G, clear.g) && matches(pixel.B, clear.b);
		}

	}

	TEST_CASE("The debug scene renders lit cubes on a grid and matches its reference image" *
		doctest::test_suite("ReferenceImages"))
	{
		const DebugView view{.ViewProjection = MakeViewProjection(glm::vec3(6.0f, 5.0f, 8.0f), glm::vec3(0.0f))};
		const std::vector<DebugCube> cubes = {
			// A slab whose top is clearly below the grid, so the grid shows on it, and cubes standing clearly above
			// it: geometry exactly on the grid's plane would make the image depend on rounding
			{.World = MakeWorld(glm::vec3(0.0f, -0.1f, 0.0f), glm::vec3(6.0f, 0.1f, 6.0f)),
				.Color = glm::vec4(0.55f, 0.6f, 0.7f, 1.0f)},
			{.World = MakeWorld(glm::vec3(-1.5f, 0.52f, 0.0f), glm::vec3(1.0f)),
				.Color = glm::vec4(0.9f, 0.3f, 0.3f, 1.0f)},
			{.World = MakeWorld(glm::vec3(1.5f, 1.02f, -1.0f), glm::vec3(1.0f, 2.0f, 1.0f)),
				.Color = glm::vec4(0.3f, 0.85f, 0.4f, 1.0f)},
			{.World = MakeWorld(glm::vec3(0.5f, 0.52f, 1.8f), glm::vec3(1.0f), 45.0f),
				.Color = glm::vec4(1.0f, 0.55f, 0.15f, 1.0f)},
		};

		const Image rendered = Render(view, cubes);

		Testing::CheckReferenceImage("DebugScene", rendered);
	}

	// Runs on every driver, including MoltenVK on macOS, where reference images aren't compared
	TEST_CASE("The debug scene draws nearer cubes over farther ones")
	{
		const DebugView view{.ViewProjection = MakeViewProjection(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f))};
		// A small red cube in front of a large green one, both straight ahead: the red one is drawn first, so only
		// the depth test keeps the green one from covering it
		const std::vector<DebugCube> cubes = {
			{.World = MakeWorld(glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(1.0f)),
				.Color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)},
			{.World = MakeWorld(glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(4.0f)),
				.Color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)},
		};

		const Image rendered = Render(view, cubes);

		const Rgba8 center = rendered.GetPixel(128, 128);
		CAPTURE(center.R);
		CAPTURE(center.G);
		CHECK(center.R > 40);
		CHECK(center.G < 10);
		// Beside the red cube, the green one shows
		const Rgba8 side = rendered.GetPixel(128 + 40, 128);
		CHECK(side.G > 40);
		CHECK(side.R < 10);
		// The corner is the background
		CHECK(IsBackground(rendered.GetPixel(0, 0), view.ClearColor));
	}

	TEST_CASE("The debug scene's grid can be hidden, leaving only the clear color")
	{
		DebugView view{.ViewProjection = MakeViewProjection(glm::vec3(4.0f, 6.0f, 4.0f), glm::vec3(0.0f)),
			.ClearColor = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f),
			.ShowGrid = false};

		const Image hidden = Render(view, {});
		view.ShowGrid = true;
		const Image shown = Render(view, {});

		size_t gridPixels = 0;
		for (uint32_t y = 0; y < ImageSize.y; ++y)
		{
			for (uint32_t x = 0; x < ImageSize.x; ++x)
			{
				CHECK(IsBackground(hidden.GetPixel(x, y), view.ClearColor));
				gridPixels += IsBackground(shown.GetPixel(x, y), view.ClearColor) ? 0 : 1;
			}
		}
		CHECK(gridPixels > 1000);
	}

	TEST_CASE("Render targets resize, and refuse empty sizes")
	{
		const auto device = GraphicsDevice::Create(Testing::MakeRenderTestDeviceConfig());
		REQUIRE_MESSAGE(device.has_value(), Testing::DescribeError(device));
		nvrhi::IDevice* nvrhiDevice = (*device)->GetNvrhiDevice();

		const auto target = RenderTarget::Create(nvrhiDevice, glm::uvec2(64, 32), "Resize test");
		REQUIRE(target.has_value());
		const nvrhi::ITexture* original = (*target)->GetColor();
		CHECK((*target)->GetSize() == glm::uvec2(64, 32));
		CHECK((*target)->GetColor()->getDesc().width == 64);
		CHECK((*target)->GetDepth()->getDesc().height == 32);

		// The same size keeps the textures
		REQUIRE((*target)->Resize(glm::uvec2(64, 32)).has_value());
		CHECK((*target)->GetColor() == original);
		REQUIRE((*target)->Resize(glm::uvec2(100, 50)).has_value());
		CHECK((*target)->GetSize() == glm::uvec2(100, 50));
		CHECK((*target)->GetColor()->getDesc().width == 100);
		CHECK((*target)->GetFramebuffer()->getFramebufferInfo().width == 100);

		CHECK_FALSE((*target)->Resize(glm::uvec2(0, 50)).has_value());
		CHECK((*target)->GetSize() == glm::uvec2(100, 50));
		CHECK_FALSE(RenderTarget::Create(nvrhiDevice, glm::uvec2(16, 0), "Empty").has_value());
	}

}
