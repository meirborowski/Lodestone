#include "Common/DescribeError.h"
#include "Common/ReferenceImage.h"
#include "Lodestone/Graphics/GraphicsDevice.h"
#include "Lodestone/Graphics/ImGuiRenderer.h"
#include "Lodestone/Graphics/RenderTarget.h"
#include "Lodestone/Graphics/TextureReadback.h"
#include "Render/RenderTestDevice.h"

#include <doctest/doctest.h>
#include <imgui.h>
#include <nvrhi/utils.h>

#include <functional>
#include <utility>

namespace Lodestone {

	namespace {

		constexpr glm::uvec2 ImageSize{256, 256};

		// Dear ImGui's context, with fixed settings so frames are the same everywhere
		class ImGuiScope
		{
		public:
			ImGuiScope()
				: m_Context(ImGui::CreateContext())
			{
				ImGuiIO& io = ImGui::GetIO();
				io.IniFilename = nullptr;
				io.DisplaySize = ImVec2(static_cast<float>(ImageSize.x), static_cast<float>(ImageSize.y));
				io.DeltaTime = 1.0f / 60.0f;
				ImGui::StyleColorsDark();
			}
			~ImGuiScope() { ImGui::DestroyContext(m_Context); }

			ImGuiScope(const ImGuiScope&) = delete;
			ImGuiScope& operator=(const ImGuiScope&) = delete;
			ImGuiScope(ImGuiScope&&) = delete;
			ImGuiScope& operator=(ImGuiScope&&) = delete;

		private:
			ImGuiContext* m_Context;
		};

		// Renders a few frames of a UI into a render target, and reads back the last. The UI gets the ID of an
		// engine texture to show
		Image RenderUI(const std::function<void(ImTextureID engineTexture)>& buildUI)
		{
			const auto device = GraphicsDevice::Create(Testing::MakeRenderTestDeviceConfig());
			REQUIRE_MESSAGE(device.has_value(), Testing::DescribeError(device));
			nvrhi::IDevice* nvrhiDevice = (*device)->GetNvrhiDevice();
			const auto target = RenderTarget::Create(nvrhiDevice, ImageSize, "ImGui test");
			REQUIRE_MESSAGE(target.has_value(), Testing::DescribeError(target));
			const auto engineTexture = RenderTarget::Create(nvrhiDevice, glm::uvec2(16, 16), "Engine texture");
			REQUIRE(engineTexture.has_value());

			const ImGuiScope imgui;
			auto renderer = ImGuiRenderer::Create(nvrhiDevice, RenderTarget::GetFramebufferInfo());
			REQUIRE_MESSAGE(renderer.has_value(), Testing::DescribeError(renderer));
			CHECK((ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0);

			const nvrhi::CommandListHandle commandList = nvrhiDevice->createCommandList();
			commandList->open();
			// Magenta, shown through ImGui::Image, as the editor shows its viewport
			nvrhi::utils::ClearColorAttachment(
				commandList, (*engineTexture)->GetFramebuffer(), 0, nvrhi::Color(1.0f, 0.0f, 1.0f, 1.0f));
			commandList->close();
			nvrhiDevice->executeCommandList(commandList);

			// The first frames lay windows out; the last one is read back
			for (int frame = 0; frame < 3; ++frame)
			{
				ImGui::NewFrame();
				buildUI((*renderer)->GetTextureId((*engineTexture)->GetColor()));
				ImGui::Render();
				commandList->open();
				nvrhi::utils::ClearColorAttachment(
					commandList, (*target)->GetFramebuffer(), 0, nvrhi::Color(0.1f, 0.1f, 0.1f, 1.0f));
				(*renderer)->Render(commandList, (*target)->GetFramebuffer(), ImGui::GetDrawData());
				commandList->close();
				nvrhiDevice->executeCommandList(commandList);
			}

			auto image = ReadTexture(nvrhiDevice, (*target)->GetColor());
			REQUIRE_MESSAGE(image.has_value(), Testing::DescribeError(image));
			// Destroying the renderer releases Dear ImGui's textures, while its context still exists
			renderer->reset();
			for (const ImTextureData* texture : ImGui::GetPlatformIO().Textures)
				CHECK(texture->BackendUserData == nullptr);
			CHECK((*device)->GetValidationErrorCount() == 0);
			return std::move(*image);
		}

		void BuildTestWindow(ImTextureID engineTexture)
		{
			ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f));
			ImGui::SetNextWindowSize(ImVec2(224.0f, 224.0f));
			ImGui::Begin("Lodestone", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
			ImGui::TextUnformatted("Hello from Dear ImGui");
			ImGui::Button("A button");
			ImGui::Image(ImTextureRef(engineTexture), ImVec2(64.0f, 48.0f));
			ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(180.0f, 180.0f), 24.0f, IM_COL32(40, 200, 120, 255));
			ImGui::End();
		}

	}

	TEST_CASE(
		"Dear ImGui renders through NVRHI and matches its reference image" * doctest::test_suite("ReferenceImages"))
	{
		const Image image = RenderUI(BuildTestWindow);

		Testing::CheckReferenceImage("ImGui", image);
	}

	// Runs on every driver, including MoltenVK on macOS, where reference images aren't compared
	TEST_CASE("Dear ImGui renders its windows, text and shapes, and shows engine textures")
	{
		const Image image = RenderUI(BuildTestWindow);

		// Outside the window, the cleared background
		const Rgba8 background = image.GetPixel(4, 4);
		CHECK(background.R == background.G);
		CHECK(background.R < 40);
		// The filled circle
		const Rgba8 circle = image.GetPixel(180, 180);
		CHECK(circle.G > 150);
		CHECK(circle.R < 80);
		// The engine texture, shown as an image: magenta somewhere in the window
		size_t magenta = 0;
		size_t bright = 0;
		for (uint32_t y = 16; y < 240; ++y)
		{
			for (uint32_t x = 16; x < 240; ++x)
			{
				const Rgba8 pixel = image.GetPixel(x, y);
				magenta += pixel.R > 240 && pixel.B > 240 && pixel.G < 16 ? 1 : 0;
				bright += pixel.R > 200 && pixel.G > 200 && pixel.B > 200 ? 1 : 0;
			}
		}
		CHECK(magenta == 64 * 48);
		// The text, drawn from the font atlas Dear ImGui asked the renderer to create
		CHECK(bright > 50);
	}

	TEST_CASE("A frame without windows leaves the target as it was cleared")
	{
		const Image image = RenderUI([](ImTextureID) {});

		CHECK(image.GetPixel(128, 128).R < 40);
	}

}
