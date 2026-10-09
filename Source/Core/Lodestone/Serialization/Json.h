#pragma once

#include "Lodestone/Core/Error.h"
#include "Lodestone/Reflection/FieldValue.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

// JSON for the files the engine reads and writes. Files may be corrupt or hostile, so parsing is bounded, reading a
// value always checks its type, and nothing here throws: nlohmann/json's exceptions are caught and returned as errors.

namespace Lodestone::Json {

	// A JSON document. Objects keep their members in the order they're added, so files read in a natural order -
	// format and version first - and the order is the same every time
	using Value = nlohmann::ordered_json;

	struct ParseLimits
	{
		// The largest document, in bytes
		size_t MaxSize = 64ull * 1024 * 1024;
		// The deepest nesting of arrays and objects. Engine files nest a few levels; the limit stops documents that
		// would exhaust the stack in code that walks them
		uint32_t MaxDepth = 64;
	};

	[[nodiscard]] std::expected<Json::Value, Error> Parse(std::string_view text, const ParseLimits& limits = {});

	// The document as text, indented for clean diffs in version control. Invalid UTF-8 in strings is replaced rather
	// than failing
	std::string Write(const Json::Value& document);

	// The member of an object, or an error naming what's missing. context describes the object in error messages
	[[nodiscard]] std::expected<const Json::Value*, Error> GetMember(
		const Json::Value& object, std::string_view key, std::string_view context);
	[[nodiscard]] std::expected<std::string, Error> GetString(
		const Json::Value& object, std::string_view key, std::string_view context);
	[[nodiscard]] std::expected<uint32_t, Error> GetUInt(
		const Json::Value& object, std::string_view key, std::string_view context);
	[[nodiscard]] std::expected<UUID, Error> GetUUID(
		const Json::Value& object, std::string_view key, std::string_view context);

	// Checks that an object has no members besides the allowed ones, so a misspelled key isn't silently ignored
	[[nodiscard]] std::expected<void, Error> CheckMembers(
		const Json::Value& object, std::span<const std::string_view> allowed, std::string_view context);

	// Field values: numbers, booleans and strings as themselves, vectors as arrays, quaternions as [x, y, z, w] and
	// UUIDs as strings. Floats are written in the shortest form that reads back to the same float
	Json::Value FromFieldValue(const FieldValue& value);
	[[nodiscard]] std::expected<FieldValue, Error> ToFieldValue(
		const Json::Value& json, FieldType type, std::string_view context);

}
