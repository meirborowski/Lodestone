#include "Lodestone/Reflection/FieldValue.h"

#include <type_traits>

namespace Lodestone {

	// FieldValue's alternatives must stay in FieldType's order, which GetFieldType() relies on
	static_assert(std::is_same_v<std::variant_alternative_t<static_cast<size_t>(FieldType::Bool), FieldValue>, bool>);
	static_assert(std::is_same_v<std::variant_alternative_t<static_cast<size_t>(FieldType::Int), FieldValue>, int32_t>);
	static_assert(
		std::is_same_v<std::variant_alternative_t<static_cast<size_t>(FieldType::UInt), FieldValue>, uint32_t>);
	static_assert(std::is_same_v<std::variant_alternative_t<static_cast<size_t>(FieldType::Float), FieldValue>, float>);
	static_assert(
		std::is_same_v<std::variant_alternative_t<static_cast<size_t>(FieldType::Vec2), FieldValue>, glm::vec2>);
	static_assert(
		std::is_same_v<std::variant_alternative_t<static_cast<size_t>(FieldType::Vec3), FieldValue>, glm::vec3>);
	static_assert(
		std::is_same_v<std::variant_alternative_t<static_cast<size_t>(FieldType::Vec4), FieldValue>, glm::vec4>);
	static_assert(
		std::is_same_v<std::variant_alternative_t<static_cast<size_t>(FieldType::Quat), FieldValue>, glm::quat>);
	static_assert(
		std::is_same_v<std::variant_alternative_t<static_cast<size_t>(FieldType::String), FieldValue>, std::string>);
	static_assert(std::is_same_v<std::variant_alternative_t<static_cast<size_t>(FieldType::UUID), FieldValue>, UUID>);
	static_assert(std::variant_size_v<FieldValue> == static_cast<size_t>(FieldType::UUID) + 1);

	std::string_view ToString(FieldType type)
	{
		switch (type)
		{
			case FieldType::Bool:
				return "Bool";
			case FieldType::Int:
				return "Int";
			case FieldType::UInt:
				return "UInt";
			case FieldType::Float:
				return "Float";
			case FieldType::Vec2:
				return "Vec2";
			case FieldType::Vec3:
				return "Vec3";
			case FieldType::Vec4:
				return "Vec4";
			case FieldType::Quat:
				return "Quat";
			case FieldType::String:
				return "String";
			case FieldType::UUID:
				return "UUID";
		}
		return "Unknown";
	}

}
