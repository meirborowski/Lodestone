#pragma once

// Internal to the scripting module (see ScriptRuntime.h)

#include "Lodestone/Asset/Model.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Scripting/ScriptRuntime.h"

#include <entt/entity/registry.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace Lodestone {

	class ComponentType;
	class PhysicsWorld;
	class Scene;
	class ScriptAssets;
	class SimulationContext;

	// An entity as scripts hold it: a handle by UUID, so one kept after the entity is destroyed fails cleanly instead
	// of reaching another entity
	struct ScriptEntity
	{
		UUID Id;

		bool operator==(const ScriptEntity& other) const = default;
	};

	// One of an entity's components, whose fields scripts read and write through the reflection registry
	struct ScriptComponentHandle
	{
		UUID Entity;
		const ComponentType* Type = nullptr;
	};

	// What the scripting API works on. The script system keeps it current
	struct ScriptApiContext
	{
		Scene* World = nullptr;
		const ScriptAssets* Assets = nullptr;
		PhysicsWorld* Physics = nullptr;
		ModelLookup Models;
		// The tick being run, while one is: scripts' input comes from it
		SimulationContext* Tick = nullptr;
		// An entity's script instance, or nil
		std::function<sol::object(UUID entity)> FindInstance;
	};

	struct InputCommand;
	struct AnimatorComponent;

	// The scripting API (see docs/Decisions/0022-scripting.md), bound into a runtime's globals by Bind(). Its functions
	// call back into this object, so it must outlive every call scripts make: the script system keeps both
	class ScriptApi
	{
	public:
		// The runtime and the context must outlive the API
		ScriptApi(ScriptRuntime& runtime, ScriptApiContext& context)
			: m_Runtime(&runtime), m_Context(&context)
		{
		}

		void Bind();

	private:
		Scene& GetScene() const { return *m_Context->World; }
		entt::registry& GetRegistry() const;

		// The entity's handle; an error raised if it's gone
		entt::entity Resolve(lua_State* state, const ScriptEntity& entity) const;
		const ComponentType& FindComponentType(lua_State* state, std::string_view name) const;
		std::string DescribeEntity(const ScriptEntity& entity) const;
		const InputCommand& GetInput(sol::optional<uint32_t> player) const;
		PhysicsWorld& RequirePhysics(lua_State* state) const;
		// The animator an entity is or belongs to (see FindAnimator), and its model if it's loaded
		std::pair<AnimatorComponent*, const Model*> RequireAnimator(lua_State* state, const ScriptEntity& entity) const;
		uint32_t ResolveClip(lua_State* state, const Model* model, const sol::object& clip) const;

		void BindMath();
		void BindLog();
		void BindEntity();
		void BindComponent();
		void BindScene();
		void BindInput();
		void BindPhysics();
		void BindAnimation();

	private:
		ScriptRuntime* m_Runtime;
		ScriptApiContext* m_Context;
	};

	// The Time table's fields, set before each tick's callbacks
	void SetScriptTime(ScriptRuntime& runtime, uint64_t tick, float deltaTime);

}
