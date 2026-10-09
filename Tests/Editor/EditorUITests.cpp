#include "Lodestone/Editor/UI/EditorUI.h"

#include "Common/DescribeError.h"
#include "Common/TemporaryDirectory.h"
#include "Lodestone/Editor/Commands/EntityCommands.h"
#include "Lodestone/Scene/Entity.h"

#include <doctest/doctest.h>
#include <imgui.h>

#include <string>
#include <vector>

namespace Lodestone {

	namespace {

		// A component with a field of every reflected type, so the inspector draws every kind of widget
		struct EverythingComponent
		{
			bool Enabled = true;
			int32_t Count = -3;
			uint32_t Layers = 7;
			float Speed = 2.5f;
			glm::vec2 Size{1.0f, 2.0f};
			glm::vec3 Offset{0.0f};
			glm::vec4 Tint{1.0f};
			glm::quat Turn = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
			std::string Label = "Hello";
			UUID Target;
		};

		struct HiddenComponent
		{
			float Secret = 0.0f;
		};

		const ComponentRegistry& GetTestComponents()
		{
			static const ComponentRegistry* s_Registry = []
			{
				auto* registry = new ComponentRegistry();
				RegisterCoreComponents(*registry);
				registry->Register<EverythingComponent>("Everything", "A field of every type")
					.Field("Enabled", &EverythingComponent::Enabled)
					.Field("Count", &EverythingComponent::Count, {.Min = -10.0, .Max = 10.0})
					.Field("Layers", &EverythingComponent::Layers, {.Max = 32.0})
					.Field("Speed", &EverythingComponent::Speed, {.Description = "Meters per second", .Min = 0.0})
					.Field("Size", &EverythingComponent::Size)
					.Field("Offset", &EverythingComponent::Offset)
					.Field("Tint", &EverythingComponent::Tint, {.Min = 0.0, .Max = 1.0})
					.Field("Turn", &EverythingComponent::Turn)
					.Field("Label", &EverythingComponent::Label)
					.Field("Target", &EverythingComponent::Target, {.Flags = FieldFlags::ReadOnly});
				registry->Register<HiddenComponent>("Hidden", {}, ComponentFlags::Internal)
					.Field("Secret", &HiddenComponent::Secret);
				return registry;
			}();
			return *s_Registry;
		}

		// Dear ImGui without a window or a GPU: frames are built, and their draw data is never rendered
		class ImGuiScope
		{
		public:
			ImGuiScope()
				: m_Context(ImGui::CreateContext())
			{
				ImGuiIO& io = ImGui::GetIO();
				io.IniFilename = nullptr;
				io.DisplaySize = ImVec2(1600.0f, 900.0f);
				io.DeltaTime = 1.0f / 60.0f;
				io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
				// Shortcuts are the same on every platform in the tests: Ctrl isn't swapped for Cmd
				io.ConfigMacOSXBehaviors = false;
				// Textures are requested the way the editor's renderer handles them, and ignored
				io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
				EditorUI::ApplyStyle(1.0f);
			}

			~ImGuiScope() { ImGui::DestroyContext(m_Context); }

			ImGuiScope(const ImGuiScope&) = delete;
			ImGuiScope& operator=(const ImGuiScope&) = delete;
			ImGuiScope(ImGuiScope&&) = delete;
			ImGuiScope& operator=(ImGuiScope&&) = delete;

		private:
			ImGuiContext* m_Context;
		};

		struct Fixture
		{
			Fixture()
				: Context(GetTestComponents()), UI(Context, MakeHost())
			{
			}

			EditorUIHost MakeHost()
			{
				EditorUIHost host;
				host.PrepareViewport = [this](glm::uvec2 size) -> std::expected<ImTextureID, Error>
				{
					ViewportSizes.push_back(size);
					return static_cast<ImTextureID>(1);
				};
				host.McpUrl = "http://127.0.0.1:7850/mcp";
				return host;
			}

			void Frame(int count = 1)
			{
				for (int index = 0; index < count; ++index)
				{
					ImGui::NewFrame();
					UI.Draw(1.0f / 60.0f);
					ImGui::Render();
				}
			}

			// Presses and releases a key chord over a few frames, as a person would
			void Press(ImGuiKeyChord chord)
			{
				ImGuiIO& io = ImGui::GetIO();
				const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
				const auto setModifiers = [&io, chord](bool down)
				{
					if ((chord & ImGuiMod_Ctrl) != 0)
						io.AddKeyEvent(ImGuiMod_Ctrl, down);
					if ((chord & ImGuiMod_Shift) != 0)
						io.AddKeyEvent(ImGuiMod_Shift, down);
				};
				setModifiers(true);
				Frame();
				io.AddKeyEvent(key, true);
				Frame();
				io.AddKeyEvent(key, false);
				setModifiers(false);
				Frame(2);
			}

			UUID CreateEntity(std::string name, UUID parent = {})
			{
				auto command = CreateScope<CreateEntityCommand>(std::move(name), parent);
				const CreateEntityCommand& created = *command;
				const auto executed = Context.Execute(std::move(command));
				REQUIRE_MESSAGE(executed.has_value(), Testing::DescribeError(executed));
				return created.GetEntityId();
			}

			ImGuiScope Gui;
			EditorContext Context;
			EditorUI UI;
			std::vector<glm::uvec2> ViewportSizes;
		};

	}

	TEST_CASE("The editor UI draws every panel, without a project")
	{
		Fixture fixture;

		fixture.Frame(3);

		CHECK(fixture.UI.IsViewportVisible());
		REQUIRE_FALSE(fixture.ViewportSizes.empty());
		CHECK(fixture.ViewportSizes.back().x > 100);
		CHECK(fixture.ViewportSizes.back().y > 100);
		CHECK_FALSE(fixture.UI.ShouldQuit());
	}

	TEST_CASE("The editor UI draws a project's entities, the inspector for every field type, and the content browser")
	{
		Fixture fixture;
		const Testing::TemporaryDirectory directory("EditorUI");
		REQUIRE(fixture.Context.CreateProject(directory.GetPath() / "Game", "UI Test").has_value());
		const UUID parent = fixture.CreateEntity("Parent");
		const UUID child = fixture.CreateEntity("Child", parent);
		fixture.CreateEntity("Grandchild", child);
		Entity entity = fixture.Context.GetScene().FindEntity(child);
		entity.Add<EverythingComponent>();
		entity.Add<HiddenComponent>();
		fixture.Context.Select(child);
		REQUIRE(fixture.Context.SaveDocument("Scenes/Main.lscene").has_value());
		REQUIRE(fixture.Context.SavePrefab(parent, "Prefabs/Parent.lprefab").has_value());

		fixture.Frame(3);

		// Play mode makes the inspector read-only; it still draws
		REQUIRE(fixture.Context.StartPlay().has_value());
		fixture.Frame(2);
		REQUIRE(fixture.Context.StopPlay().has_value());
		fixture.Frame();
	}

	TEST_CASE("The window title names the project and document, and marks unsaved changes")
	{
		Fixture fixture;
		CHECK(fixture.UI.GetWindowTitle() == "Lodestone Editor - Untitled scene");

		const Testing::TemporaryDirectory directory("EditorUITitle");
		REQUIRE(fixture.Context.CreateProject(directory.GetPath() / "Game", "Breakout").has_value());
		fixture.CreateEntity("Paddle");
		CHECK(fixture.UI.GetWindowTitle() == "Lodestone Editor - Breakout - Untitled scene*");

		REQUIRE(fixture.Context.SaveDocument("Levels/One.lscene").has_value());
		CHECK(fixture.UI.GetWindowTitle() == "Lodestone Editor - Breakout - Levels/One.lscene");
	}

	TEST_CASE("Keyboard shortcuts undo, redo, duplicate, delete and play")
	{
		Fixture fixture;
		fixture.Frame(2);
		const UUID entity = fixture.CreateEntity("Box");
		fixture.Context.Select(entity);

		fixture.Press(ImGuiMod_Ctrl | ImGuiKey_D);
		CHECK(fixture.Context.GetScene().GetEntityCount() == 2);
		// The copy is selected
		CHECK(fixture.Context.GetSelection() != entity);

		fixture.Press(ImGuiKey_Delete);
		CHECK(fixture.Context.GetScene().GetEntityCount() == 1);

		fixture.Press(ImGuiMod_Ctrl | ImGuiKey_Z);
		CHECK(fixture.Context.GetScene().GetEntityCount() == 2);
		fixture.Press(ImGuiMod_Ctrl | ImGuiKey_Y);
		CHECK(fixture.Context.GetScene().GetEntityCount() == 1);
		fixture.Press(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);
		CHECK(fixture.Context.GetScene().GetEntityCount() == 1);
		fixture.Press(ImGuiMod_Ctrl | ImGuiKey_Z);
		fixture.Press(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);
		CHECK(fixture.Context.GetScene().GetEntityCount() == 1);

		fixture.Press(ImGuiMod_Ctrl | ImGuiKey_P);
		CHECK(fixture.Context.GetPlayState() == PlayState::Playing);
		// Edits don't happen while playing
		fixture.Press(ImGuiMod_Ctrl | ImGuiKey_Z);
		CHECK(fixture.Context.GetScene().GetEntityCount() == 1);
		fixture.Press(ImGuiMod_Ctrl | ImGuiKey_P);
		CHECK(fixture.Context.GetPlayState() == PlayState::Editing);
	}

	TEST_CASE("Ctrl+N starts a new scene, asking first when there are unsaved changes")
	{
		Fixture fixture;
		fixture.Frame(2);

		fixture.Press(ImGuiMod_Ctrl | ImGuiKey_N);
		CHECK(fixture.Context.GetScene().GetEntityCount() == 0);

		fixture.CreateEntity("Unsaved");
		fixture.Press(ImGuiMod_Ctrl | ImGuiKey_N);
		// The question is open, and the scene is still there
		CHECK(fixture.Context.GetScene().GetEntityCount() == 1);
		fixture.Press(ImGuiKey_Escape);
		CHECK(fixture.Context.GetScene().GetEntityCount() == 1);
	}

	TEST_CASE("Quitting asks about unsaved changes first")
	{
		Fixture fixture;
		fixture.Frame();
		fixture.CreateEntity("Unsaved");

		CHECK_FALSE(fixture.UI.RequestQuit());
		fixture.Frame(2);
		CHECK_FALSE(fixture.UI.ShouldQuit());

		Fixture saved;
		saved.Frame();
		CHECK(saved.UI.RequestQuit());
		CHECK(saved.UI.ShouldQuit());
	}

	TEST_CASE("The viewport isn't rendered when it can't get an image")
	{
		const ImGuiScope imgui;
		EditorContext context(GetTestComponents());
		EditorUIHost host;
		host.PrepareViewport = [](glm::uvec2) -> std::expected<ImTextureID, Error>
		{ return std::unexpected(Error(ErrorCode::DeviceError, "No device")); };
		EditorUI ui(context, std::move(host));

		for (int frame = 0; frame < 3; ++frame)
		{
			ImGui::NewFrame();
			ui.Draw(1.0f / 60.0f);
			ImGui::Render();
		}

		CHECK_FALSE(ui.IsViewportVisible());
	}

}
