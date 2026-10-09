#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Platform/Window.h"

#include <expected>

namespace Lodestone {

	// Feeds a window's input, size and cursor to Dear ImGui, through Dear ImGui's GLFW backend. The window's own input
	// handling keeps working: the backend passes every event on to it. Uses the current ImGui context, which must
	// outlive this object, as must the window
	class ImGuiPlatform
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		[[nodiscard]] static std::expected<Scope<ImGuiPlatform>, Error> Create(Window& window);

		ImGuiPlatform(Passkey passkey, Window& window);
		~ImGuiPlatform();

		ImGuiPlatform(const ImGuiPlatform&) = delete;
		ImGuiPlatform& operator=(const ImGuiPlatform&) = delete;
		ImGuiPlatform(ImGuiPlatform&&) = delete;
		ImGuiPlatform& operator=(ImGuiPlatform&&) = delete;

		// Call before ImGui::NewFrame()
		void NewFrame();
		// How much to scale the UI for the window's display: 1 at 96 DPI on Windows and Linux, more on denser displays.
		// Always 1 on macOS, where Dear ImGui's coordinates are in points
		float GetContentScale() const;

	private:
		Window* m_Window;
	};

}
