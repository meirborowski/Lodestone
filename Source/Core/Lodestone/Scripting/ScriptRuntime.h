#pragma once

// Internal to the scripting module: Lua and sol2 are linked privately by LodestoneCore, so only its sources include
// this

#include "Lodestone/Core/Error.h"

#include <sol/sol.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Lodestone {

	class ScriptAssets;

	// The Lua VM scripts run in, sandboxed as docs/Features/Scripting.md#sandbox describes: a trimmed standard library,
	// text-only loading, require limited to the project's scripts, a memory limit and an instruction budget per call
	class ScriptRuntime
	{
	public:
		// The assets, if given, must outlive the runtime; without them require finds nothing
		ScriptRuntime(size_t memoryLimit, uint64_t instructionLimit, const ScriptAssets* assets);
		~ScriptRuntime() = default;

		ScriptRuntime(const ScriptRuntime&) = delete;
		ScriptRuntime& operator=(const ScriptRuntime&) = delete;
		ScriptRuntime(ScriptRuntime&&) = delete;
		ScriptRuntime& operator=(ScriptRuntime&&) = delete;

		sol::state& GetLua() { return m_Lua; }

		// Compiles a chunk of Lua text (never bytecode), named after its path so errors name it, without running it
		[[nodiscard]] std::expected<sol::protected_function, Error> Compile(
			std::string_view source, std::string_view path);

		// Calls a Lua function with a fresh instruction budget. Returns its first result, or the error it raised with a
		// traceback
		template <typename... Args>
		[[nodiscard]] std::expected<sol::object, Error> Call(
			const sol::protected_function& function, Args&&... arguments)
		{
			m_Instructions = 0;
			const sol::protected_function call(function, m_MessageHandler);
			const sol::protected_function_result result = call(std::forward<Args>(arguments)...);
			if (!result.valid())
			{
				const sol::error error = result;
				return std::unexpected(Error(ErrorCode::ScriptError, error.what()));
			}
			if (result.return_count() == 0)
				return sol::make_object(m_Lua, sol::lua_nil);
			return result.get<sol::object>();
		}

		// Forgets the modules require loaded, so the next require loads them again
		void ForgetModules();
		// The paths of the modules require looked for - found or not, loaded or not - with their files' versions then
		// (nothing for a file that wasn't there), to see when they change
		const std::map<std::string, std::optional<uint64_t>>& GetModules() const { return m_Modules; }

		size_t GetMemoryUsed() const { return m_Memory.Used; }

		// A module name as a script path: "Scripts.Util" is "Scripts/Util.lua". Nothing for names that aren't
		// letters, digits and underscores between dots
		static std::optional<std::string> GetModulePath(std::string_view name);
		// A C function's arguments as print shows them: each through tostring, separated by tabs
		static std::string JoinArguments(lua_State* state);

	private:
		struct MemoryBudget
		{
			size_t Used = 0;
			size_t Limit = 0;
		};

	private:
		void Sandbox();
		static ScriptRuntime& Get(lua_State* state);
		static void* Allocate(void* userData, void* block, size_t oldSize, size_t newSize);
		static void CountInstructions(lua_State* state, lua_Debug* debug);
		static int SearchProject(lua_State* state);
		static int AddTraceback(lua_State* state);
		static int Print(lua_State* state);

	private:
		// Declared before the state, which allocates through it until it's closed
		MemoryBudget m_Memory;
		uint64_t m_InstructionLimit;
		uint64_t m_Instructions = 0;
		const ScriptAssets* m_Assets;
		std::map<std::string, std::optional<uint64_t>> m_Modules;
		sol::state m_Lua;
		sol::reference m_MessageHandler;
	};

}
