#pragma once

#include "Lodestone/Core/UUID.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace Lodestone {

	// The types a reflected component field can have. Serialization, the inspector, MCP, script bindings and
	// replication each handle exactly these
	enum class FieldType
	{
		Bool,
		Int,
		UInt,
		Float,
		Vec2,
		Vec3,
		Vec4,
		Quat,
		String,
		UUID,
	};

	std::string_view ToString(FieldType type);

	// The value of a reflected field. The alternatives are in the order of FieldType
	using FieldValue =
		std::variant<bool, int32_t, uint32_t, float, glm::vec2, glm::vec3, glm::vec4, glm::quat, std::string, UUID>;

	inline FieldType GetFieldType(const FieldValue& value)
	{
		return static_cast<FieldType>(value.index());
	}

	// The field type that stores a C++ type, for the types that have one
	template <typename T>
	struct FieldTypeOf;

	template <>
	struct FieldTypeOf<bool>
	{
		static constexpr FieldType Value = FieldType::Bool;
	};

	template <>
	struct FieldTypeOf<int32_t>
	{
		static constexpr FieldType Value = FieldType::Int;
	};

	template <>
	struct FieldTypeOf<uint32_t>
	{
		static constexpr FieldType Value = FieldType::UInt;
	};

	template <>
	struct FieldTypeOf<float>
	{
		static constexpr FieldType Value = FieldType::Float;
	};

	template <>
	struct FieldTypeOf<glm::vec2>
	{
		static constexpr FieldType Value = FieldType::Vec2;
	};

	template <>
	struct FieldTypeOf<glm::vec3>
	{
		static constexpr FieldType Value = FieldType::Vec3;
	};

	template <>
	struct FieldTypeOf<glm::vec4>
	{
		static constexpr FieldType Value = FieldType::Vec4;
	};

	template <>
	struct FieldTypeOf<glm::quat>
	{
		static constexpr FieldType Value = FieldType::Quat;
	};

	template <>
	struct FieldTypeOf<std::string>
	{
		static constexpr FieldType Value = FieldType::String;
	};

	template <>
	struct FieldTypeOf<UUID>
	{
		static constexpr FieldType Value = FieldType::UUID;
	};

	template <typename T>
	concept ReflectableFieldType = requires { FieldTypeOf<T>::Value; };

}
