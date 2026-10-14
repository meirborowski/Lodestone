#include "Lodestone/Scripting/LuaAnnotations.h"

#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Input/InputCodes.h"
#include "Lodestone/Reflection/ComponentRegistry.h"
#include "Lodestone/Serialization/Json.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <iterator>
#include <set>
#include <string>
#include <system_error>

namespace Lodestone {

	namespace {

		// Scripting/LuaApi.lua, embedded by CMake as character literals
		constexpr char LuaApi[] = {
#include "Lodestone/Scripting/LuaApi.inc"
		};

		std::string_view GetLuaApi()
		{
			return {std::data(LuaApi), std::size(LuaApi)};
		}

		std::string_view GetLuaType(FieldType type)
		{
			switch (type)
			{
				case FieldType::Bool:
					return "boolean";
				case FieldType::Int:
				case FieldType::UInt:
					return "integer";
				case FieldType::Float:
					return "number";
				case FieldType::Vec2:
					return "vec2";
				case FieldType::Vec3:
					return "vec3";
				case FieldType::Vec4:
					return "vec4";
				case FieldType::Quat:
					return "quat";
				case FieldType::String:
					return "string";
				case FieldType::UUID:
					// Read as text, nil for the nil UUID; written as text, an Entity or nil
					return "string?";
			}
			return "any";
		}

		// Descriptions on one line, as annotation comments need them
		std::string OneLine(std::string_view text)
		{
			std::string line(text);
			for (char& character : line)
				if (character == '\n' || character == '\r')
					character = ' ';
			return line;
		}

		void AddAlias(
			std::string& text, std::string_view name, std::string_view description, const std::set<std::string>& values)
		{
			text += fmt::format("---{}\n---@alias {}\n", description, name);
			for (const std::string& value : values)
				text += fmt::format("---| \"{}\"\n", value);
			text += '\n';
		}

	}

	std::string GenerateLuaAnnotations(const ComponentRegistry& components)
	{
		std::string text(GetLuaApi());
		text += "\n------------------------------------------------------------------------------------------------"
				"------------------------\n-- Generated from the engine's registries\n----------------------------"
				"--------------------------------------------------------------------------------------------\n\n";

		std::set<std::string> componentNames;
		for (const ComponentType* type : components.GetTypes())
		{
			if (type->IsInternal())
				continue;
			componentNames.insert(type->GetName());
			text += fmt::format(
				"---{}\n---@class {}Component : Component\n", OneLine(type->GetDescription()), type->GetName());
			for (const FieldInfo& field : type->GetFields())
				text += fmt::format("---@field {} {} {}{}\n", field.GetName(), GetLuaType(field.GetType()),
					OneLine(field.GetDescription()), field.IsReadOnly() ? " (read-only)" : "");
			text += '\n';
		}

		// Entity:Get and Entity:Add give each component's class
		text += "---The entity's component, or nil if it has none\n";
		for (const std::string& name : componentNames)
			text += fmt::format("---@overload fun(self: Entity, component: \"{}\"): {}Component?\n", name, name);
		text += "---@param component ComponentName\n---@return Component?\nfunction Entity:Get(component) end\n\n";
		text += "---Adds a component, optionally with values for its fields. Every field is checked first: a bad one "
				"adds nothing\n";
		for (const std::string& name : componentNames)
			text += fmt::format(
				"---@overload fun(self: Entity, component: \"{}\", fields?: table<string, any>): {}Component\n", name,
				name);
		text += "---@param component ComponentName\n---@param fields? table<string, any>\n---@return Component\n"
				"function Entity:Add(component, fields) end\n\n";

		AddAlias(text, "ComponentName", "A component's name", componentNames);

		std::set<std::string> keys;
		for (uint32_t code = 0; code < KeyCodeCount; ++code)
			if (const std::optional<Key> key = KeyFromCode(static_cast<int>(code)))
				keys.emplace(ToString(*key));
		AddAlias(text, "KeyName", "A key's name", keys);
		std::set<std::string> mouseButtons;
		for (uint32_t index = 0; index < MouseButtonCount; ++index)
			mouseButtons.emplace(ToString(static_cast<MouseButton>(index)));
		AddAlias(text, "MouseButtonName", "A mouse button's name", mouseButtons);
		std::set<std::string> gamepadButtons;
		for (uint32_t index = 0; index < GamepadButtonCount; ++index)
			gamepadButtons.emplace(ToString(static_cast<GamepadButton>(index)));
		AddAlias(text, "GamepadButtonName", "A gamepad button's name", gamepadButtons);
		std::set<std::string> gamepadAxes;
		for (uint32_t index = 0; index < GamepadAxisCount; ++index)
			gamepadAxes.emplace(ToString(static_cast<GamepadAxis>(index)));
		AddAlias(text, "GamepadAxisName", "A gamepad axis's name", gamepadAxes);
		return text;
	}

	std::string GenerateLuaLanguageServerConfig()
	{
		const Json::Value config = {
			{"$schema", "https://raw.githubusercontent.com/LuaLS/vscode-lua/master/setting/schema.json"},
			{"runtime.version", "Lua 5.4"},
			{"runtime.builtin",
				{{"io", "disable"}, {"debug", "disable"}, {"ffi", "disable"}, {"jit", "disable"},
					{"bit32", "disable"}}},
			{"workspace.library", Json::Value::array({std::string(LuaAnnotationsDirectory)})},
			{"workspace.checkThirdParty", false},
			{"diagnostics.globals", Json::Value::array()},
			{"diagnostics.severity", {{"undefined-field", "Error"}, {"undefined-global", "Error"}}},
		};
		return config.dump(2) + "\n";
	}

	std::expected<void, Error> WriteLuaLanguageServerFiles(
		const std::filesystem::path& projectDirectory, const ComponentRegistry& components)
	{
		const std::filesystem::path directory = projectDirectory / LuaAnnotationsDirectory;
		std::error_code error;
		std::filesystem::create_directories(directory, error);
		if (error)
			return std::unexpected(Error(ErrorCode::IoError, fmt::format("Can't create {}: {}", directory, error)));

		// Rewritten only when they've changed, so an unchanged project's files stay as they are
		const std::filesystem::path annotations = directory / LuaAnnotationsFileName;
		const std::string text = GenerateLuaAnnotations(components);
		const auto current = ReadFile(annotations);
		if (!current || *current != text)
			if (auto written = WriteFileAtomically(annotations, text); !written)
				return written;

		const std::filesystem::path config = projectDirectory / LuaLanguageServerConfigFileName;
		if (std::filesystem::exists(config, error))
			return {};
		if (error)
			return std::unexpected(Error(ErrorCode::IoError, fmt::format("Can't check {}: {}", config, error)));
		return WriteFileAtomically(config, GenerateLuaLanguageServerConfig());
	}

}
