#include "Lodestone/Scripting/ScriptRuntime.h"

#include "Lodestone/Core/Assert.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Scripting/ScriptAssets.h"

#include <cstdlib>
#include <string>

namespace Lodestone {

	namespace {

		// The instruction budget is checked every this many instructions
		constexpr int HookInterval = 1000;

		bool IsModuleCharacter(char character)
		{
			return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
				(character >= '0' && character <= '9') || character == '_';
		}

	}

	ScriptRuntime::ScriptRuntime(size_t memoryLimit, uint64_t instructionLimit, const ScriptAssets* assets)
		: m_Memory{.Limit = memoryLimit},
		  m_InstructionLimit(instructionLimit),
		  m_Assets(assets),
		  m_Lua(sol::default_at_panic, &ScriptRuntime::Allocate, &m_Memory)
	{
		lua_State* state = m_Lua.lua_state();
		// Every thread of the state - coroutines too - finds the runtime here, and inherits the hook
		*static_cast<ScriptRuntime**>(lua_getextraspace(state)) = this;
		lua_sethook(state, &ScriptRuntime::CountInstructions, LUA_MASKCOUNT, HookInterval);
		lua_pushcfunction(state, &ScriptRuntime::AddTraceback);
		m_MessageHandler = sol::reference(state, -1);
		lua_pop(state, 1);
		Sandbox();
	}

	void ScriptRuntime::Sandbox()
	{
		m_Lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::utf8,
			sol::lib::coroutine, sol::lib::os, sol::lib::package);
		sol::table globals = m_Lua.globals();

		// No files: scripts load other scripts only through require
		globals["dofile"] = sol::lua_nil;
		globals["loadfile"] = sol::lua_nil;
		globals.set_function("print", &ScriptRuntime::Print);

		// From os, only the time
		const sol::table os = globals["os"];
		sol::table safeOs = m_Lua.create_table();
		safeOs["time"] = os["time"];
		safeOs["clock"] = os["clock"];
		safeOs["date"] = os["date"];
		globals["os"] = safeOs;

		// require searches the project's scripts, and preloaded modules; no native modules, no file system
		sol::table package = globals["package"];
		package["loaded"]["os"] = safeOs;
		package["loadlib"] = sol::lua_nil;
		package["searchpath"] = sol::lua_nil;
		package["path"] = "";
		package["cpath"] = "";
		const sol::table searchers = package["searchers"];
		sol::table projectSearchers = m_Lua.create_table();
		projectSearchers[1] = searchers[1];
		projectSearchers[2] = &ScriptRuntime::SearchProject;
		package["searchers"] = projectSearchers;

		// load only compiles text: malicious bytecode can corrupt memory. An explicit environment argument, even nil,
		// is passed on as it was given. And random numbers start the same way every run, as the simulation needs
		const auto prepared = m_Lua.safe_script(R"(
			local rawload = load
			load = function(chunk, name, mode, ...)
				if select("#", ...) > 0 then
					return rawload(chunk, name, "t", ...)
				end
				return rawload(chunk, name, "t")
			end
			math.randomseed(0)
		)",
			sol::script_pass_on_error, "=sandbox");
		LS_CORE_ASSERT(prepared.valid(), "The script sandbox failed to set up");
	}

	ScriptRuntime& ScriptRuntime::Get(lua_State* state)
	{
		return **static_cast<ScriptRuntime**>(lua_getextraspace(state));
	}

	void* ScriptRuntime::Allocate(void* userData, void* block, size_t oldSize, size_t newSize)
	{
		auto& memory = *static_cast<MemoryBudget*>(userData);
		// Without a block, oldSize says what kind of object it's for, not its size
		const size_t current = block != nullptr ? oldSize : 0;
		if (newSize == 0)
		{
			memory.Used -= current;
			std::free(block); // NOLINT(cppcoreguidelines-no-malloc): Lua's allocator contract is realloc and free
			return nullptr;
		}
		if (newSize > current && newSize - current > memory.Limit - memory.Used)
			return nullptr;
		void* resized = std::realloc(block, newSize); // NOLINT(cppcoreguidelines-no-malloc)
		if (resized == nullptr)
			// Lua counts on shrinking never failing: the block it has is big enough
			return newSize <= current ? block : nullptr;
		memory.Used = memory.Used - current + newSize;
		return resized;
	}

	void ScriptRuntime::CountInstructions(lua_State* state, [[maybe_unused]] lua_Debug* debug)
	{
		ScriptRuntime& runtime = Get(state);
		runtime.m_Instructions += HookInterval;
		// Raised at every check once over the budget, so a script that catches it with pcall can't keep going
		if (runtime.m_Instructions > runtime.m_InstructionLimit)
			luaL_error(state, "the script ran more than %I instructions without returning, so it was stopped",
				static_cast<lua_Integer>(runtime.m_InstructionLimit));
	}

	int ScriptRuntime::SearchProject(lua_State* state)
	{
		ScriptRuntime& runtime = Get(state);
		const char* name = luaL_checkstring(state, 1);
		const std::optional<std::string> path = GetModulePath(name);
		if (!path)
		{
			lua_pushfstring(
				state, "\n\t'%s' isn't a module name: they're like Scripts.Util, for Scripts/Util.lua", name);
			return 1;
		}
		auto file = runtime.m_Assets != nullptr
			? runtime.m_Assets->ReadScript(*path)
			: std::expected<ScriptFile, Error>(std::unexpected(Error(ErrorCode::NotFound, "No project")));
		// Recorded whether it loads or not, so fixing it, changing it or adding it loads the scripts again
		std::optional<uint64_t>& version = runtime.m_Modules[*path];
		if (file)
			version = file->Version;
		else if (runtime.m_Assets != nullptr)
			version = runtime.m_Assets->GetVersion(*path);
		else
			version.reset();
		if (!file)
		{
			lua_pushfstring(state, "\n\tno script '%s' in the project", path->c_str());
			return 1;
		}
		const std::string chunkName = "@" + file->Path;
		if (luaL_loadbufferx(state, file->Source.data(), file->Source.size(), chunkName.c_str(), "t") != LUA_OK)
			return lua_error(state);
		lua_pushstring(state, file->Path.c_str());
		return 2;
	}

	int ScriptRuntime::AddTraceback(lua_State* state)
	{
		const char* message = luaL_tolstring(state, 1, nullptr);
		luaL_traceback(state, state, message, 1);
		return 1;
	}

	std::string ScriptRuntime::JoinArguments(lua_State* state)
	{
		std::string line;
		const int count = lua_gettop(state);
		for (int index = 1; index <= count; ++index)
		{
			size_t length = 0;
			const char* text = luaL_tolstring(state, index, &length);
			if (index > 1)
				line += '\t';
			line.append(text, length);
			lua_pop(state, 1);
		}
		return line;
	}

	int ScriptRuntime::Print(lua_State* state)
	{
		LS_INFO("{}", JoinArguments(state));
		return 0;
	}

	std::expected<sol::protected_function, Error> ScriptRuntime::Compile(std::string_view source, std::string_view path)
	{
		const sol::load_result loaded = m_Lua.load(source, "@" + std::string(path), sol::load_mode::text);
		if (!loaded.valid())
		{
			const sol::error error = loaded;
			return std::unexpected(Error(ErrorCode::ScriptError, error.what()));
		}
		return loaded.get<sol::protected_function>();
	}

	void ScriptRuntime::ForgetModules()
	{
		sol::table loaded = m_Lua["package"]["loaded"];
		for (const auto& [path, version] : std::exchange(m_Modules, {}))
		{
			// Back from a path to the module name it was required by
			std::string name = path.substr(0, path.size() - 4);
			for (char& character : name)
				if (character == '/')
					character = '.';
			loaded[name] = sol::lua_nil;
		}
	}

	std::optional<std::string> ScriptRuntime::GetModulePath(std::string_view name)
	{
		if (name.empty() || name.front() == '.' || name.back() == '.')
			return std::nullopt;
		std::string path;
		path.reserve(name.size() + 4);
		char previous = '\0';
		for (const char character : name)
		{
			if (character == '.')
			{
				if (previous == '.')
					return std::nullopt;
				path += '/';
			}
			else if (IsModuleCharacter(character))
			{
				path += character;
			}
			else
			{
				return std::nullopt;
			}
			previous = character;
		}
		return path + ".lua";
	}

}
