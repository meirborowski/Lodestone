#include "Lodestone/Scripting/ScriptBindings.h"

#include "Lodestone/Animation/Animation.h"
#include "Lodestone/Animation/AnimationComponents.h"
#include "Lodestone/Core/Assert.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Core/Rotation.h"
#include "Lodestone/Input/InputCodes.h"
#include "Lodestone/Input/InputCommand.h"
#include "Lodestone/Physics/PhysicsComponents.h"
#include "Lodestone/Physics/PhysicsWorld.h"
#include "Lodestone/Reflection/ComponentRegistry.h"
#include "Lodestone/Scene/Components.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/PrefabSerializer.h"
#include "Lodestone/Scene/Scene.h"
#include "Lodestone/Scripting/ScriptAssets.h"
#include "Lodestone/Simulation/Simulation.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/trigonometric.hpp>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace Lodestone {

	namespace {

		constexpr glm::quat Identity = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);

		// Raises a Lua error from a binding. Lua is compiled as C++, so this unwinds the binding's frames, running
		// their destructors (see docs/Decisions/0003-lua-compiled-as-cpp.md)
		[[noreturn]] void Raise(lua_State* state, std::string_view message)
		{
			lua_pushlstring(state, message.data(), message.size());
			lua_error(state);
			std::unreachable();
		}

		int LogInfo(lua_State* state)
		{
			LS_INFO("{}", ScriptRuntime::JoinArguments(state));
			return 0;
		}

		int LogWarn(lua_State* state)
		{
			LS_WARN("{}", ScriptRuntime::JoinArguments(state));
			return 0;
		}

		int LogError(lua_State* state)
		{
			LS_ERROR("{}", ScriptRuntime::JoinArguments(state));
			return 0;
		}

		constexpr float MaxAnimationSpeed = 100.0f;
		constexpr std::array<std::string_view, 3> RaycastOptionNames{"MaxDistance", "IncludeTriggers", "Ignore"};
		constexpr std::array<std::string_view, 2> AnimationOptionNames{"Loop", "Speed"};

		// Raises an error naming the keys an options table can have, if it has another: a misspelled option would
		// otherwise be ignored without a word
		void CheckOptionNames(
			lua_State* state, const sol::table& options, std::span<const std::string_view> names, std::string_view what)
		{
			for (const auto& [key, value] : options)
			{
				if (key.get_type() == sol::type::string && std::ranges::contains(names, key.as<std::string_view>()))
					continue;
				std::string list(names.front());
				for (size_t index = 1; index < names.size(); ++index)
					list += fmt::format("{}{}", index + 1 == names.size() ? " and " : ", ", names[index]);
				Raise(state, fmt::format("{} options are {}", what, list));
			}
		}

		float CheckAnimationSpeed(lua_State* state, float speed)
		{
			if (!std::isfinite(speed) || std::abs(speed) > MaxAnimationSpeed)
				Raise(state, "An animation's speed is from -100 to 100");
			return speed;
		}

		bool IsFinite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		template <typename Vector>
		Vector SafeNormalize(const Vector& value)
		{
			const float length = glm::length(value);
			return length > 0.0f && std::isfinite(length) ? value / length : Vector(0.0f);
		}

		template <typename Vector>
		std::string DescribeVector(const char* name, const Vector& value)
		{
			std::string text = fmt::format("{}({}", name, value[0]);
			for (glm::length_t index = 1; index < Vector::length(); ++index)
				text += fmt::format(", {}", value[index]);
			return text + ")";
		}

		// vec2, vec3 and vec4: made from nothing (zero), one number (every component) or each component; with the
		// arithmetic operators, per component or with a number
		template <typename Vector>
		void BindVector(sol::state& lua, const char* name)
		{
			auto type = lua.new_usertype<Vector>(
				name, sol::no_constructor, sol::meta_function::addition,
				[](const Vector& a, const Vector& b) { return a + b; }, sol::meta_function::subtraction,
				[](const Vector& a, const Vector& b) { return a - b; }, sol::meta_function::unary_minus,
				[](const Vector& a) { return -a; }, sol::meta_function::multiplication,
				sol::overload([](const Vector& a, const Vector& b) { return a * b; }, [](const Vector& a, float scale)
					{ return a * scale; }, [](float scale, const Vector& a) { return scale * a; }),
				sol::meta_function::division,
				sol::overload([](const Vector& a, const Vector& b) { return a / b; },
					[](const Vector& a, float scale) { return a / scale; }),
				sol::meta_function::equal_to, [](const Vector& a, const Vector& b) { return a == b; },
				sol::meta_function::to_string, [name](const Vector& a) { return DescribeVector(name, a); }, "Length",
				[](const Vector& a) { return glm::length(a); }, "Normalized", [](const Vector& a)
				{ return SafeNormalize(a); }, "Dot", [](const Vector& a, const Vector& b) { return glm::dot(a, b); },
				"Distance", [](const Vector& a, const Vector& b) { return glm::distance(a, b); }, "Lerp",
				[](const Vector& a, const Vector& b, float t) { return a + (b - a) * t; });
			type["x"] = &Vector::x;
			type["y"] = &Vector::y;
			if constexpr (Vector::length() == 2)
			{
				type[sol::call_constructor] = sol::factories([] { return Vector(0.0f); },
					[](float value) { return Vector(value); }, [](float x, float y) { return Vector(x, y); });
			}
			else if constexpr (Vector::length() == 3)
			{
				type["z"] = &Vector::z;
				type[sol::call_constructor] = sol::factories([] { return Vector(0.0f); }, [](float value)
					{ return Vector(value); }, [](float x, float y, float z) { return Vector(x, y, z); });
				type["Cross"] = [](const Vector& a, const Vector& b) { return glm::cross(a, b); };
			}
			else
			{
				type["z"] = &Vector::z;
				type["w"] = &Vector::w;
				type[sol::call_constructor] = sol::factories([] { return Vector(0.0f); }, [](float value)
					{ return Vector(value); }, [](float x, float y, float z, float w) { return Vector(x, y, z, w); });
			}
		}

		void BindQuaternion(sol::state& lua)
		{
			lua.new_usertype<glm::quat>(
				"quat", sol::call_constructor,
				sol::factories([] { return Identity; },
					[](float w, float x, float y, float z) { return glm::quat::wxyz(w, x, y, z); }),
				"x", &glm::quat::x, "y", &glm::quat::y, "z", &glm::quat::z, "w", &glm::quat::w,
				sol::meta_function::multiplication,
				sol::overload([](const glm::quat& a, const glm::quat& b) { return a * b; },
					[](const glm::quat& rotation, const glm::vec3& vector) { return rotation * vector; }),
				sol::meta_function::equal_to, [](const glm::quat& a, const glm::quat& b) { return a == b; },
				sol::meta_function::to_string,
				[](const glm::quat& q) { return fmt::format("quat({}, {}, {}, {})", q.w, q.x, q.y, q.z); }, "FromEuler",
				&QuaternionFromEuler, "AngleAxis",
				[](float degrees, const glm::vec3& axis)
				{
					const glm::vec3 unit = SafeNormalize(axis);
					return unit == glm::vec3(0.0f) ? Identity : glm::angleAxis(glm::radians(degrees), unit);
				},
				"LookRotation",
				[](const glm::vec3& forward, sol::optional<glm::vec3> up)
				{
					const glm::vec3 direction = SafeNormalize(forward);
					const glm::vec3 above = SafeNormalize(up.value_or(glm::vec3(0.0f, 1.0f, 0.0f)));
					if (direction == glm::vec3(0.0f) || above == glm::vec3(0.0f) ||
						std::abs(glm::dot(direction, above)) > 0.9999f)
						return Identity;
					return glm::quatLookAt(direction, above);
				},
				"ToEuler", &EulerFromQuaternion, "Inverse", [](const glm::quat& q) { return glm::inverse(q); },
				"Normalized",
				[](const glm::quat& q)
				{
					const float length = glm::length(q);
					return length > 0.0f && std::isfinite(length) ? q / length : Identity;
				},
				"Slerp", [](const glm::quat& a, const glm::quat& b, float t) { return glm::slerp(a, b, t); });
		}

		// A reflected value as a Lua value: UUIDs as their text (nil for the nil UUID), vectors and quaternions as the
		// math types
		sol::object ToLua(sol::state_view lua, const FieldValue& value)
		{
			return std::visit(
				[&lua](const auto& alternative) -> sol::object
				{
					using T = std::decay_t<decltype(alternative)>;
					if constexpr (std::is_same_v<T, UUID>)
						return alternative.IsNil() ? sol::make_object(lua, sol::lua_nil)
												   : sol::make_object(lua, alternative.ToString());
					else
						return sol::make_object(lua, alternative);
				},
				value);
		}

		// A Lua value as a reflected value of a type; an error saying why it isn't one
		std::expected<FieldValue, std::string> FromLua(const sol::object& value, FieldType type)
		{
			const auto expected = [type] { return fmt::format("a {}", ToString(type)); };
			switch (type)
			{
				case FieldType::Bool:
					if (value.get_type() == sol::type::boolean)
						return FieldValue(value.as<bool>());
					break;
				case FieldType::Int:
				case FieldType::UInt: {
					if (value.get_type() != sol::type::number)
						break;
					const double number = value.as<double>();
					const double low = type == FieldType::Int ? std::numeric_limits<int32_t>::min() : 0.0;
					const double high = type == FieldType::Int ? std::numeric_limits<int32_t>::max()
															   : std::numeric_limits<uint32_t>::max();
					if (number != std::floor(number) || number < low || number > high)
						return std::unexpected(fmt::format("{} isn't {}", number, expected()));
					if (type == FieldType::Int)
						return FieldValue(static_cast<int32_t>(number));
					return FieldValue(static_cast<uint32_t>(number));
				}
				case FieldType::Float:
					if (value.get_type() == sol::type::number)
						return FieldValue(value.as<float>());
					break;
				case FieldType::Vec2:
					if (value.is<glm::vec2>())
						return FieldValue(value.as<glm::vec2>());
					break;
				case FieldType::Vec3:
					if (value.is<glm::vec3>())
						return FieldValue(value.as<glm::vec3>());
					break;
				case FieldType::Vec4:
					if (value.is<glm::vec4>())
						return FieldValue(value.as<glm::vec4>());
					break;
				case FieldType::Quat:
					if (value.is<glm::quat>())
						return FieldValue(value.as<glm::quat>());
					break;
				case FieldType::String:
					if (value.get_type() == sol::type::string)
						return FieldValue(value.as<std::string>());
					break;
				case FieldType::UUID:
					if (value.get_type() == sol::type::lua_nil)
						return FieldValue(UUID());
					if (value.is<ScriptEntity>())
						return FieldValue(value.as<ScriptEntity>().Id);
					if (value.get_type() == sol::type::string)
					{
						if (const std::optional<UUID> id = UUID::Parse(value.as<std::string>()))
							return FieldValue(*id);
						return std::unexpected(fmt::format("'{}' isn't a UUID", value.as<std::string>()));
					}
					break;
			}
			return std::unexpected(
				fmt::format("it needs {}, not a {}", expected(), sol::type_name(value.lua_state(), value.get_type())));
		}

	}

	entt::registry& ScriptApi::GetRegistry() const
	{
		return m_Context->World->GetRegistry();
	}

	entt::entity ScriptApi::Resolve(lua_State* state, const ScriptEntity& entity) const
	{
		const entt::entity handle = GetScene().FindHandle(entity.Id);
		if (handle == entt::null)
			Raise(state, fmt::format("Entity {} doesn't exist (any more)", entity.Id));
		return handle;
	}

	const ComponentType& ScriptApi::FindComponentType(lua_State* state, std::string_view name) const
	{
		const ComponentType* type = GetScene().GetComponentRegistry().Find(name);
		if (type == nullptr || type->IsInternal())
			Raise(state, fmt::format("There's no component called '{}'", name));
		return *type;
	}

	std::string ScriptApi::DescribeEntity(const ScriptEntity& entity) const
	{
		const entt::entity handle = GetScene().FindHandle(entity.Id);
		if (handle == entt::null)
			return fmt::format("Entity({}, destroyed)", entity.Id);
		return fmt::format("Entity({}, {})", GetRegistry().get<NameComponent>(handle).Name, entity.Id);
	}

	const InputCommand& ScriptApi::GetInput(sol::optional<uint32_t> player) const
	{
		static const InputCommand Empty;
		return m_Context->Tick != nullptr ? m_Context->Tick->GetInput(player.value_or(0)) : Empty;
	}

	PhysicsWorld& ScriptApi::RequirePhysics(lua_State* state) const
	{
		if (m_Context->Physics == nullptr)
			Raise(state, "There's no physics world");
		return *m_Context->Physics;
	}

	void ScriptApi::Bind()
	{
		BindMath();
		BindLog();
		BindEntity();
		BindComponent();
		BindScene();
		BindInput();
		BindPhysics();
		BindAnimation();
		sol::table time = m_Runtime->GetLua().create_named_table("Time");
		time["Delta"] = 0.0f;
		time["Tick"] = 0;
		time["Elapsed"] = 0.0;
	}

	void ScriptApi::BindMath()
	{
		sol::state& lua = m_Runtime->GetLua();
		BindVector<glm::vec2>(lua, "vec2");
		BindVector<glm::vec3>(lua, "vec3");
		BindVector<glm::vec4>(lua, "vec4");
		BindQuaternion(lua);
	}

	void ScriptApi::BindLog()
	{
		sol::table log = m_Runtime->GetLua().create_named_table("Log");
		log.set_function("Info", &LogInfo);
		log.set_function("Warn", &LogWarn);
		log.set_function("Error", &LogError);
	}

	void ScriptApi::BindEntity()
	{
		sol::state& lua = m_Runtime->GetLua();
		auto entity = lua.new_usertype<ScriptEntity>("Entity", sol::no_constructor);
		entity[sol::meta_function::equal_to] = [](const ScriptEntity& a, const ScriptEntity& b) { return a == b; };
		entity[sol::meta_function::to_string] = [this](const ScriptEntity& self) { return DescribeEntity(self); };
		entity["IsValid"] = [this](const ScriptEntity& self) { return GetScene().Contains(self.Id); };
		entity["GetId"] = [](const ScriptEntity& self) { return self.Id.ToString(); };
		entity["GetName"] = [this](const ScriptEntity& self, sol::this_state state)
		{ return GetRegistry().get<NameComponent>(Resolve(state, self)).Name; };
		entity["SetName"] = [this](const ScriptEntity& self, const std::string& name, sol::this_state state)
		{
			const entt::entity handle = Resolve(state, self);
			GetRegistry().patch<NameComponent>(handle, [&name](NameComponent& component) { component.Name = name; });
		};
		entity["Has"] = [this](const ScriptEntity& self, std::string_view component, sol::this_state state)
		{ return FindComponentType(state, component).Has(GetRegistry(), Resolve(state, self)); };
		entity["Get"] = [this](const ScriptEntity& self, std::string_view component, sol::this_state state)
		{
			const ComponentType& type = FindComponentType(state, component);
			if (!type.Has(GetRegistry(), Resolve(state, self)))
				return sol::make_object(state, sol::lua_nil);
			return sol::make_object(state, ScriptComponentHandle{.Entity = self.Id, .Type = &type});
		};
		entity["Add"] = [this](const ScriptEntity& self, std::string_view component, sol::optional<sol::table> fields,
							sol::this_state state)
		{
			const ComponentType& type = FindComponentType(state, component);
			if (type.IsRequired())
				Raise(state, fmt::format("Every entity has a {} component already", type.GetName()));
			const entt::entity handle = Resolve(state, self);
			entt::registry& registry = GetRegistry();
			if (type.Has(registry, handle))
				Raise(state, fmt::format("{} already has a {} component", DescribeEntity(self), type.GetName()));
			// Every field is checked before the component is added, so a bad one adds nothing
			std::vector<std::pair<const FieldInfo*, FieldValue>> values;
			if (fields)
				for (const auto& [key, value] : *fields)
				{
					if (key.get_type() != sol::type::string)
						Raise(state, "Component fields are named by strings");
					const FieldInfo* field = type.FindField(key.as<std::string>());
					if (field == nullptr)
						Raise(state, fmt::format("{} has no field '{}'", type.GetName(), key.as<std::string>()));
					auto converted = FromLua(value, field->GetType());
					if (!converted)
						Raise(state, fmt::format("{}.{}: {}", type.GetName(), field->GetName(), converted.error()));
					if (auto valid = field->Validate(*converted); !valid)
						Raise(state, valid.error().GetMessageText());
					values.emplace_back(field, std::move(*converted));
				}
			type.Add(registry, handle);
			for (const auto& [field, value] : values)
			{
				[[maybe_unused]] const auto set = type.SetField(registry, handle, *field, value);
				LS_CORE_ASSERT(set.has_value(), "A checked field value couldn't be set");
			}
			return ScriptComponentHandle{.Entity = self.Id, .Type = &type};
		};
		entity["Remove"] = [this](const ScriptEntity& self, std::string_view component, sol::this_state state)
		{
			const ComponentType& type = FindComponentType(state, component);
			if (type.IsRequired())
				Raise(state, fmt::format("Every entity needs its {} component", type.GetName()));
			const entt::entity handle = Resolve(state, self);
			if (!type.Has(GetRegistry(), handle))
				return false;
			type.Remove(GetRegistry(), handle);
			return true;
		};
		entity["GetParent"] = [this](const ScriptEntity& self, sol::this_state state)
		{
			const UUID parent = GetRegistry().get<HierarchyComponent>(Resolve(state, self)).Parent;
			return parent.IsNil() ? sol::make_object(state, sol::lua_nil)
								  : sol::make_object(state, ScriptEntity{.Id = parent});
		};
		entity["SetParent"] = [this](
								  const ScriptEntity& self, sol::optional<ScriptEntity> parent, sol::this_state state)
		{
			const Entity child(Resolve(state, self), &GetScene());
			const Entity newParent = parent ? Entity(Resolve(state, *parent), &GetScene()) : Entity();
			if (auto moved = GetScene().SetParent(child, newParent); !moved)
				Raise(state, moved.error().GetMessageText());
		};
		entity["GetChildren"] = [this](const ScriptEntity& self, sol::this_state state)
		{
			sol::table children = sol::state_view(state).create_table();
			for (const UUID child : GetRegistry().get<HierarchyComponent>(Resolve(state, self)).Children)
				children.add(ScriptEntity{.Id = child});
			return children;
		};
		entity["FindChild"] = [this](const ScriptEntity& self, std::string_view name, sol::this_state state)
		{
			for (const UUID child : GetRegistry().get<HierarchyComponent>(Resolve(state, self)).Children)
				if (GetRegistry().get<NameComponent>(GetScene().FindHandle(child)).Name == name)
					return sol::make_object(state, ScriptEntity{.Id = child});
			return sol::make_object(state, sol::lua_nil);
		};
		entity["GetWorldPosition"] = [this](const ScriptEntity& self, sol::this_state state)
		{
			const Entity found(Resolve(state, self), &GetScene());
			return glm::vec3(GetScene().GetWorldMatrix(found)[3]);
		};
		entity["GetWorldRotation"] = [this](const ScriptEntity& self, sol::this_state state)
		{
			const glm::mat4 world = GetScene().GetWorldMatrix(Entity(Resolve(state, self), &GetScene()));
			const glm::mat3 axes(SafeNormalize(glm::vec3(world[0])), SafeNormalize(glm::vec3(world[1])),
				SafeNormalize(glm::vec3(world[2])));
			return glm::normalize(glm::quat_cast(axes));
		};
		entity["GetForward"] = [this](const ScriptEntity& self, sol::this_state state)
		{
			const glm::mat4 world = GetScene().GetWorldMatrix(Entity(Resolve(state, self), &GetScene()));
			return SafeNormalize(-glm::vec3(world[2]));
		};
		entity["GetScript"] = [this](const ScriptEntity& self, sol::this_state state)
		{
			Resolve(state, self);
			return m_Context->FindInstance(self.Id);
		};
		entity["Destroy"] = [this](const ScriptEntity& self, sol::this_state state)
		{ GetScene().DestroyEntity(Entity(Resolve(state, self), &GetScene())); };
	}

	void ScriptApi::BindComponent()
	{
		sol::state& lua = m_Runtime->GetLua();
		auto component = lua.new_usertype<ScriptComponentHandle>("Component", sol::no_constructor);
		component[sol::meta_function::to_string] = [this](const ScriptComponentHandle& self)
		{ return fmt::format("{} of {}", self.Type->GetName(), DescribeEntity(ScriptEntity{.Id = self.Entity})); };
		component[sol::meta_function::index] =
			[this](const ScriptComponentHandle& self, std::string_view name, sol::this_state state)
		{
			const entt::entity handle = Resolve(state, ScriptEntity{.Id = self.Entity});
			const void* data = self.Type->TryGet(std::as_const(GetRegistry()), handle);
			if (data == nullptr)
				Raise(state,
					fmt::format("{} no longer has a {} component", DescribeEntity(ScriptEntity{.Id = self.Entity}),
						self.Type->GetName()));
			const FieldInfo* field = self.Type->FindField(name);
			if (field == nullptr)
				Raise(state, fmt::format("{} has no field '{}'", self.Type->GetName(), name));
			return ToLua(state, field->Get(data));
		};
		component[sol::meta_function::new_index] = [this](const ScriptComponentHandle& self, std::string_view name,
													   const sol::object& value, sol::this_state state)
		{
			const entt::entity handle = Resolve(state, ScriptEntity{.Id = self.Entity});
			if (!self.Type->Has(GetRegistry(), handle))
				Raise(state,
					fmt::format("{} no longer has a {} component", DescribeEntity(ScriptEntity{.Id = self.Entity}),
						self.Type->GetName()));
			const FieldInfo* field = self.Type->FindField(name);
			if (field == nullptr)
				Raise(state, fmt::format("{} has no field '{}'", self.Type->GetName(), name));
			if (field->IsReadOnly())
				Raise(state, fmt::format("{}.{} is read-only", self.Type->GetName(), name));
			auto converted = FromLua(value, field->GetType());
			if (!converted)
				Raise(state, fmt::format("{}.{}: {}", self.Type->GetName(), name, converted.error()));
			if (auto set = self.Type->SetField(GetRegistry(), handle, *field, *converted); !set)
				Raise(state, set.error().GetMessageText());
		};
	}

	void ScriptApi::BindScene()
	{
		sol::table scene = m_Runtime->GetLua().create_named_table("Scene");
		scene["Create"] =
			[this](const sol::optional<std::string>& name, sol::optional<ScriptEntity> parent, sol::this_state state)
		{
			const Entity created = GetScene().CreateEntity(name.value_or("Entity"));
			if (parent)
				if (auto moved = GetScene().SetParent(created, Entity(Resolve(state, *parent), &GetScene())); !moved)
					Raise(state, moved.error().GetMessageText());
			return ScriptEntity{.Id = created.GetId()};
		};
		scene["Get"] = [this](std::string_view id, sol::this_state state)
		{
			const std::optional<UUID> parsed = UUID::Parse(id);
			if (!parsed || !GetScene().Contains(*parsed))
				return sol::make_object(state, sol::lua_nil);
			return sol::make_object(state, ScriptEntity{.Id = *parsed});
		};
		scene["Find"] = [this](std::string_view name, sol::this_state state)
		{
			for (const auto [handle, id, entityName] :
				GetRegistry().view<const IDComponent, const NameComponent>().each())
				if (entityName.Name == name)
					return sol::make_object(state, ScriptEntity{.Id = id.ID});
			return sol::make_object(state, sol::lua_nil);
		};
		scene["FindAll"] = [this](std::string_view name, sol::this_state state)
		{
			sol::table found = sol::state_view(state).create_table();
			for (const auto [handle, id, entityName] :
				GetRegistry().view<const IDComponent, const NameComponent>().each())
				if (entityName.Name == name)
					found.add(ScriptEntity{.Id = id.ID});
			return found;
		};
		scene["FindWith"] = [this](std::string_view component, sol::this_state state)
		{
			const ComponentType& type = FindComponentType(state, component);
			sol::table found = sol::state_view(state).create_table();
			for (const auto [handle, id] : GetRegistry().view<const IDComponent>().each())
				if (type.Has(GetRegistry(), handle))
					found.add(ScriptEntity{.Id = id.ID});
			return found;
		};
		scene["GetRoots"] = [this](sol::this_state state)
		{
			sol::table roots = sol::state_view(state).create_table();
			for (const UUID root : GetScene().GetRootEntities())
				roots.add(ScriptEntity{.Id = root});
			return roots;
		};
		scene["Instantiate"] = [this](
								   std::string_view prefab, sol::optional<ScriptEntity> parent, sol::this_state state)
		{
			if (m_Context->Assets == nullptr)
				Raise(state, "There are no assets to instantiate prefabs from");
			auto loaded = m_Context->Assets->LoadPrefab(prefab);
			if (!loaded)
				Raise(state, loaded.error().GetMessageText());
			const Entity under = parent ? Entity(Resolve(state, *parent), &GetScene()) : Entity();
			auto instance = PrefabSerializer::Instantiate(GetScene(), loaded->Entities, loaded->Prefab, under);
			if (!instance)
				Raise(state, instance.error().GetMessageText());
			return ScriptEntity{.Id = instance->GetId()};
		};
		scene["Destroy"] = [this](const ScriptEntity& entity, sol::this_state state)
		{ GetScene().DestroyEntity(Entity(Resolve(state, entity), &GetScene())); };
	}

	void ScriptApi::BindInput()
	{
		sol::table input = m_Runtime->GetLua().create_named_table("Input");
		const auto key = [](std::string_view name, sol::this_state state)
		{
			const std::optional<Key> found = KeyFromName(name);
			if (!found)
				Raise(state, fmt::format("There's no key called '{}'", name));
			return *found;
		};
		const auto mouseButton = [](std::string_view name, sol::this_state state)
		{
			const std::optional<MouseButton> found = MouseButtonFromName(name);
			if (!found)
				Raise(state, fmt::format("There's no mouse button called '{}'", name));
			return *found;
		};
		const auto gamepadButton = [](std::string_view name, sol::this_state state)
		{
			const std::optional<GamepadButton> found = GamepadButtonFromName(name);
			if (!found)
				Raise(state, fmt::format("There's no gamepad button called '{}'", name));
			return *found;
		};
		input["IsKeyDown"] = [this, key](std::string_view name, sol::optional<uint32_t> player, sol::this_state state)
		{ return GetInput(player).IsKeyDown(key(name, state)); };
		input["WasKeyPressed"] = [this, key](
									 std::string_view name, sol::optional<uint32_t> player, sol::this_state state)
		{ return GetInput(player).WasKeyPressed(key(name, state)); };
		input["WasKeyReleased"] = [this, key](
									  std::string_view name, sol::optional<uint32_t> player, sol::this_state state)
		{ return GetInput(player).WasKeyReleased(key(name, state)); };
		input["IsMouseButtonDown"] = [this, mouseButton](
										 std::string_view name, sol::optional<uint32_t> player, sol::this_state state)
		{ return GetInput(player).IsMouseButtonDown(mouseButton(name, state)); };
		input["WasMouseButtonPressed"] =
			[this, mouseButton](std::string_view name, sol::optional<uint32_t> player, sol::this_state state)
		{ return GetInput(player).WasMouseButtonPressed(mouseButton(name, state)); };
		input["WasMouseButtonReleased"] =
			[this, mouseButton](std::string_view name, sol::optional<uint32_t> player, sol::this_state state)
		{ return GetInput(player).WasMouseButtonReleased(mouseButton(name, state)); };
		input["GetMousePosition"] = [this](sol::optional<uint32_t> player) { return GetInput(player).MousePosition; };
		input["GetMouseDelta"] = [this](sol::optional<uint32_t> player) { return GetInput(player).MouseDelta; };
		input["GetScrollDelta"] = [this](sol::optional<uint32_t> player) { return GetInput(player).ScrollDelta; };
		input["IsGamepadConnected"] = [this](sol::optional<uint32_t> player)
		{ return GetInput(player).GamepadConnected; };
		input["IsGamepadButtonDown"] = [this, gamepadButton](
										   std::string_view name, sol::optional<uint32_t> player, sol::this_state state)
		{ return GetInput(player).IsGamepadButtonDown(gamepadButton(name, state)); };
		input["WasGamepadButtonPressed"] =
			[this, gamepadButton](std::string_view name, sol::optional<uint32_t> player, sol::this_state state)
		{ return GetInput(player).WasGamepadButtonPressed(gamepadButton(name, state)); };
		input["WasGamepadButtonReleased"] =
			[this, gamepadButton](std::string_view name, sol::optional<uint32_t> player, sol::this_state state)
		{ return GetInput(player).WasGamepadButtonReleased(gamepadButton(name, state)); };
		input["GetGamepadAxis"] = [this](std::string_view name, sol::optional<uint32_t> player, sol::this_state state)
		{
			const std::optional<GamepadAxis> axis = GamepadAxisFromName(name);
			if (!axis)
				Raise(state, fmt::format("There's no gamepad axis called '{}'", name));
			return GetInput(player).GetGamepadAxis(*axis);
		};
	}

	void ScriptApi::BindPhysics()
	{
		sol::table physics = m_Runtime->GetLua().create_named_table("Physics");
		// A ray and its options from a table: MaxDistance, IncludeTriggers and Ignore (an entity)
		const auto readRay = [](const glm::vec3& origin, const glm::vec3& direction,
								 const sol::optional<sol::table>& table, sol::this_state state)
		{
			const float length = glm::length(direction);
			if (!IsFinite(origin) || !std::isfinite(length) || length == 0.0f)
				Raise(state, "A ray needs a finite origin, and a direction that isn't zero");
			RaycastOptions options;
			if (!table)
				return options;
			CheckOptionNames(state, *table, RaycastOptionNames, "Raycast");
			const sol::object maxDistance = (*table)["MaxDistance"];
			if (maxDistance.get_type() != sol::type::lua_nil)
			{
				const float distance = maxDistance.get_type() == sol::type::number ? maxDistance.as<float>() : 0.0f;
				if (!std::isfinite(distance) || distance <= 0.0f)
					Raise(state, "Raycast options' MaxDistance is a distance over 0");
				options.MaxDistance = distance;
			}
			const sol::object includeTriggers = (*table)["IncludeTriggers"];
			if (includeTriggers.get_type() == sol::type::boolean)
				options.IncludeTriggers = includeTriggers.as<bool>();
			else if (includeTriggers.get_type() != sol::type::lua_nil)
				Raise(state, "Raycast options' IncludeTriggers is a boolean");
			const sol::object ignore = (*table)["Ignore"];
			if (ignore.is<ScriptEntity>())
				options.Ignore = ignore.as<ScriptEntity>().Id;
			else if (ignore.get_type() != sol::type::lua_nil)
				Raise(state, "Raycast options' Ignore is an Entity");
			return options;
		};
		const auto describeHit = [](const RaycastHit& hit, sol::this_state state)
		{
			sol::table table = sol::state_view(state).create_table();
			table["Entity"] = ScriptEntity{.Id = hit.Entity};
			table["Point"] = hit.Point;
			table["Normal"] = hit.Normal;
			table["Distance"] = hit.Distance;
			return table;
		};
		physics["Raycast"] = [this, readRay, describeHit](const glm::vec3& origin, const glm::vec3& direction,
								 const sol::optional<sol::table>& options, sol::this_state state)
		{
			const RaycastOptions read = readRay(origin, direction, options, state);
			const std::optional<RaycastHit> hit = RequirePhysics(state).Raycast(origin, direction, read);
			return hit ? sol::make_object(state, describeHit(*hit, state)) : sol::make_object(state, sol::lua_nil);
		};
		physics["RaycastAll"] = [this, readRay, describeHit](const glm::vec3& origin, const glm::vec3& direction,
									const sol::optional<sol::table>& options, sol::this_state state)
		{
			const RaycastOptions read = readRay(origin, direction, options, state);
			sol::table hits = sol::state_view(state).create_table();
			for (const RaycastHit& hit : RequirePhysics(state).RaycastAll(origin, direction, read))
				hits.add(describeHit(hit, state));
			return hits;
		};
		physics["AddImpulse"] = [this](const ScriptEntity& entity, const glm::vec3& impulse, sol::this_state state)
		{
			auto* body = GetRegistry().try_get<RigidBodyComponent>(Resolve(state, entity));
			if (body == nullptr || body->Type != static_cast<uint32_t>(BodyType::Dynamic))
				Raise(state, fmt::format("{} isn't a dynamic body", DescribeEntity(entity)));
			if (!std::isfinite(impulse.x) || !std::isfinite(impulse.y) || !std::isfinite(impulse.z))
				Raise(state, "An impulse must be finite");
			GetRegistry().patch<RigidBodyComponent>(Resolve(state, entity),
				[&impulse](RigidBodyComponent& component) { component.LinearVelocity += impulse / component.Mass; });
		};
		physics["IsSleeping"] = [this](const ScriptEntity& entity, sol::this_state state)
		{
			Resolve(state, entity);
			return RequirePhysics(state).IsSleeping(entity.Id);
		};
	}

	std::pair<AnimatorComponent*, const Model*> ScriptApi::RequireAnimator(
		lua_State* state, const ScriptEntity& entity) const
	{
		Resolve(state, entity);
		const UUID animatorId = FindAnimator(GetScene(), entity.Id);
		const entt::entity animator = GetScene().FindHandle(animatorId);
		if (animator == entt::null)
			Raise(state, fmt::format("{} has no Animator", DescribeEntity(entity)));
		const auto* instance = GetRegistry().try_get<ModelInstanceComponent>(animator);
		const Model* model = instance != nullptr && m_Context->Models ? m_Context->Models(instance->Model) : nullptr;
		return {&GetRegistry().get<AnimatorComponent>(animator), model};
	}

	uint32_t ScriptApi::ResolveClip(lua_State* state, const Model* model, const sol::object& clip) const
	{
		if (clip.get_type() == sol::type::string)
		{
			if (model == nullptr)
				Raise(state, "Clips can only be named once their model is loaded");
			const std::string name = clip.as<std::string>();
			for (uint32_t index = 0; index < model->Animations.size(); ++index)
				if (model->Animations[index].Name == name)
					return index;
			Raise(state, fmt::format("The model has no clip called '{}'", name));
		}
		auto index = FromLua(clip, FieldType::UInt);
		if (!index)
			Raise(state, fmt::format("A clip is its index, from 0, or its name: {}", index.error()));
		const uint32_t value = std::get<uint32_t>(*index);
		if (model != nullptr && value >= model->Animations.size())
			Raise(state, fmt::format("The model has {} clips, so it has no clip {}", model->Animations.size(), value));
		return value;
	}

	void ScriptApi::BindAnimation()
	{
		sol::table animation = m_Runtime->GetLua().create_named_table("Animation");
		animation["Play"] = [this](const ScriptEntity& entity, const sol::object& clip,
								const sol::optional<sol::table>& options, sol::this_state state)
		{
			auto [animator, model] = RequireAnimator(state, entity);
			const uint32_t index = ResolveClip(state, model, clip);
			// Every option is checked before anything changes
			std::optional<bool> loop;
			std::optional<float> speed;
			if (options)
			{
				CheckOptionNames(state, *options, AnimationOptionNames, "Animation");
				const sol::object loopOption = (*options)["Loop"];
				if (loopOption.get_type() == sol::type::boolean)
					loop = loopOption.as<bool>();
				else if (loopOption.get_type() != sol::type::lua_nil)
					Raise(state, "Animation options' Loop is a boolean");
				const sol::object speedOption = (*options)["Speed"];
				if (speedOption.get_type() == sol::type::number)
					speed = CheckAnimationSpeed(state, speedOption.as<float>());
				else if (speedOption.get_type() != sol::type::lua_nil)
					Raise(state, "Animation options' Speed is a number");
			}
			Play(*animator, index);
			animator->Loop = loop.value_or(animator->Loop);
			animator->Speed = speed.value_or(animator->Speed);
		};
		animation["CrossFade"] =
			[this](const ScriptEntity& entity, const sol::object& clip, float duration, sol::this_state state)
		{
			if (!std::isfinite(duration) || duration < 0.0f)
				Raise(state, "A crossfade lasts 0 seconds or more");
			auto [animator, model] = RequireAnimator(state, entity);
			CrossFade(*animator, ResolveClip(state, model, clip), duration);
		};
		animation["Stop"] = [this](const ScriptEntity& entity, sol::this_state state)
		{
			AnimatorComponent& animator = *RequireAnimator(state, entity).first;
			animator.Playing = false;
			animator.Time = 0.0f;
		};
		animation["Pause"] = [this](const ScriptEntity& entity, sol::this_state state)
		{ RequireAnimator(state, entity).first->Playing = false; };
		animation["Resume"] = [this](const ScriptEntity& entity, sol::this_state state)
		{ RequireAnimator(state, entity).first->Playing = true; };
		animation["IsPlaying"] = [this](const ScriptEntity& entity, sol::this_state state)
		{ return RequireAnimator(state, entity).first->Playing; };
		animation["SetSpeed"] = [this](const ScriptEntity& entity, float speed, sol::this_state state)
		{ RequireAnimator(state, entity).first->Speed = CheckAnimationSpeed(state, speed); };
		animation["GetClip"] = [this](const ScriptEntity& entity, sol::this_state state)
		{ return RequireAnimator(state, entity).first->Clip; };
		animation["GetTime"] = [this](const ScriptEntity& entity, sol::this_state state)
		{ return RequireAnimator(state, entity).first->Time; };
		animation["GetClipCount"] = [this](const ScriptEntity& entity, sol::this_state state)
		{
			const Model* model = RequireAnimator(state, entity).second;
			if (model == nullptr)
				Raise(state, "The animator's model isn't loaded");
			return static_cast<uint32_t>(model->Animations.size());
		};
		animation["GetClipName"] = [this](
									   const ScriptEntity& entity, sol::optional<uint32_t> clip, sol::this_state state)
		{
			auto [animator, model] = RequireAnimator(state, entity);
			const uint32_t index = clip.value_or(animator->Clip);
			if (model == nullptr || index >= model->Animations.size())
				Raise(state, fmt::format("The animator's model has no clip {}", index));
			return model->Animations[index].Name;
		};
	}

	void SetScriptTime(ScriptRuntime& runtime, uint64_t tick, float deltaTime)
	{
		sol::table time = runtime.GetLua()["Time"];
		time["Delta"] = deltaTime;
		time["Tick"] = tick;
		time["Elapsed"] = static_cast<double>(tick) * deltaTime;
	}

}
