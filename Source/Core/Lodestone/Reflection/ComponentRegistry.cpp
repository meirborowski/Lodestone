#include "Lodestone/Reflection/ComponentRegistry.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cmath>

namespace Lodestone {

	namespace {

		// Component and field names appear in files, MCP and scripts, so they're plain identifiers
		bool IsIdentifier(std::string_view name)
		{
			const auto isLetter = [](char character)
			{ return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z'); };
			const auto isDigit = [](char character) { return character >= '0' && character <= '9'; };
			return !name.empty() && isLetter(name.front()) &&
				std::ranges::all_of(name, [&](char character) { return isLetter(character) || isDigit(character); });
		}

		bool HasLimits(FieldType type)
		{
			switch (type)
			{
				case FieldType::Int:
				case FieldType::UInt:
				case FieldType::Float:
				case FieldType::Vec2:
				case FieldType::Vec3:
				case FieldType::Vec4:
					return true;
				case FieldType::Bool:
				case FieldType::Quat:
				case FieldType::String:
				case FieldType::UUID:
					return false;
			}
			return false;
		}

		// The numbers a value's limits apply to: the value itself, or each element of a vector
		std::vector<double> GetLimitedNumbers(const FieldValue& value)
		{
			return std::visit(
				[](const auto& alternative) -> std::vector<double>
				{
					using T = std::decay_t<decltype(alternative)>;
					if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t> || std::is_same_v<T, float>)
						return {static_cast<double>(alternative)};
					else if constexpr (std::is_same_v<T, glm::vec2>)
						return {alternative.x, alternative.y};
					else if constexpr (std::is_same_v<T, glm::vec3>)
						return {alternative.x, alternative.y, alternative.z};
					else if constexpr (std::is_same_v<T, glm::vec4>)
						return {alternative.x, alternative.y, alternative.z, alternative.w};
					else
						return {};
				},
				value);
		}

		// The floating-point numbers in a value, which must be finite
		std::vector<float> GetFloats(const FieldValue& value)
		{
			return std::visit(
				[](const auto& alternative) -> std::vector<float>
				{
					using T = std::decay_t<decltype(alternative)>;
					if constexpr (std::is_same_v<T, float>)
						return {alternative};
					else if constexpr (std::is_same_v<T, glm::vec2>)
						return {alternative.x, alternative.y};
					else if constexpr (std::is_same_v<T, glm::vec3>)
						return {alternative.x, alternative.y, alternative.z};
					else if constexpr (std::is_same_v<T, glm::vec4> || std::is_same_v<T, glm::quat>)
						return {alternative.x, alternative.y, alternative.z, alternative.w};
					else
						return {};
				},
				value);
		}

	}

	FieldInfo::FieldInfo(std::string name, FieldType type, const FieldOptions& options, FieldValue defaultValue,
		Getter getter, Setter setter)
		: m_Name(std::move(name)),
		  m_Type(type),
		  m_Description(options.Description),
		  m_Min(options.Min),
		  m_Max(options.Max),
		  m_Flags(options.Flags),
		  m_Default(std::move(defaultValue)),
		  m_Getter(std::move(getter)),
		  m_Setter(std::move(setter))
	{
		LS_CORE_ASSERT(IsIdentifier(m_Name), "Field name '{}' isn't an identifier", m_Name);
		LS_CORE_ASSERT(GetFieldType(m_Default) == m_Type, "Field '{}' has a default of the wrong type", m_Name);
		LS_CORE_ASSERT(
			HasLimits(m_Type) || (!m_Min && !m_Max), "{} field '{}' can't have limits", ToString(m_Type), m_Name);
		LS_CORE_ASSERT(!m_Min || !m_Max || *m_Min <= *m_Max, "Field '{}' has a minimum above its maximum", m_Name);
		LS_CORE_ASSERT(Validate(m_Default).has_value(), "Field '{}' has a default outside its limits", m_Name);
	}

	std::expected<void, Error> FieldInfo::Validate(const FieldValue& value) const
	{
		if (GetFieldType(value) != m_Type)
			return std::unexpected(Error(ErrorCode::InvalidArgument,
				fmt::format("Field '{}' is a {}, not a {}", m_Name, ToString(m_Type), ToString(GetFieldType(value)))));

		const std::vector<float> floats = GetFloats(value);
		if (!std::ranges::all_of(floats, [](float number) { return std::isfinite(number); }))
			return std::unexpected(Error(
				ErrorCode::InvalidArgument, fmt::format("Field '{}' must be finite (no infinity or NaN)", m_Name)));
		if (m_Type == FieldType::Quat && std::ranges::all_of(floats, [](float number) { return number == 0.0f; }))
			return std::unexpected(Error(ErrorCode::InvalidArgument,
				fmt::format("Field '{}' is a rotation, so it can't be the zero quaternion", m_Name)));

		for (const double number : GetLimitedNumbers(value))
		{
			if ((m_Min && number < *m_Min) || (m_Max && number > *m_Max))
				return std::unexpected(Error(ErrorCode::InvalidArgument,
					fmt::format("Field '{}' must be between {} and {}, not {}", m_Name,
						m_Min ? fmt::format("{}", *m_Min) : "-infinity", m_Max ? fmt::format("{}", *m_Max) : "infinity",
						number)));
		}
		return {};
	}

	std::expected<void, Error> FieldInfo::Set(void* component, const FieldValue& value) const
	{
		if (auto valid = Validate(value); !valid)
			return valid;
		m_Setter(component, value);
		return {};
	}

	ComponentType::ComponentType(std::string name, std::string description, ComponentFlags flags, entt::id_type typeId,
		Detail::ComponentOperations operations)
		: m_Name(std::move(name)),
		  m_Description(std::move(description)),
		  m_Flags(flags),
		  m_TypeId(typeId),
		  m_Operations(operations)
	{
		LS_CORE_ASSERT(IsIdentifier(m_Name), "Component name '{}' isn't an identifier", m_Name);
	}

	const FieldInfo* ComponentType::FindField(std::string_view name) const
	{
		const auto field = std::ranges::find(m_Fields, name, &FieldInfo::GetName);
		return field != m_Fields.end() ? &*field : nullptr;
	}

	void* ComponentType::Add(entt::registry& registry, entt::entity entity) const
	{
		LS_CORE_ASSERT(!Has(registry, entity), "The entity already has a {} component", m_Name);
		return m_Operations.Add(registry, entity);
	}

	std::expected<void, Error> ComponentType::SetField(
		entt::registry& registry, entt::entity entity, const FieldInfo& field, const FieldValue& value) const
	{
		void* component = TryGet(registry, entity);
		if (component == nullptr)
			return std::unexpected(
				Error(ErrorCode::InvalidState, fmt::format("The entity has no {} component", m_Name)));
		if (auto set = field.Set(component, value); !set)
			return set;
		NotifyChanged(registry, entity);
		return {};
	}

	void ComponentType::AddField(FieldInfo field)
	{
		LS_CORE_ASSERT(
			FindField(field.GetName()) == nullptr, "Component {} already has a field '{}'", m_Name, field.GetName());
		m_Fields.push_back(std::move(field));
	}

	const ComponentType* ComponentRegistry::Find(std::string_view name) const
	{
		const auto type = m_ByName.find(name);
		return type != m_ByName.end() ? type->second : nullptr;
	}

	const ComponentType* ComponentRegistry::FindByTypeId(entt::id_type typeId) const
	{
		const auto type = m_ByTypeId.find(typeId);
		return type != m_ByTypeId.end() ? type->second : nullptr;
	}

	ComponentType& ComponentRegistry::AddType(Scope<ComponentType> type)
	{
		LS_CORE_ASSERT(
			Find(type->GetName()) == nullptr, "A component named '{}' is already registered", type->GetName());
		LS_CORE_ASSERT(FindByTypeId(type->GetTypeId()) == nullptr,
			"Component {} is already registered under another name", type->GetName());

		ComponentType& added = *type;
		m_Types.push_back(std::move(type));
		m_Order.push_back(&added);
		m_ByName.emplace(added.GetName(), &added);
		m_ByTypeId.emplace(added.GetTypeId(), &added);
		return added;
	}

}
