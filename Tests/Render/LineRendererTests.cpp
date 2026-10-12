#include "Lodestone/Renderer/LineRenderer.h"

#include "Common/DescribeError.h"
#include "Lodestone/Graphics/GraphicsDevice.h"
#include "Lodestone/Graphics/RenderTarget.h"
#include "Lodestone/Graphics/TextureReadback.h"
#include "Render/RenderTestDevice.h"

#include <doctest/doctest.h>
#include <nvrhi/utils.h>

#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

namespace Lodestone {

	namespace {

		constexpr uint32_t ImageSize = 64;

		// The clip-space height of the center of a row of pixels, so a horizontal line covers exactly that row
		float RowToClip(uint32_t row)
		{
			return 1.0f - 2.0f * (static_cast<float>(row) + 0.5f) / static_cast<float>(ImageSize);
		}

		std::vector<LineVertex> MakeLine(uint32_t row, float depth, uint32_t color)
		{
			return {{.Position = glm::vec3(-0.9f, RowToClip(row), depth), .Color = color},
				{.Position = glm::vec3(0.9f, RowToClip(row), depth), .Color = color}};
		}

		// Draws lines over a dark red image whose depth is 0.5 everywhere, with clip space as world space
		Image Render(const std::vector<LineVertex>& depthTested, const std::vector<LineVertex>& onTop)
		{
			auto device = GraphicsDevice::Create(Testing::MakeRenderTestDeviceConfig());
			REQUIRE_MESSAGE(device.has_value(), Testing::DescribeError(device));
			nvrhi::IDevice* nvrhiDevice = (*device)->GetNvrhiDevice();
			const auto target = RenderTarget::Create(nvrhiDevice, glm::uvec2(ImageSize), "Line test");
			REQUIRE_MESSAGE(target.has_value(), Testing::DescribeError(target));
			const auto lines = LineRenderer::Create(nvrhiDevice);
			REQUIRE_MESSAGE(lines.has_value(), Testing::DescribeError(lines));

			const nvrhi::CommandListHandle commandList = nvrhiDevice->createCommandList();
			commandList->open();
			nvrhi::utils::ClearColorAttachment(
				commandList, (*target)->GetFramebuffer(), 0, nvrhi::Color(0.5f, 0.0f, 0.0f, 1.0f));
			commandList->clearDepthStencilTexture((*target)->GetDepth(), nvrhi::AllSubresources, true, 0.5f, false, 0);
			(*lines)->Render(commandList, (*target)->GetFramebuffer(), glm::mat4(1.0f), depthTested, onTop);
			commandList->close();
			nvrhiDevice->executeCommandList(commandList);

			auto image = ReadTexture(nvrhiDevice, (*target)->GetColor());
			REQUIRE_MESSAGE(image.has_value(), Testing::DescribeError(image));
			CHECK((*device)->GetValidationErrorCount() == 0);
			return std::move(*image);
		}

		bool IsNear(Rgba8 pixel, Rgba8 expected)
		{
			const auto near = [](uint8_t a, uint8_t b)
			{ return std::abs(static_cast<int>(a) - static_cast<int>(b)) <= 2; };
			return near(pixel.R, expected.R) && near(pixel.G, expected.G) && near(pixel.B, expected.B);
		}

	}

	TEST_CASE("Depth-tested lines hide behind the scene; the others draw over everything")
	{
		constexpr uint32_t green = LineVertex::MakeColor(0, 255, 0);
		// Depth is reversed: larger is nearer
		std::vector<LineVertex> depthTested = MakeLine(10, 0.8f, green);
		const std::vector<LineVertex> hidden = MakeLine(20, 0.2f, green);
		depthTested.insert(depthTested.end(), hidden.begin(), hidden.end());
		const std::vector<LineVertex> onTop = MakeLine(30, 0.2f, LineVertex::MakeColor(0, 0, 255));

		const Image image = Render(depthTested, onTop);

		const Rgba8 background{.R = 128, .G = 0, .B = 0, .A = 255};
		CHECK(IsNear(image.GetPixel(32, 10), Rgba8{.R = 0, .G = 255, .B = 0, .A = 255}));
		CHECK(IsNear(image.GetPixel(32, 20), background));
		CHECK(IsNear(image.GetPixel(32, 30), Rgba8{.R = 0, .G = 0, .B = 255, .A = 255}));
		// Beside the lines' ends, and between them: the background
		CHECK(IsNear(image.GetPixel(1, 10), background));
		CHECK(IsNear(image.GetPixel(32, 15), background));
	}

	TEST_CASE("Lines blend by their alpha, and an odd vertex left over draws nothing")
	{
		std::vector<LineVertex> onTop = MakeLine(12, 0.5f, LineVertex::MakeColor(0, 0, 255, 128));
		onTop.push_back({.Position = glm::vec3(0.0f), .Color = LineVertex::MakeColor(255, 255, 255)});

		const Image image = Render({}, onTop);

		// Half blue over dark red
		CHECK(IsNear(image.GetPixel(32, 12), Rgba8{.R = 64, .G = 0, .B = 128, .A = 255}));
		CHECK(IsNear(image.GetPixel(32, 32), Rgba8{.R = 128, .G = 0, .B = 0, .A = 255}));
		CHECK(IsNear(Render({}, {}).GetPixel(5, 5), Rgba8{.R = 128, .G = 0, .B = 0, .A = 255}));
	}

}
