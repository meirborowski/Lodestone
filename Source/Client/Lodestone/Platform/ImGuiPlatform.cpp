#include "Lodestone/Platform/ImGuiPlatform.h"

#include <imgui_impl_glfw.h>

namespace Lodestone {

	std::expected<Scope<ImGuiPlatform>, Error> ImGuiPlatform::Create(Window& window)
	{
		// The backend installs its callbacks in front of the window's, and calls the window's from its own
		if (!ImGui_ImplGlfw_InitForVulkan(window.GetNativeHandle(), true))
			return std::unexpected(Error(ErrorCode::DeviceError, "Connecting Dear ImGui to the window failed"));
		return CreateScope<ImGuiPlatform>(Passkey(), window);
	}

	ImGuiPlatform::ImGuiPlatform(Passkey /*passkey*/, Window& window)
		: m_Window(&window)
	{
	}

	ImGuiPlatform::~ImGuiPlatform()
	{
		ImGui_ImplGlfw_Shutdown();
	}

	void ImGuiPlatform::NewFrame()
	{
		ImGui_ImplGlfw_NewFrame();
	}

	float ImGuiPlatform::GetContentScale() const
	{
		return ImGui_ImplGlfw_GetContentScaleForWindow(m_Window->GetNativeHandle());
	}

}
