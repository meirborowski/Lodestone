#include "Lodestone/Scripting/ScriptSystem.h"

#include "Lodestone/Core/Log.h"
#include "Lodestone/Scene/Components.h"
#include "Lodestone/Scene/Scene.h"
#include "Lodestone/Scripting/ScriptAssets.h"
#include "Lodestone/Scripting/ScriptBindings.h"
#include "Lodestone/Scripting/ScriptComponents.h"
#include "Lodestone/Scripting/ScriptRuntime.h"
#include "Lodestone/Serialization/Json.h"

#include <spdlog/fmt/fmt.h>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Lodestone {

	namespace {

		// How deep property defaults' tables are copied for each instance
		constexpr int MaxCopyDepth = 8;

		std::string_view GetCallbackName(PhysicsEventType type)
		{
			switch (type)
			{
				case PhysicsEventType::CollisionBegin:
					return "OnCollisionBegin";
				case PhysicsEventType::CollisionEnd:
					return "OnCollisionEnd";
				case PhysicsEventType::TriggerEnter:
					return "OnTriggerEnter";
				case PhysicsEventType::TriggerExit:
					return "OnTriggerExit";
			}
			return "";
		}

		// A property default, copied for an instance, so instances don't share tables or vectors and change each
		// other's
		sol::object CopyValue(sol::state& lua, const sol::object& value, int depth)
		{
			if (value.is<glm::vec2>())
				return sol::make_object(lua, value.as<glm::vec2>());
			if (value.is<glm::vec3>())
				return sol::make_object(lua, value.as<glm::vec3>());
			if (value.is<glm::vec4>())
				return sol::make_object(lua, value.as<glm::vec4>());
			if (value.is<glm::quat>())
				return sol::make_object(lua, value.as<glm::quat>());
			if (value.get_type() != sol::type::table || depth >= MaxCopyDepth)
				return value;
			sol::table copy = lua.create_table();
			for (const auto& [key, element] : value.as<sol::table>())
				copy[key] = CopyValue(lua, element, depth + 1);
			return copy;
		}

		std::string DescribePropertyType(const sol::object& value)
		{
			if (value.is<glm::vec2>())
				return "vec2";
			if (value.is<glm::vec3>())
				return "vec3";
			if (value.is<glm::vec4>())
				return "vec4";
			return sol::type_name(value.lua_state(), value.get_type());
		}

		// A JSON property value as the Lua type of its default; an error saying why it can't be
		std::expected<sol::object, std::string> ConvertProperty(
			const sol::state_view& lua, const sol::object& defaultValue, const Json::Value& value)
		{
			const auto number = [](const Json::Value& element) { return element.get<float>(); };
			const auto isVector = [&value](size_t size)
			{
				return value.is_array() && value.size() == size &&
					std::ranges::all_of(value, [](const Json::Value& element) { return element.is_number(); });
			};
			if (defaultValue.get_type() == sol::type::number && value.is_number_integer())
				return sol::make_object(lua, value.get<lua_Integer>());
			if (defaultValue.get_type() == sol::type::number && value.is_number())
				return sol::make_object(lua, value.get<double>());
			if (defaultValue.get_type() == sol::type::boolean && value.is_boolean())
				return sol::make_object(lua, value.get<bool>());
			if (defaultValue.get_type() == sol::type::string && value.is_string())
				return sol::make_object(lua, value.get<std::string>());
			if (defaultValue.is<glm::vec2>() && isVector(2))
				return sol::make_object(lua, glm::vec2(number(value[0]), number(value[1])));
			if (defaultValue.is<glm::vec3>() && isVector(3))
				return sol::make_object(lua, glm::vec3(number(value[0]), number(value[1]), number(value[2])));
			if (defaultValue.is<glm::vec4>() && isVector(4))
				return sol::make_object(
					lua, glm::vec4(number(value[0]), number(value[1]), number(value[2]), number(value[3])));
			return std::unexpected(
				fmt::format("{} doesn't fit a default of type {}", value.dump(), DescribePropertyType(defaultValue)));
		}

	}

	class ScriptSystem::Implementation
	{
	public:
		Implementation(Scene& scene, ScriptSystemConfig config);

		void Update(SimulationContext& context);
		void DeliverEvents(SimulationContext& context, std::span<const PhysicsEvent> events);
		void Shutdown();
		void Reload();
		bool ReloadChanged();
		std::expected<std::string, Error> Run(std::string_view source, std::string_view name);

		size_t GetInstanceCount() const { return m_Instances.size(); }
		uint64_t GetErrorCount() const { return m_ErrorCount; }
		size_t GetMemoryUsed() const { return m_Runtime.GetMemoryUsed(); }

	private:
		struct LoadedScript
		{
			// Empty when the asset couldn't be found
			std::string Path;
			// The version of the file last loaded, or tried: nothing if it wasn't there
			std::optional<uint64_t> Version;
			// The script's table, from the last time it loaded; invalid if it never has
			sol::table Table;
		};

		struct Instance
		{
			UUID Script;
			// The Script component's properties the instance was given
			std::string Properties;
			sol::table Self;
			// Raised an error: no more callbacks until scripts reload
			bool Failed = false;
		};

	private:
		// The script asset's table, loaded the first time it's needed; nullptr if it can't load
		const LoadedScript* GetScript(UUID id);
		// Loads (or reloads) a script, keeping the table it had if this load fails
		void LoadScript(UUID id, LoadedScript& script);
		void ReportError(std::string_view message);
		std::optional<sol::table> CreateInstance(
			UUID entity, const ScriptComponent& component, const LoadedScript& script);
		void ApplyProperties(sol::table& self, const sol::table& script, const std::string& properties, UUID entity);
		void Reconcile();
		template <typename... Args>
		void CallMethod(UUID entity, Instance& instance, std::string_view name, Args&&... arguments);
		std::string DescribeEntity(UUID entity) const;

	private:
		Scene* m_Scene;
		ScriptSystemConfig m_Config;
		// Before the runtime, whose bindings use it until the state closes
		ScriptApiContext m_Context;
		ScriptRuntime m_Runtime;
		ScriptApi m_Api;
		// By script asset and by entity, so they're visited in the same order every run. After the runtime: they
		// hold references into its state
		std::map<UUID, LoadedScript> m_Scripts;
		std::map<UUID, Instance> m_Instances;
		uint64_t m_ErrorCount = 0;
	};

	ScriptSystem::Implementation::Implementation(Scene& scene, ScriptSystemConfig config)
		: m_Scene(&scene),
		  m_Config(std::move(config)),
		  m_Context{.World = &scene, .Assets = m_Config.Assets, .Physics = m_Config.Physics, .Models = m_Config.Models},
		  m_Runtime(m_Config.MemoryLimit, m_Config.InstructionLimit, m_Config.Assets),
		  m_Api(m_Runtime, m_Context)
	{
		m_Context.FindInstance = [this](UUID entity)
		{
			const auto found = m_Instances.find(entity);
			if (found == m_Instances.end())
				return sol::make_object(m_Runtime.GetLua(), sol::lua_nil);
			return sol::make_object(m_Runtime.GetLua(), found->second.Self);
		};
		m_Api.Bind();
	}

	std::string ScriptSystem::Implementation::DescribeEntity(UUID entity) const
	{
		const entt::entity handle = m_Scene->FindHandle(entity);
		if (handle == entt::null)
			return fmt::format("{}", entity);
		return m_Scene->GetRegistry().get<NameComponent>(handle).Name;
	}

	void ScriptSystem::Implementation::ReportError(std::string_view message)
	{
		++m_ErrorCount;
		LS_ERROR("{}", message);
	}

	const ScriptSystem::Implementation::LoadedScript* ScriptSystem::Implementation::GetScript(UUID id)
	{
		auto [found, inserted] = m_Scripts.try_emplace(id);
		if (inserted)
			LoadScript(id, found->second);
		return found->second.Table.valid() ? &found->second : nullptr;
	}

	void ScriptSystem::Implementation::LoadScript(UUID id, LoadedScript& script)
	{
		const std::optional<std::string> path =
			m_Config.Assets != nullptr ? m_Config.Assets->FindScript(id) : std::nullopt;
		if (!path)
		{
			ReportError(fmt::format("Script {} isn't in the project", id));
			return;
		}
		script.Path = *path;
		// Recorded before anything can fail, so the script is tried again once its file changes, and not before
		script.Version = m_Config.Assets->GetVersion(*path);
		auto file = m_Config.Assets->ReadScript(*path);
		if (!file)
		{
			ReportError(fmt::format("Script {} can't be read: {}", *path, file.error()));
			return;
		}
		script.Version = file->Version;
		auto chunk = m_Runtime.Compile(file->Source, file->Path);
		if (!chunk)
		{
			ReportError(fmt::format("Script {} doesn't compile: {}", *path, chunk.error().GetMessageText()));
			return;
		}
		auto result = m_Runtime.Call(*chunk);
		if (!result)
		{
			ReportError(fmt::format("Script {} failed to load: {}", *path, result.error().GetMessageText()));
			return;
		}
		if (result->get_type() != sol::type::table)
		{
			ReportError(fmt::format("Script {} must return a table of callbacks", *path));
			return;
		}
		script.Table = result->as<sol::table>();
		// Instances find the script's callbacks through it, as their metatable
		script.Table["__index"] = script.Table;
	}

	void ScriptSystem::Implementation::ApplyProperties(
		sol::table& self, const sol::table& script, const std::string& properties, UUID entity)
	{
		sol::state& lua = m_Runtime.GetLua();
		const sol::object defaults = script["Properties"];
		if (defaults.get_type() == sol::type::table)
			for (const auto& [key, value] : defaults.as<sol::table>())
				self[key] = CopyValue(lua, value, 0);

		auto overrides = Json::Parse(properties);
		if (!overrides || !overrides->is_object())
		{
			ReportError(fmt::format(
				"The script properties of '{}' aren't a JSON object: {}", DescribeEntity(entity), properties));
			return;
		}
		for (const auto& [key, value] : overrides->items())
		{
			sol::object defaultValue;
			if (defaults.get_type() == sol::type::table)
				defaultValue = defaults.as<sol::table>()[key];
			if (!defaultValue.valid() || defaultValue.get_type() == sol::type::lua_nil)
			{
				ReportError(fmt::format("The script of '{}' has no property '{}'", DescribeEntity(entity), key));
				continue;
			}
			auto converted = ConvertProperty(lua, defaultValue, value);
			if (!converted)
			{
				ReportError(
					fmt::format("Script property '{}' of '{}': {}", key, DescribeEntity(entity), converted.error()));
				continue;
			}
			self[key] = *converted;
		}
	}

	std::optional<sol::table> ScriptSystem::Implementation::CreateInstance(
		UUID entity, const ScriptComponent& component, const LoadedScript& script)
	{
		sol::table self = m_Runtime.GetLua().create_table();
		ApplyProperties(self, script.Table, component.Properties, entity);
		self["Entity"] = ScriptEntity{.Id = entity};
		self[sol::metatable_key] = script.Table;
		return self;
	}

	template <typename... Args>
	void ScriptSystem::Implementation::CallMethod(
		UUID entity, Instance& instance, std::string_view name, Args&&... arguments)
	{
		// OnDestroy runs even when the entity is gone, so scripts can let go of what they hold; nothing else does
		if (instance.Failed || (name != "OnDestroy" && !m_Scene->Contains(entity)))
			return;
		const sol::object method = instance.Self[name];
		if (method.get_type() != sol::type::function)
			return;
		auto result =
			m_Runtime.Call(method.as<sol::protected_function>(), instance.Self, std::forward<Args>(arguments)...);
		if (result)
			return;
		instance.Failed = true;
		ReportError(fmt::format("Script error in {} of '{}' (its script stops until scripts reload): {}", name,
			DescribeEntity(entity), result.error().GetMessageText()));
	}

	void ScriptSystem::Implementation::Reconcile()
	{
		entt::registry& registry = m_Scene->GetRegistry();

		// Instances whose entity, component or script went away end, in the order of their entities' UUIDs
		for (auto instance = m_Instances.begin(); instance != m_Instances.end();)
		{
			const entt::entity handle = m_Scene->FindHandle(instance->first);
			const auto* component = handle != entt::null ? registry.try_get<ScriptComponent>(handle) : nullptr;
			if (component != nullptr && component->Script == instance->second.Script)
			{
				// Properties changed while running are applied to the instance as it is
				if (component->Properties != instance->second.Properties)
				{
					instance->second.Properties = component->Properties;
					if (const LoadedScript* script = GetScript(instance->second.Script))
						ApplyProperties(instance->second.Self, script->Table, component->Properties, instance->first);
				}
				++instance;
				continue;
			}
			CallMethod(instance->first, instance->second, "OnDestroy");
			instance = m_Instances.erase(instance);
		}

		// New Script components get instances, in the order of the components, then OnCreate in that order. No
		// script runs while the components are visited, since scripts can add and remove them
		std::vector<std::pair<UUID, ScriptComponent>> added;
		for (const auto [handle, id, component] : registry.view<const IDComponent, const ScriptComponent>().each())
			if (!m_Instances.contains(id.ID))
				added.emplace_back(id.ID, component);
		std::vector<UUID> created;
		for (const auto& [entity, component] : added)
		{
			const LoadedScript* script = GetScript(component.Script);
			if (script == nullptr)
				continue;
			std::optional<sol::table> self = CreateInstance(entity, component, *script);
			if (!self)
				continue;
			m_Instances.emplace(entity,
				Instance{.Script = component.Script, .Properties = component.Properties, .Self = std::move(*self)});
			created.push_back(entity);
		}
		for (const UUID entity : created)
			if (const auto instance = m_Instances.find(entity); instance != m_Instances.end())
				CallMethod(entity, instance->second, "OnCreate");
	}

	void ScriptSystem::Implementation::Update(SimulationContext& context)
	{
		m_Context.Tick = &context;
		SetScriptTime(m_Runtime, context.GetTick(), context.GetDeltaTime());
		Reconcile();

		// In the order of the Script components, collected first since scripts can add and remove them
		std::vector<UUID> order;
		const auto scripted = m_Scene->GetRegistry().view<const IDComponent, const ScriptComponent>();
		for (const entt::entity handle : scripted)
			order.push_back(scripted.get<const IDComponent>(handle).ID);
		for (const UUID entity : order)
			if (const auto instance = m_Instances.find(entity); instance != m_Instances.end())
				CallMethod(entity, instance->second, "OnUpdate", context.GetDeltaTime());
		m_Context.Tick = nullptr;
	}

	void ScriptSystem::Implementation::DeliverEvents(SimulationContext& context, std::span<const PhysicsEvent> events)
	{
		m_Context.Tick = &context;
		for (const PhysicsEvent& event : events)
		{
			const std::string_view callback = GetCallbackName(event.Type);
			for (const auto& [self, other] :
				{std::pair(event.First, event.Second), std::pair(event.Second, event.First)})
				if (const auto instance = m_Instances.find(self); instance != m_Instances.end())
					CallMethod(self, instance->second, callback, ScriptEntity{.Id = other});
		}
		m_Context.Tick = nullptr;
	}

	void ScriptSystem::Implementation::Shutdown()
	{
		for (auto& [entity, instance] : m_Instances)
			CallMethod(entity, instance, "OnDestroy");
		m_Instances.clear();
	}

	bool ScriptSystem::Implementation::ReloadChanged()
	{
		if (m_Config.Assets == nullptr)
			return false;
		bool changed = false;
		for (const auto& [id, script] : m_Scripts)
			changed |= script.Path.empty() ? m_Config.Assets->FindScript(id).has_value()
										   : m_Config.Assets->GetVersion(script.Path) != script.Version;
		for (const auto& [path, version] : m_Runtime.GetModules())
			changed |= m_Config.Assets->GetVersion(path) != version;
		if (changed)
			Reload();
		return changed;
	}

	void ScriptSystem::Implementation::Reload()
	{
		LS_INFO("Loading the scripts again");
		m_Runtime.ForgetModules();
		for (auto& [id, script] : m_Scripts)
			LoadScript(id, script);
		for (auto& [entity, instance] : m_Instances)
		{
			const auto script = m_Scripts.find(instance.Script);
			if (script == m_Scripts.end() || !script->second.Table.valid())
				continue;
			instance.Self[sol::metatable_key] = script->second.Table;
			instance.Failed = false;
			CallMethod(entity, instance, "OnReload");
		}
	}

	std::expected<std::string, Error> ScriptSystem::Implementation::Run(std::string_view source, std::string_view name)
	{
		auto chunk = m_Runtime.Compile(source, name);
		if (!chunk)
			return std::unexpected(chunk.error());
		auto result = m_Runtime.Call(*chunk);
		if (!result)
			return std::unexpected(result.error());
		lua_State* state = m_Runtime.GetLua().lua_state();
		result->push(state);
		size_t length = 0;
		const char* text = luaL_tolstring(state, -1, &length);
		std::string value(text, length);
		lua_pop(state, 2);
		return value;
	}

	ScriptSystem::ScriptSystem(Scene& scene, ScriptSystemConfig config)
		: m_Implementation(CreateScope<Implementation>(scene, std::move(config)))
	{
	}

	ScriptSystem::~ScriptSystem() = default;

	void ScriptSystem::Update(SimulationContext& context)
	{
		m_Implementation->Update(context);
	}

	void ScriptSystem::DeliverEvents(SimulationContext& context, std::span<const PhysicsEvent> events)
	{
		m_Implementation->DeliverEvents(context, events);
	}

	void ScriptSystem::Shutdown()
	{
		m_Implementation->Shutdown();
	}

	void ScriptSystem::Reload()
	{
		m_Implementation->Reload();
	}

	bool ScriptSystem::ReloadChanged()
	{
		return m_Implementation->ReloadChanged();
	}

	std::expected<std::string, Error> ScriptSystem::Run(std::string_view source, std::string_view name)
	{
		return m_Implementation->Run(source, name);
	}

	std::expected<void, Error> ScriptSystem::CheckSyntax(std::string_view source, std::string_view path)
	{
		lua_State* state = luaL_newstate();
		if (state == nullptr)
			return std::unexpected(Error(ErrorCode::OutOfMemory, "No memory for a Lua state"));
		const std::string chunkName = "@" + std::string(path);
		std::expected<void, Error> result;
		if (luaL_loadbufferx(state, source.data(), source.size(), chunkName.c_str(), "t") != LUA_OK)
			result = std::unexpected(Error(ErrorCode::ScriptError, lua_tostring(state, -1)));
		lua_close(state);
		return result;
	}

	size_t ScriptSystem::GetInstanceCount() const
	{
		return m_Implementation->GetInstanceCount();
	}

	uint64_t ScriptSystem::GetErrorCount() const
	{
		return m_Implementation->GetErrorCount();
	}

	size_t ScriptSystem::GetMemoryUsed() const
	{
		return m_Implementation->GetMemoryUsed();
	}

}
