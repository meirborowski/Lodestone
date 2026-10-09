#include "Lodestone/Reflection/ComponentRegistry.h"

#include "Lodestone/Scene/Components.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>

namespace Lodestone {

	namespace {

		struct TestComponent
		{
			bool Enabled = true;
			int32_t Count = 3;
			uint32_t Mask = 0;
			float Speed = 1.5f;
			glm::vec2 Offset{0.0f};
			glm::vec3 Color{1.0f};
			glm::vec4 Tint{1.0f};
			glm::quat Rotation = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
			std::string Label = "label";
			UUID Target;
		};

		struct TagComponent
		{
		};

		void CountUpdate(int& count, entt::registry& /*registry*/, entt::entity /*entity*/)
		{
			++count;
		}

		void RegisterTestComponent(ComponentRegistry& registry)
		{
			registry.Register<TestComponent>("Test", "A component with a field of every type")
				.Field("Enabled", &TestComponent::Enabled)
				.Field("Count", &TestComponent::Count, {.Min = -10.0, .Max = 10.0})
				.Field("Mask", &TestComponent::Mask, {.Flags = FieldFlags::Replicated})
				.Field("Speed", &TestComponent::Speed, {.Description = "Meters per second", .Min = 0.0})
				.Field("Offset", &TestComponent::Offset)
				.Field("Color", &TestComponent::Color, {.Min = 0.0, .Max = 1.0})
				.Field("Tint", &TestComponent::Tint)
				.Field("Rotation", &TestComponent::Rotation)
				.Field("Label", &TestComponent::Label, {.Flags = FieldFlags::ReadOnly})
				.Field("Target", &TestComponent::Target);
		}

	}

	TEST_CASE("Registered components are found by name and by type")
	{
		ComponentRegistry registry;
		RegisterTestComponent(registry);
		registry.Register<TagComponent>("Tag");

		const ComponentType* type = registry.Find("Test");
		REQUIRE(type != nullptr);
		CHECK(type->GetName() == "Test");
		CHECK(type->GetDescription() == "A component with a field of every type");
		CHECK(registry.Find<TestComponent>() == type);
		CHECK(registry.Find<TagComponent>() == registry.Find("Tag"));
		CHECK(registry.Find("Missing") == nullptr);
		CHECK(registry.Find<NameComponent>() == nullptr);
		REQUIRE(registry.GetTypes().size() == 2);
		CHECK(registry.GetTypes()[0] == type);
	}

	TEST_CASE("Fields are described by name, type, limits, flags and default")
	{
		ComponentRegistry registry;
		RegisterTestComponent(registry);
		const ComponentType& type = *registry.Find("Test");

		REQUIRE(type.GetFields().size() == 10);
		const FieldInfo* speed = type.FindField("Speed");
		REQUIRE(speed != nullptr);
		CHECK(speed->GetType() == FieldType::Float);
		CHECK(speed->GetDescription() == "Meters per second");
		CHECK(speed->GetMin() == 0.0);
		CHECK_FALSE(speed->GetMax().has_value());
		CHECK(std::get<float>(speed->GetDefault()) == 1.5f);

		CHECK(type.FindField("Mask")->IsReplicated());
		CHECK_FALSE(type.FindField("Mask")->IsReadOnly());
		CHECK(type.FindField("Label")->IsReadOnly());
		CHECK(type.FindField("Rotation")->GetType() == FieldType::Quat);
		CHECK(type.FindField("Target")->GetType() == FieldType::UUID);
		CHECK(type.FindField("Missing") == nullptr);
	}

	TEST_CASE("Fields are read and written through the registry")
	{
		ComponentRegistry registry;
		RegisterTestComponent(registry);
		const ComponentType& type = *registry.Find("Test");
		TestComponent component;

		REQUIRE(type.FindField("Count")->Set(&component, int32_t{-4}).has_value());
		REQUIRE(type.FindField("Color")->Set(&component, glm::vec3(0.5f, 0.25f, 1.0f)).has_value());
		REQUIRE(type.FindField("Label")->Set(&component, std::string("renamed")).has_value());

		CHECK(component.Count == -4);
		CHECK(component.Color == glm::vec3(0.5f, 0.25f, 1.0f));
		CHECK(component.Label == "renamed");
		CHECK(std::get<std::string>(type.FindField("Label")->Get(&component)) == "renamed");
	}

	TEST_CASE("Setting a field validates the value's type and limits")
	{
		ComponentRegistry registry;
		RegisterTestComponent(registry);
		const ComponentType& type = *registry.Find("Test");
		TestComponent component;

		SUBCASE("Wrong type")
		{
			const auto set = type.FindField("Count")->Set(&component, 2.0f);
			REQUIRE_FALSE(set.has_value());
			CHECK(set.error().GetCode() == ErrorCode::InvalidArgument);
		}
		SUBCASE("Below the minimum")
		{
			CHECK_FALSE(type.FindField("Count")->Set(&component, int32_t{-11}).has_value());
		}
		SUBCASE("Above the maximum, in one element of a vector")
		{
			CHECK_FALSE(type.FindField("Color")->Set(&component, glm::vec3(0.5f, 1.5f, 0.5f)).has_value());
		}
		SUBCASE("Not finite")
		{
			CHECK_FALSE(type.FindField("Speed")->Set(&component, std::numeric_limits<float>::infinity()).has_value());
			CHECK_FALSE(type.FindField("Offset")->Set(&component, glm::vec2(std::nanf(""), 0.0f)).has_value());
		}
		SUBCASE("The zero quaternion")
		{
			CHECK_FALSE(
				type.FindField("Rotation")->Set(&component, glm::quat::wxyz(0.0f, 0.0f, 0.0f, 0.0f)).has_value());
		}
		// A rejected value leaves the component unchanged
		CHECK(component.Count == 3);
		CHECK(component.Color == glm::vec3(1.0f));
		CHECK(component.Speed == 1.5f);
	}

	TEST_CASE("Component types add, find, change and remove components in a registry")
	{
		ComponentRegistry components;
		RegisterTestComponent(components);
		components.Register<TagComponent>("Tag");
		const ComponentType& type = *components.Find("Test");
		const ComponentType& tag = *components.Find("Tag");

		entt::registry registry;
		const entt::entity entity = registry.create();
		CHECK_FALSE(type.Has(registry, entity));
		CHECK(type.TryGet(registry, entity) == nullptr);

		const void* added = type.Add(registry, entity);
		CHECK(added == &registry.get<TestComponent>(entity));
		CHECK(type.Has(registry, entity));

		// Tag components with no data work like any other
		CHECK(tag.Add(registry, entity) != nullptr);
		CHECK(tag.Has(registry, entity));

		int updates = 0;
		registry.on_update<TestComponent>().connect<&CountUpdate>(updates);
		REQUIRE(type.SetField(registry, entity, *type.FindField("Speed"), 4.0f).has_value());
		CHECK(registry.get<TestComponent>(entity).Speed == 4.0f);
		CHECK(updates == 1);

		type.Remove(registry, entity);
		CHECK_FALSE(type.Has(registry, entity));
		const auto missing = type.SetField(registry, entity, *type.FindField("Speed"), 4.0f);
		REQUIRE_FALSE(missing.has_value());
		CHECK(missing.error().GetCode() == ErrorCode::InvalidState);
	}

	TEST_CASE("The core components are registered with their flags")
	{
		ComponentRegistry registry;
		RegisterCoreComponents(registry);

		for (const char* name : {"ID", "Name", "Transform", "Hierarchy"})
		{
			CAPTURE(name);
			const ComponentType* type = registry.Find(name);
			REQUIRE(type != nullptr);
			CHECK(type->IsRequired());
			CHECK_FALSE(type->IsInternal());
		}
		CHECK(registry.Find("PreviousTransform")->IsInternal());
		CHECK(registry.Find("ID")->FindField("ID")->IsReadOnly());
		CHECK(registry.Find("Hierarchy")->FindField("Parent")->IsReadOnly());
		CHECK(registry.Find("Hierarchy")->FindField("Children") == nullptr);
		CHECK(registry.Find("Transform")->FindField("Position")->IsReplicated());
		CHECK(&GetEngineComponentRegistry() == &GetEngineComponentRegistry());
		CHECK(GetEngineComponentRegistry().Find<TransformComponent>() != nullptr);
	}

	TEST_CASE("Field types have names")
	{
		CHECK(ToString(FieldType::Vec3) == "Vec3");
		CHECK(ToString(FieldType::UUID) == "UUID");
		CHECK(GetFieldType(FieldValue(glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f))) == FieldType::Quat);
	}

}
