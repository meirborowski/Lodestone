#include "Lodestone/Serialization/Json.h"

#include "Common/DescribeError.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <string>

namespace Lodestone {

	TEST_CASE("Json::Parse reads documents and reports malformed ones")
	{
		const auto document = Json::Parse(R"({"name": "value", "list": [1, 2.5, true]})");
		REQUIRE(document.has_value());
		CHECK((*document)["name"] == "value");
		CHECK((*document)["list"].size() == 3);

		for (const char* text : {"", "{", "{\"a\": }", "[1, 2,]", "nul", "{\"a\": 1} trailing"})
		{
			CAPTURE(text);
			const auto malformed = Json::Parse(text);
			REQUIRE_FALSE(malformed.has_value());
			CHECK(malformed.error().GetCode() == ErrorCode::ParseError);
		}
	}

	TEST_CASE("Json::Parse rejects documents beyond its limits")
	{
		const std::string deep = std::string(100, '[') + std::string(100, ']');
		CHECK_FALSE(Json::Parse(deep).has_value());
		CHECK(Json::Parse(deep, {.MaxDepth = 100}).has_value());
		// Brackets inside strings don't count
		CHECK(Json::Parse("[\"" + std::string(100, '[') + "\"]", {.MaxDepth = 2}).has_value());
		CHECK(Json::Parse("[\"\\\"" + std::string(100, '[') + "\"]", {.MaxDepth = 2}).has_value());

		CHECK_FALSE(Json::Parse("[1, 2, 3]", {.MaxSize = 8}).has_value());
	}

	TEST_CASE("Json::Write indents documents and ends them with a newline")
	{
		const Json::Value document = {{"a", 1}};
		CHECK(Json::Write(document) == "{\n  \"a\": 1\n}\n");
		// Invalid UTF-8 is replaced rather than failing
		const Json::Value invalid = std::string("\xFF");
		CHECK(Json::Write(invalid) == "\"\xEF\xBF\xBD\"\n");
	}

	TEST_CASE("Field values round-trip through JSON")
	{
		const std::array<FieldValue, 10> values = {
			FieldValue(true),
			FieldValue(int32_t{-2147483647 - 1}),
			FieldValue(uint32_t{4294967295u}),
			FieldValue(0.1f),
			FieldValue(glm::vec2(1.0f, -2.5f)),
			FieldValue(glm::vec3(1e-30f, 3.4e38f, -0.0f)),
			FieldValue(glm::vec4(0.25f, 0.5f, 0.75f, 1.0f)),
			FieldValue(glm::quat::wxyz(0.5f, 0.1f, 0.2f, 0.3f)),
			FieldValue(std::string("Ünïcødé \"quoted\"")),
			FieldValue(UUID(0x1234, 0x5678)),
		};
		for (const FieldValue& value : values)
		{
			CAPTURE(ToString(GetFieldType(value)));
			const Json::Value json = Json::FromFieldValue(value);
			// Through text, as in a file
			const auto parsed = Json::Parse(json.dump());
			REQUIRE(parsed.has_value());
			const auto read = Json::ToFieldValue(*parsed, GetFieldType(value), "value");
			REQUIRE_MESSAGE(read.has_value(), Testing::DescribeError(read));
			CHECK(*read == value);
		}
	}

	TEST_CASE("Floats are written in their shortest form")
	{
		CHECK(Json::FromFieldValue(0.1f).dump() == "0.1");
		CHECK(Json::FromFieldValue(glm::vec3(1.0f, 0.3f, 2.0f)).dump() == "[1.0,0.3,2.0]");
		CHECK(Json::FromFieldValue(glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f)).dump() == "[0.0,0.0,0.0,1.0]");
	}

	TEST_CASE("Reading a field value checks its type and range")
	{
		const auto expectError = [](const Json::Value& json, FieldType type)
		{
			CAPTURE(json.dump());
			CAPTURE(ToString(type));
			const auto value = Json::ToFieldValue(json, type, "test.field");
			REQUIRE_FALSE(value.has_value());
			CHECK(value.error().GetCode() == ErrorCode::ParseError);
			CHECK(value.error().GetMessageText().starts_with("test.field: "));
		};
		expectError(1, FieldType::Bool);
		expectError(1.5, FieldType::Int);
		expectError(2147483648ll, FieldType::Int);
		expectError(-1, FieldType::UInt);
		expectError(4294967296ll, FieldType::UInt);
		expectError("1", FieldType::Float);
		expectError(1e39, FieldType::Float);
		expectError(Json::Value::array({1, 2}), FieldType::Vec3);
		expectError(Json::Value::array({1, "2", 3}), FieldType::Vec3);
		expectError(Json::Value::array({0, 0, 0}), FieldType::Quat);
		expectError(5, FieldType::String);
		expectError("not-a-uuid", FieldType::UUID);
		expectError(nullptr, FieldType::Float);
	}

	TEST_CASE("Integers are accepted for float fields")
	{
		const auto value = Json::ToFieldValue(3, FieldType::Float, "value");
		REQUIRE(value.has_value());
		CHECK(std::get<float>(*value) == 3.0f);
	}

	TEST_CASE("Members are read with their types checked")
	{
		const Json::Value object = {{"name", "Box"}, {"version", 3}, {"id", "01234567-89ab-cdef-fedc-ba9876543210"}};

		const auto name = Json::GetString(object, "name", "object");
		const auto version = Json::GetUInt(object, "version", "object");
		const auto id = Json::GetUUID(object, "id", "object");
		REQUIRE(name.has_value());
		REQUIRE(version.has_value());
		REQUIRE(id.has_value());
		CHECK(*name == "Box");
		CHECK(*version == 3u);
		CHECK(*id == UUID(0x0123'4567'89ab'cdefull, 0xfedc'ba98'7654'3210ull));

		const auto missing = Json::GetString(object, "other", "object");
		REQUIRE_FALSE(missing.has_value());
		CHECK(missing.error().GetMessageText() == "object: 'other' is missing");
		CHECK_FALSE(Json::GetUInt(object, "name", "object").has_value());
		CHECK_FALSE(Json::GetString(Json::Value::array(), "name", "object").has_value());
	}

	TEST_CASE("CheckMembers rejects unexpected members")
	{
		const Json::Value object = {{"a", 1}, {"b", 2}};
		constexpr std::array<std::string_view, 3> allowed = {"a", "b", "c"};
		constexpr std::array<std::string_view, 1> fewer = {"a"};

		CHECK(Json::CheckMembers(object, allowed, "object").has_value());
		const auto unexpected = Json::CheckMembers(object, fewer, "object");
		REQUIRE_FALSE(unexpected.has_value());
		CHECK(unexpected.error().GetMessageText() == "object: unexpected member 'b'");
	}

}
