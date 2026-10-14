#pragma once

#include "Lodestone/Core/Error.h"
#include "Lodestone/Scene/Components.h"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace Lodestone {

	class ComponentRegistry;

	// Where a project's lua-language-server files go, relative to its directory: the annotations, which the engine
	// keeps up to date, and the configuration that has lua-language-server check the project's scripts against them
	inline constexpr std::string_view LuaAnnotationsDirectory = ".lodestone";
	inline constexpr std::string_view LuaAnnotationsFileName = "Lodestone.lua";
	inline constexpr std::string_view LuaLanguageServerConfigFileName = ".luarc.json";

	// The scripting API's annotations for lua-language-server (see docs/Decisions/0022-scripting.md): the hand-written
	// declarations of the API (Scripting/LuaApi.lua), followed by what comes from the engine's registries - a class
	// for every component that isn't internal, Entity:Get typed for each, and the names of components, keys, mouse
	// buttons and gamepad buttons and axes
	std::string GenerateLuaAnnotations(const ComponentRegistry& components = GetEngineComponentRegistry());

	// A project's .luarc.json: Lua 5.4 with what the sandbox leaves of its standard library, and the annotations in
	// LuaAnnotationsDirectory as a library
	std::string GenerateLuaLanguageServerConfig();

	// Gives a project what lua-language-server needs to complete and check its scripts: writes the annotations when
	// they've changed, and the configuration when the project has none - after that, it's the project's to change
	[[nodiscard]] std::expected<void, Error> WriteLuaLanguageServerFiles(const std::filesystem::path& projectDirectory,
		const ComponentRegistry& components = GetEngineComponentRegistry());

}
