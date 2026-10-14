#pragma once

#include "Lodestone/Asset/Model.h"
#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Physics/PhysicsWorld.h"
#include "Lodestone/Simulation/Simulation.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace Lodestone {

	class Scene;
	class ScriptAssets;

	struct ScriptSystemConfig
	{
		// Where scripts and the prefabs they instantiate come from; it must outlive the system. Without it, no script
		// loads
		const ScriptAssets* Assets = nullptr;
		// The models animators play, so scripts can name clips
		ModelLookup Models;
		// The physics world scripts query; without one, the physics functions report that there's none
		PhysicsWorld* Physics = nullptr;
		// The most memory scripts can use together, in bytes
		size_t MemoryLimit = size_t{256} * 1024 * 1024;
		// The most Lua instructions a callback can run before it's stopped as a runaway
		uint64_t InstructionLimit = 50'000'000;
	};

	// A scene's scripts (see docs/Features/Scripting.md and docs/Decisions/0022-scripting.md): a sandboxed Lua VM
	// running the Script components' behaviors as part of the simulation. Update() runs before physics, and
	// DeliverEvents() after it with the tick's collision and trigger events. Script errors are logged with the script
	// and line, and stop only the instance that raised them.
	//
	// Used from one thread at a time
	class ScriptSystem
	{
	public:
		// The scene and the config's assets and physics world must outlive the system
		explicit ScriptSystem(Scene& scene, ScriptSystemConfig config = {});
		~ScriptSystem();

		ScriptSystem(const ScriptSystem&) = delete;
		ScriptSystem& operator=(const ScriptSystem&) = delete;
		ScriptSystem(ScriptSystem&&) = delete;
		ScriptSystem& operator=(ScriptSystem&&) = delete;

		// The tick's scripting: instances made for new Script components (OnCreate) and removed for those gone
		// (OnDestroy), then every instance's OnUpdate, in the order of the components
		void Update(SimulationContext& context);
		// The tick's collision and trigger events, to both entities' scripts
		void DeliverEvents(SimulationContext& context, std::span<const PhysicsEvent> events);
		// Ends every instance (OnDestroy), as when play mode stops
		void Shutdown();

		// Loads every script again, and the modules they require, keeping instances' fields: each instance takes its
		// script's new table, is enabled again if an error stopped it, and gets OnReload (see
		// docs/Decisions/0022-scripting.md). A script that fails to load keeps its old version
		void Reload();
		// Reload() if any of the scripts' files changed since they were loaded; whether any had
		bool ReloadChanged();

		// Runs a chunk of Lua in the scripts' sandbox, outside any instance - for tests and tools. Returns what the
		// chunk returns, as text, or the error it raised
		[[nodiscard]] std::expected<std::string, Error> Run(std::string_view source, std::string_view name = "chunk");
		// Compiles a script without running it, to check its syntax: the error names the line
		[[nodiscard]] static std::expected<void, Error> CheckSyntax(std::string_view source, std::string_view path);

		size_t GetInstanceCount() const;
		// Errors scripts raised, and scripts that failed to load, since the system was made
		uint64_t GetErrorCount() const;
		// The memory the scripts use, in bytes
		size_t GetMemoryUsed() const;

	private:
		class Implementation;

	private:
		Scope<Implementation> m_Implementation;
	};

}
