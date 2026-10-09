#include "Lodestone/Graphics/GraphicsDevice.h"

#include <doctest/doctest.h>
#include <spdlog/fmt/fmt.h>

namespace Lodestone {

	TEST_CASE("A headless graphics device can be created")
	{
		const auto device = GraphicsDevice::Create({});

		REQUIRE_MESSAGE(device.has_value(), fmt::format("{}", device.error()));
		CHECK((*device)->GetNvrhiDevice() != nullptr);
		CHECK_FALSE((*device)->CanPresent());
		CHECK_FALSE((*device)->GetInfo().Name.empty());
		CHECK((*device)->GetInfo().ApiVersion >= VK_API_VERSION_1_3);
		MESSAGE(fmt::format("Device: {} ({})", (*device)->GetInfo().Name, ToString((*device)->GetInfo().Type)));
	}

#if defined(LS_LAVAPIPE_ICD)
	TEST_CASE("Rendering tests run on lavapipe")
	{
		// RenderTestMain.cpp points the Vulkan loader at lavapipe alone
		const auto device = GraphicsDevice::Create({});

		REQUIRE_MESSAGE(device.has_value(), fmt::format("{}", device.error()));
		CHECK((*device)->GetInfo().Type == GpuType::Cpu);
		CHECK((*device)->GetInfo().Name.starts_with("llvmpipe"));
	}
#endif

	TEST_CASE("Only one graphics device exists at a time")
	{
		const auto first = GraphicsDevice::Create({});
		REQUIRE_MESSAGE(first.has_value(), fmt::format("{}", first.error()));

		const auto second = GraphicsDevice::Create({});

		REQUIRE_FALSE(second.has_value());
		CHECK(second.error().GetCode() == ErrorCode::InvalidState);
	}

	TEST_CASE("Presentation extensions without a presentation query are rejected")
	{
		GraphicsDeviceConfig config;
		config.PresentationExtensions = {"VK_KHR_surface"};

		const auto device = GraphicsDevice::Create(config);

		REQUIRE_FALSE(device.has_value());
		CHECK(device.error().GetCode() == ErrorCode::InvalidArgument);
	}

}
