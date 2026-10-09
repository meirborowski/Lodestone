#include "Common/ReferenceImage.h"
#include "Lodestone/Graphics/GraphicsDevice.h"
#include "Lodestone/Graphics/TextureReadback.h"
#include "Lodestone/Graphics/TriangleRenderer.h"

#include <doctest/doctest.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <utility>

// Prints pixels in assertion messages
template <>
struct doctest::StringMaker<Lodestone::Rgba8>
{
	// NOLINTNEXTLINE(readability-identifier-naming): the name doctest calls
	static doctest::String convert(const Lodestone::Rgba8& color)
	{
		return fmt::format("({}, {}, {}, {})", color.R, color.G, color.B, color.A).c_str();
	}
};

namespace Lodestone {

	namespace {

		constexpr uint32_t ImageSize = 256;

		nvrhi::TextureHandle CreateRenderTarget(nvrhi::IDevice* device)
		{
			nvrhi::TextureDesc desc;
			desc.width = ImageSize;
			desc.height = ImageSize;
			desc.format = nvrhi::Format::RGBA8_UNORM;
			desc.debugName = "Test render target";
			desc.isRenderTarget = true;
			desc.initialState = nvrhi::ResourceStates::RenderTarget;
			desc.keepInitialState = true;
			return device->createTexture(desc);
		}

		// Renders the triangle into an offscreen texture and reads it back
		Image RenderTriangle(const GraphicsDevice& device)
		{
			nvrhi::IDevice* nvrhiDevice = device.GetNvrhiDevice();
			const nvrhi::TextureHandle target = CreateRenderTarget(nvrhiDevice);
			REQUIRE(target);
			const nvrhi::FramebufferHandle framebuffer =
				nvrhiDevice->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target));
			const auto renderer = TriangleRenderer::Create(nvrhiDevice, framebuffer->getFramebufferInfo());
			REQUIRE_MESSAGE(renderer.has_value(), fmt::format("{}", renderer.error()));

			const nvrhi::CommandListHandle commandList = nvrhiDevice->createCommandList();
			commandList->open();
			(*renderer)->Render(commandList, framebuffer);
			commandList->close();
			nvrhiDevice->executeCommandList(commandList);

			auto image = ReadTexture(nvrhiDevice, target);
			REQUIRE_MESSAGE(image.has_value(), fmt::format("{}", image.error()));
			REQUIRE(image->GetWidth() == ImageSize);
			REQUIRE(image->GetHeight() == ImageSize);
			return std::move(*image);
		}

	}

	TEST_CASE("The triangle renders offscreen and matches its reference image" * doctest::test_suite("ReferenceImages"))
	{
		const auto device = GraphicsDevice::Create({});
		REQUIRE_MESSAGE(device.has_value(), fmt::format("{}", device.error()));

		const Image image = RenderTriangle(**device);

		Testing::CheckReferenceImage("Triangle", image);
		CHECK((*device)->GetValidationErrorCount() == 0);
	}

	// Runs on every driver, including MoltenVK on macOS, where reference images aren't compared: the background is
	// cleared, and each corner of the triangle has its own color
	TEST_CASE("The triangle renders offscreen")
	{
		const auto device = GraphicsDevice::Create({});
		REQUIRE_MESSAGE(device.has_value(), fmt::format("{}", device.error()));

		const Image image = RenderTriangle(**device);

		const Rgba8 background = image.GetPixel(0, 0);
		CAPTURE(background);
		CHECK(background.R < 40);
		CHECK(background.G < 40);
		CHECK(background.B < 40);
		CHECK(background.A == 255);

		// Just inside the top (red), bottom-right (green) and bottom-left (blue) corners
		const Rgba8 top = image.GetPixel(128, 48);
		CAPTURE(top);
		CHECK(top.R > 2 * std::max(top.G, top.B));
		const Rgba8 bottomRight = image.GetPixel(208, 216);
		CAPTURE(bottomRight);
		CHECK(bottomRight.G > 2 * std::max(bottomRight.R, bottomRight.B));
		const Rgba8 bottomLeft = image.GetPixel(48, 216);
		CAPTURE(bottomLeft);
		CHECK(bottomLeft.B > 2 * std::max(bottomLeft.R, bottomLeft.G));

		CHECK((*device)->GetValidationErrorCount() == 0);
	}

	TEST_CASE("Reading back an unsupported format fails")
	{
		const auto device = GraphicsDevice::Create({});
		REQUIRE_MESSAGE(device.has_value(), fmt::format("{}", device.error()));
		nvrhi::IDevice* nvrhiDevice = (*device)->GetNvrhiDevice();

		nvrhi::TextureDesc desc;
		desc.width = 4;
		desc.height = 4;
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.debugName = "Float texture";
		const nvrhi::TextureHandle texture = nvrhiDevice->createTexture(desc);
		REQUIRE(texture);

		const auto image = ReadTexture(nvrhiDevice, texture);

		REQUIRE_FALSE(image.has_value());
		CHECK(image.error().GetCode() == ErrorCode::Unsupported);
	}

}
