#include "Lodestone/Serialization/Json.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

namespace Lodestone::Json {

	namespace {

		// The deepest nesting of arrays and objects in a document, found without parsing it, so a hostile document
		// can't exhaust the stack in the parser or in code that walks the result
		uint32_t MeasureDepth(std::string_view text, uint32_t limit)
		{
			uint32_t depth = 0;
			uint32_t deepest = 0;
			bool inString = false;
			bool escaped = false;
			for (const char character : text)
			{
				if (inString)
				{
					if (escaped)
						escaped = false;
					else if (character == '\\')
						escaped = true;
					else if (character == '"')
						inString = false;
				}
				else if (character == '"')
				{
					inString = true;
				}
				else if (character == '[' || character == '{')
				{
					deepest = std::max(deepest, ++depth);
					if (deepest > limit)
						return deepest;
				}
				else if ((character == ']' || character == '}') && depth > 0)
				{
					--depth;
				}
			}
			return deepest;
		}

		Error MakeTypeError(std::string_view context, std::string_view expected, const Json::Value& actual)
		{
			return Error(
				ErrorCode::ParseError, fmt::format("{}: expected {}, not {}", context, expected, actual.type_name()));
		}

		// The double that prints in the fewest digits and still converts back to the float
		double ToShortestDouble(float value)
		{
			// Zero keeps its sign, which the shortest text ("-0") would lose as an integer
			if (value == 0.0f)
				return static_cast<double>(value);
			std::array<char, 64> digits{};
			const auto [end, error] = std::to_chars(digits.data(), digits.data() + digits.size(), value);
			if (error == std::errc())
			{
				// nlohmann/json parses numbers the same way when the document is read back
				const Json::Value parsed = Json::Value::parse(digits.data(), end, nullptr, /*allow_exceptions=*/false);
				if (parsed.is_number())
				{
					const auto shortest = parsed.get<double>();
					if (static_cast<float>(shortest) == value)
						return shortest;
				}
			}
			return static_cast<double>(value);
		}

		std::expected<float, Error> ToFloat(const Json::Value& json, std::string_view context)
		{
			if (!json.is_number())
				return std::unexpected(MakeTypeError(context, "a number", json));
			const auto value = json.get<double>();
			if (!std::isfinite(value) || std::abs(value) > static_cast<double>(std::numeric_limits<float>::max()))
				return std::unexpected(
					Error(ErrorCode::ParseError, fmt::format("{}: {} is out of range for a float", context, value)));
			return static_cast<float>(value);
		}

		template <size_t Count>
		std::expected<std::array<float, Count>, Error> ToFloats(const Json::Value& json, std::string_view context)
		{
			if (!json.is_array() || json.size() != Count)
				return std::unexpected(MakeTypeError(context, fmt::format("an array of {} numbers", Count), json));
			std::array<float, Count> values{};
			for (size_t index = 0; index < Count; ++index)
			{
				const auto value = ToFloat(json[index], context);
				if (!value)
					return std::unexpected(value.error());
				values[index] = *value;
			}
			return values;
		}

		std::expected<int64_t, Error> ToInteger(
			const Json::Value& json, int64_t min, int64_t max, std::string_view context)
		{
			if (!json.is_number_integer())
				return std::unexpected(MakeTypeError(context, "a whole number", json));
			if (json.is_number_unsigned())
			{
				const auto value = json.get<uint64_t>();
				if (std::cmp_greater(value, max))
					return std::unexpected(
						Error(ErrorCode::ParseError, fmt::format("{}: {} is larger than {}", context, value, max)));
				return static_cast<int64_t>(value);
			}
			const auto value = json.get<int64_t>();
			if (value < min || value > max)
				return std::unexpected(
					Error(ErrorCode::ParseError, fmt::format("{}: {} is outside {} to {}", context, value, min, max)));
			return value;
		}

	}

	std::expected<Json::Value, Error> Parse(std::string_view text, const ParseLimits& limits)
	{
		if (text.size() > limits.MaxSize)
			return std::unexpected(Error(ErrorCode::ParseError,
				fmt::format("The document is {} bytes, more than the limit of {}", text.size(), limits.MaxSize)));
		if (MeasureDepth(text, limits.MaxDepth) > limits.MaxDepth)
			return std::unexpected(Error(ErrorCode::ParseError,
				fmt::format("The document nests arrays and objects deeper than {} levels", limits.MaxDepth)));

		try
		{
			return Json::Value::parse(text.begin(), text.end());
		}
		catch (const Json::Value::exception& exception)
		{
			return std::unexpected(Error(ErrorCode::ParseError, exception.what()));
		}
	}

	std::string Write(const Json::Value& document)
	{
		return document.dump(2, ' ', /*ensure_ascii=*/false, Json::Value::error_handler_t::replace) + "\n";
	}

	std::expected<const Json::Value*, Error> GetMember(
		const Json::Value& object, std::string_view key, std::string_view context)
	{
		if (!object.is_object())
			return std::unexpected(MakeTypeError(context, "an object", object));
		const auto member = object.find(std::string(key));
		if (member == object.end())
			return std::unexpected(Error(ErrorCode::ParseError, fmt::format("{}: '{}' is missing", context, key)));
		return &*member;
	}

	std::expected<std::string, Error> GetString(
		const Json::Value& object, std::string_view key, std::string_view context)
	{
		const auto member = GetMember(object, key, context);
		if (!member)
			return std::unexpected(member.error());
		if (!(*member)->is_string())
			return std::unexpected(MakeTypeError(fmt::format("{}.{}", context, key), "a string", **member));
		return (*member)->get<std::string>();
	}

	std::expected<uint32_t, Error> GetUInt(const Json::Value& object, std::string_view key, std::string_view context)
	{
		const auto member = GetMember(object, key, context);
		if (!member)
			return std::unexpected(member.error());
		const auto value =
			ToInteger(**member, 0, std::numeric_limits<uint32_t>::max(), fmt::format("{}.{}", context, key));
		if (!value)
			return std::unexpected(value.error());
		return static_cast<uint32_t>(*value);
	}

	std::expected<UUID, Error> GetUUID(const Json::Value& object, std::string_view key, std::string_view context)
	{
		const auto member = GetMember(object, key, context);
		if (!member)
			return std::unexpected(member.error());
		const auto value = ToFieldValue(**member, FieldType::UUID, fmt::format("{}.{}", context, key));
		if (!value)
			return std::unexpected(value.error());
		return std::get<UUID>(*value);
	}

	std::expected<void, Error> CheckMembers(
		const Json::Value& object, std::span<const std::string_view> allowed, std::string_view context)
	{
		if (!object.is_object())
			return std::unexpected(MakeTypeError(context, "an object", object));
		for (const auto& [key, value] : object.items())
		{
			if (std::ranges::find(allowed, key) == allowed.end())
				return std::unexpected(
					Error(ErrorCode::ParseError, fmt::format("{}: unexpected member '{}'", context, key)));
		}
		return {};
	}

	Json::Value FromFieldValue(const FieldValue& value)
	{
		return std::visit(
			[](const auto& alternative) -> Json::Value
			{
				using T = std::decay_t<decltype(alternative)>;
				if constexpr (std::is_same_v<T, float>)
					return ToShortestDouble(alternative);
				else if constexpr (std::is_same_v<T, glm::vec2>)
					return Json::Value::array({ToShortestDouble(alternative.x), ToShortestDouble(alternative.y)});
				else if constexpr (std::is_same_v<T, glm::vec3>)
					return Json::Value::array({ToShortestDouble(alternative.x), ToShortestDouble(alternative.y),
						ToShortestDouble(alternative.z)});
				else if constexpr (std::is_same_v<T, glm::vec4> || std::is_same_v<T, glm::quat>)
					return Json::Value::array({ToShortestDouble(alternative.x), ToShortestDouble(alternative.y),
						ToShortestDouble(alternative.z), ToShortestDouble(alternative.w)});
				else if constexpr (std::is_same_v<T, UUID>)
					return alternative.ToString();
				else
					return alternative;
			},
			value);
	}

	std::expected<FieldValue, Error> ToFieldValue(const Json::Value& json, FieldType type, std::string_view context)
	{
		switch (type)
		{
			case FieldType::Bool:
				if (!json.is_boolean())
					return std::unexpected(MakeTypeError(context, "true or false", json));
				return json.get<bool>();
			case FieldType::Int: {
				const auto value =
					ToInteger(json, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(), context);
				if (!value)
					return std::unexpected(value.error());
				return static_cast<int32_t>(*value);
			}
			case FieldType::UInt: {
				const auto value = ToInteger(json, 0, std::numeric_limits<uint32_t>::max(), context);
				if (!value)
					return std::unexpected(value.error());
				return static_cast<uint32_t>(*value);
			}
			case FieldType::Float: {
				const auto value = ToFloat(json, context);
				if (!value)
					return std::unexpected(value.error());
				return *value;
			}
			case FieldType::Vec2: {
				const auto values = ToFloats<2>(json, context);
				if (!values)
					return std::unexpected(values.error());
				return glm::vec2((*values)[0], (*values)[1]);
			}
			case FieldType::Vec3: {
				const auto values = ToFloats<3>(json, context);
				if (!values)
					return std::unexpected(values.error());
				return glm::vec3((*values)[0], (*values)[1], (*values)[2]);
			}
			case FieldType::Vec4: {
				const auto values = ToFloats<4>(json, context);
				if (!values)
					return std::unexpected(values.error());
				return glm::vec4((*values)[0], (*values)[1], (*values)[2], (*values)[3]);
			}
			case FieldType::Quat: {
				const auto values = ToFloats<4>(json, context);
				if (!values)
					return std::unexpected(values.error());
				return glm::quat::wxyz((*values)[3], (*values)[0], (*values)[1], (*values)[2]);
			}
			case FieldType::String:
				if (!json.is_string())
					return std::unexpected(MakeTypeError(context, "a string", json));
				return json.get<std::string>();
			case FieldType::UUID: {
				if (!json.is_string())
					return std::unexpected(MakeTypeError(context, "a UUID string", json));
				const std::optional<UUID> uuid = UUID::Parse(json.get<std::string>());
				if (!uuid)
					return std::unexpected(Error(
						ErrorCode::ParseError, fmt::format("{}: '{}' isn't a UUID", context, json.get<std::string>())));
				return *uuid;
			}
		}
		return std::unexpected(Error(ErrorCode::InvalidArgument, fmt::format("{}: unknown field type", context)));
	}

}
