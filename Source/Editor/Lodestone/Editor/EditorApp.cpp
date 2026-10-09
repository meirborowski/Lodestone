#include "Lodestone/Editor/EditorApp.h"

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Environment.h"
#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Core/Version.h"
#include "Lodestone/Editor/EditorContext.h"
#include "Lodestone/Editor/Mcp/EditorTools.h"
#include "Lodestone/Editor/Mcp/HttpTransport.h"
#include "Lodestone/Editor/Mcp/MainThreadDispatcher.h"
#include "Lodestone/Editor/Mcp/McpServer.h"
#include "Lodestone/Editor/Mcp/StdioTransport.h"
#include "Lodestone/Editor/SceneView.h"
#include "Lodestone/Editor/UI/EditorUI.h"
#include "Lodestone/Graphics/GraphicsDevice.h"
#include "Lodestone/Graphics/ImGuiRenderer.h"
#include "Lodestone/Graphics/Swapchain.h"
#include "Lodestone/Input/InputCommandBuilder.h"
#include "Lodestone/Platform/ImGuiPlatform.h"
#include "Lodestone/Platform/Window.h"

#include <imgui.h>
#include <nvrhi/utils.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <system_error>
#include <thread>

#if LS_PLATFORM_WINDOWS
	#include <fcntl.h>
	#include <io.h>
#endif

namespace Lodestone {

	namespace {

		using Clock = std::chrono::steady_clock;

		constexpr std::string_view McpInstructions =
			"Lodestone Editor: build and playtest games. Start with project_info. Open a project with project_open or "
			"create one with project_create; scenes and prefabs are saved in its Assets directory, and every path "
			"you give is relative to it. Entities are identified by UUIDs. component_types lists every component and "
			"its fields: add them with component_add and change them with component_set. Every edit is a command "
			"that edit_undo can undo. viewport_screenshot shows the scene from the editor camera (camera_set and "
			"camera_frame move it). play_start runs the scene; input_send plays it, play_step runs ticks while "
			"paused, and play_stop restores the scene as it was. log_read shows the log, including errors.";

		// How often the headless editor wakes up when nothing happens: to see whether its stdio client has gone,
		// and, while playing, to run the simulation
		constexpr std::chrono::milliseconds IdleWait{50};
		constexpr std::chrono::milliseconds PlayingWait{1};

		// Set from a signal handler, so it must be a lock-free atomic
		std::atomic<bool> s_StopRequested = false;
		static_assert(std::atomic<bool>::is_always_lock_free);

		void HandleStopSignal(int /*signal*/)
		{
			s_StopRequested.store(true);
		}

		// Where the editor keeps per-user settings, such as its panel layout
		std::optional<std::filesystem::path> GetSettingsDirectory()
		{
#if LS_PLATFORM_WINDOWS
			if (const auto appData = ReadEnvironmentVariable("APPDATA"))
				return PathFromUtf8(*appData) / "Lodestone";
#elif LS_PLATFORM_MACOS
			if (const auto home = ReadEnvironmentVariable("HOME"))
				return PathFromUtf8(*home) / "Library" / "Application Support" / "Lodestone";
#else
			if (const auto config = ReadEnvironmentVariable("XDG_CONFIG_HOME"); config && !config->empty())
				return PathFromUtf8(*config) / "lodestone";
			if (const auto home = ReadEnvironmentVariable("HOME"))
				return PathFromUtf8(*home) / ".config" / "lodestone";
#endif
			return std::nullopt;
		}

		std::expected<void, Error> OpenStartupProject(EditorContext& context, const EditorOptions& options)
		{
			if (!options.Project)
				return {};
			if (auto opened = context.OpenProject(*options.Project); !opened)
				return std::unexpected(opened.error().WithContext(fmt::format("Opening {}", *options.Project)));
			LS_CORE_INFO("Opened the project {}", *options.Project);
			return {};
		}

		// The HTTP transport, if the options call for it
		std::expected<Scope<HttpTransport>, Error> StartHttp(
			const EditorOptions& options, McpServer& server, MainThreadDispatcher& dispatcher)
		{
			// The stdio editor serves HTTP only when asked: clients each start one, and they'd clash over the port
			if (options.McpStdio && !options.McpPort)
				return Scope<HttpTransport>();
			const uint16_t port = options.McpPort.value_or(EditorOptions::DefaultMcpPort);
			auto transport = HttpTransport::Start(server, dispatcher, port);
			if (!transport)
			{
				// Another editor is probably serving the default port. This one still works, just not over MCP
				if (!options.McpPort)
				{
					LS_CORE_WARN("MCP over HTTP is off: {}", transport.error());
					return Scope<HttpTransport>();
				}
				return std::unexpected(transport.error());
			}
			return std::move(*transport);
		}

		// Shuts a dispatcher down when it goes out of scope. Declared after the transports, so it runs before they're
		// destroyed: they wait for their requests, which wait for the dispatcher
		class DispatcherShutdown
		{
		public:
			explicit DispatcherShutdown(MainThreadDispatcher& dispatcher)
				: m_Dispatcher(&dispatcher)
			{
			}
			~DispatcherShutdown() { m_Dispatcher->Shutdown(); }

			DispatcherShutdown(const DispatcherShutdown&) = delete;
			DispatcherShutdown& operator=(const DispatcherShutdown&) = delete;
			DispatcherShutdown(DispatcherShutdown&&) = delete;
			DispatcherShutdown& operator=(DispatcherShutdown&&) = delete;

		private:
			MainThreadDispatcher* m_Dispatcher;
		};

		// Renders screenshots for the headless editor, which starts its graphics device on the first one, so it
		// starts quickly and runs where there's no GPU until a screenshot is asked for
		class OffscreenCapture
		{
		public:
			explicit OffscreenCapture(std::filesystem::path driver)
				: m_Driver(std::move(driver))
			{
			}

			std::expected<Image, Error> Capture(const EditorContext& context, glm::uvec2 size)
			{
				if (m_Failure)
					return std::unexpected(*m_Failure);
				if (!m_View)
				{
					if (auto started = Start(); !started)
					{
						m_Failure = started.error().WithContext("Starting the graphics device for screenshots");
						LS_CORE_ERROR("{}", *m_Failure);
						return std::unexpected(*m_Failure);
					}
				}
				return m_View->Capture(context, size);
			}

			uint32_t GetValidationErrorCount() const { return m_Device ? m_Device->GetValidationErrorCount() : 0; }

		private:
			std::expected<void, Error> Start()
			{
				GraphicsDeviceConfig config;
				config.Driver = m_Driver;
				auto device = GraphicsDevice::Create(config);
				if (!device)
					return std::unexpected(device.error());
				auto view = SceneView::Create(**device);
				if (!view)
					return std::unexpected(view.error());
				m_Device = std::move(*device);
				m_View = std::move(*view);
				LS_CORE_INFO("Rendering screenshots on {}", m_Device->GetInfo().Name);
				return {};
			}

		private:
			std::filesystem::path m_Driver;
			std::optional<Error> m_Failure;
			// Declared before the view, which it outlives
			Scope<GraphicsDevice> m_Device;
			Scope<SceneView> m_View;
		};

		std::expected<int, Error> RunHeadless(const EditorOptions& options, EditorContext& context, McpServer& server)
		{
			MainThreadDispatcher dispatcher;
			OffscreenCapture capture(options.VulkanDriver);
			// Set on the main thread, by editor_quit
			bool quit = false;
			EditorToolHost host;
			host.Capture = [&capture, &context](uint32_t width, uint32_t height)
			{ return capture.Capture(context, glm::uvec2(width, height)); };
			if (!options.McpStdio)
				host.RequestQuit = [&quit] { quit = true; };
			RegisterEditorTools(server, context, std::move(host));

			auto http = StartHttp(options, server, dispatcher);
			if (!http)
				return std::unexpected(http.error());

			Scope<StdioTransport> stdio;
			if (options.McpStdio)
			{
#if LS_PLATFORM_WINDOWS
				// Messages end with \n alone; text mode would turn it into \r\n
				if (_setmode(_fileno(stdout), _O_BINARY) == -1 || _setmode(_fileno(stdin), _O_BINARY) == -1)
					return std::unexpected(Error(ErrorCode::IoError, "Switching standard I/O to binary mode failed"));
#endif
				stdio = CreateScope<StdioTransport>(server, dispatcher, std::cin, std::cout);
				LS_CORE_INFO("MCP server reading standard input");
			}
			else
			{
				// The stdio editor stops when its client closes standard input; this one when it's told to
				std::signal(SIGINT, HandleStopSignal);
				std::signal(SIGTERM, HandleStopSignal);
			}
			const DispatcherShutdown shutdown(dispatcher);
			if (stdio)
				stdio->Start();
			LS_CORE_INFO("Lodestone Editor {} running headless", VersionString);

			Clock::time_point last = Clock::now();
			while (!s_StopRequested.load() && !(stdio && stdio->IsFinished()))
			{
				dispatcher.WaitForWork(context.GetPlayState() == PlayState::Playing ? PlayingWait : IdleWait);
				dispatcher.RunPending();
				if (quit)
					break;
				const Clock::time_point now = Clock::now();
				context.Update(std::chrono::duration<double>(now - last).count());
				last = now;
			}

			LS_CORE_INFO("The headless editor is stopping");
			if (const uint32_t errors = capture.GetValidationErrorCount(); errors > 0)
			{
				LS_CORE_ERROR("The validation layers reported {} errors", errors);
				return EXIT_FAILURE;
			}
			return EXIT_SUCCESS;
		}

		// Dear ImGui's context, for as long as the windowed editor runs
		class ImGuiContextScope
		{
		public:
			ImGuiContextScope()
				: m_Context(ImGui::CreateContext())
			{
			}
			~ImGuiContextScope() { ImGui::DestroyContext(m_Context); }

			ImGuiContextScope(const ImGuiContextScope&) = delete;
			ImGuiContextScope& operator=(const ImGuiContextScope&) = delete;
			ImGuiContextScope(ImGuiContextScope&&) = delete;
			ImGuiContextScope& operator=(ImGuiContextScope&&) = delete;

		private:
			ImGuiContext* m_Context;
		};

		std::expected<int, Error> RunWindowed(const EditorOptions& options, EditorContext& context, McpServer& server)
		{
			auto window = Window::Create({.Title = "Lodestone Editor", .Width = 1600, .Height = 900});
			if (!window)
				return std::unexpected(window.error());
			auto presentationExtensions = (*window)->GetRequiredVulkanInstanceExtensions();
			if (!presentationExtensions)
				return std::unexpected(presentationExtensions.error());

			GraphicsDeviceConfig deviceConfig;
			deviceConfig.PresentationExtensions = std::move(*presentationExtensions);
			deviceConfig.SupportsPresentation =
				[&window](VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t queueFamily)
			{ return (*window)->SupportsVulkanPresentation(instance, physicalDevice, queueFamily); };
			deviceConfig.Driver = options.VulkanDriver;
			auto device = GraphicsDevice::Create(deviceConfig);
			if (!device)
				return std::unexpected(device.error());
			auto swapchain = Swapchain::Create(**device, **window, {.VSync = options.VSync});
			if (!swapchain)
				return std::unexpected(swapchain.error());
			auto sceneView = SceneView::Create(**device);
			if (!sceneView)
				return std::unexpected(sceneView.error());
			nvrhi::IDevice* nvrhiDevice = (*device)->GetNvrhiDevice();
			const nvrhi::CommandListHandle commandList = nvrhiDevice->createCommandList();

			MainThreadDispatcher dispatcher;
			// Set on the main thread, by editor_quit, which asks about unsaved changes itself
			bool quit = false;
			EditorToolHost host;
			host.Capture = [&sceneView, &context](uint32_t width, uint32_t height)
			{ return (*sceneView)->Capture(context, glm::uvec2(width, height)); };
			host.RequestQuit = [&quit] { quit = true; };
			RegisterEditorTools(server, context, std::move(host));
			auto http = StartHttp(options, server, dispatcher);
			if (!http)
				return std::unexpected(http.error());
			const DispatcherShutdown shutdown(dispatcher);

			// The layout is saved per user; without a settings directory, it isn't saved. Declared before Dear ImGui's
			// context, which saves it when it's destroyed
			std::string iniPath;
			if (const auto settings = GetSettingsDirectory())
			{
				std::error_code error;
				std::filesystem::create_directories(*settings, error);
				if (!error)
					iniPath = PathToUtf8(*settings / "EditorLayout.ini");
			}
			const ImGuiContextScope imguiContext;
			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigWindowsMoveFromTitleBarOnly = true;
			io.IniFilename = iniPath.empty() ? nullptr : iniPath.c_str();

			auto platform = ImGuiPlatform::Create(**window);
			if (!platform)
				return std::unexpected(platform.error());
			EditorUI::ApplyStyle((*platform)->GetContentScale());
			auto imguiRenderer = ImGuiRenderer::Create(nvrhiDevice, (*swapchain)->GetFramebufferInfo());
			if (!imguiRenderer)
				return std::unexpected(imguiRenderer.error());

			EditorUIHost uiHost;
			uiHost.PrepareViewport = [&sceneView, &imguiRenderer](glm::uvec2 size) -> std::expected<ImTextureID, Error>
			{
				const auto texture = (*sceneView)->PrepareViewport(size);
				if (!texture)
					return std::unexpected(texture.error());
				return (*imguiRenderer)->GetTextureId(*texture);
			};
			uiHost.McpUrl = *http ? (*http)->GetUrl() : std::string();
			EditorUI ui(context, std::move(uiHost));

			InputCommandBuilder input;
			std::string title;
			uint32_t frames = 0;
			Clock::time_point last = Clock::now();
			while (!ui.ShouldQuit() && (!options.FrameLimit || frames < *options.FrameLimit))
			{
				(*window)->PollEvents();
				if ((*window)->ShouldClose() && !ui.RequestQuit())
					(*window)->CancelClose();
				dispatcher.RunPending();
				if (quit)
					break;

				const Clock::time_point now = Clock::now();
				const double elapsed = std::chrono::duration<double>(now - last).count();
				last = now;
				// While playing, the viewport takes the keyboard and mouse when it has focus
				if (ui.IsViewportFocused())
				{
					input.AccumulateFrame((*window)->GetInput());
					context.Update(elapsed,
						[&input](uint64_t tick) { return std::vector<InputCommand>{input.BuildCommand(tick)}; });
				}
				else
				{
					context.Update(elapsed);
				}

				if (std::string newTitle = ui.GetWindowTitle(); newTitle != title)
				{
					title = std::move(newTitle);
					(*window)->SetTitle(title);
				}

				const auto began = (*swapchain)->BeginFrame();
				if (!began)
					return std::unexpected(began.error());
				if (!*began)
				{
					// Minimized: nothing to draw, but MCP requests still get answered
					dispatcher.WaitForWork(std::chrono::milliseconds(16));
					continue;
				}

				(*platform)->NewFrame();
				ImGui::NewFrame();
				ui.Draw(static_cast<float>(elapsed));
				ImGui::Render();

				// The scene, after the frame's edits, then the UI that shows it
				if (ui.IsViewportVisible())
					(*sceneView)->RenderViewport(context);
				commandList->open();
				nvrhi::utils::ClearColorAttachment(
					commandList, (*swapchain)->GetCurrentFramebuffer(), 0, nvrhi::Color(0.08f, 0.08f, 0.09f, 1.0f));
				(*imguiRenderer)->Render(commandList, (*swapchain)->GetCurrentFramebuffer(), ImGui::GetDrawData());
				commandList->close();
				nvrhiDevice->executeCommandList(commandList);
				if (auto presented = (*swapchain)->Present(); !presented)
					return std::unexpected(presented.error());
				++frames;
			}

			LS_CORE_INFO("Rendered {} frames", frames);
			// Validation errors are bugs, so automated runs must catch them. Validation runs in Debug builds only
			if (const uint32_t errors = (*device)->GetValidationErrorCount(); errors > 0)
			{
				LS_CORE_ERROR("The validation layers reported {} errors", errors);
				return EXIT_FAILURE;
			}
			return EXIT_SUCCESS;
		}

	}

	std::expected<int, Error> RunEditor(const EditorOptions& options)
	{
		if (options.McpStdio && !options.Headless)
			return std::unexpected(Error(ErrorCode::InvalidArgument, "--mcp-stdio needs --headless"));
		if (options.FrameLimit && options.Headless)
			return std::unexpected(Error(ErrorCode::InvalidArgument, "--frames applies to the windowed editor only"));

		EditorContext context;
		if (auto opened = OpenStartupProject(context, options); !opened)
			return std::unexpected(opened.error());
		McpServer server("Lodestone Editor", std::string(VersionString), std::string(McpInstructions));
		return options.Headless ? RunHeadless(options, context, server) : RunWindowed(options, context, server);
	}

}
