#pragma once

#include "Lodestone/Core/UUID.h"
#include "Lodestone/Editor/Commands/Command.h"
#include "Lodestone/Reflection/FieldValue.h"
#include "Lodestone/Serialization/Json.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The commands that edit entities and their components. Component data is given as JSON, in the same form as scene
// files ({"Transform": {"Position": [1, 2, 3]}}), and checked against the reflection registry: unknown components and
// fields, wrong types, values out of range and read-only fields are rejected before anything changes

namespace Lodestone {

	// Creates an entity, with components, under a parent (or as a root entity)
	class CreateEntityCommand final : public Command
	{
	public:
		// components: {"Type": {"Field": value, ...}, ...}, added or set on the new entity. The entity's UUID is
		// chosen the first time the command runs and reused when it's redone
		CreateEntityCommand(std::string name, UUID parent = {}, std::optional<size_t> index = std::nullopt,
			Json::Value components = Json::Value::object());

		std::string GetName() const override;
		[[nodiscard]] std::expected<void, Error> Execute(Scene& scene) override;
		[[nodiscard]] std::expected<void, Error> Undo(Scene& scene) override;

		// The created entity, after the command has run
		UUID GetEntityId() const { return m_Id; }

	private:
		std::string m_Name;
		UUID m_Parent;
		std::optional<size_t> m_Index;
		Json::Value m_Components;
		UUID m_Id;
	};

	// Destroys an entity and its descendants. Undo recreates them exactly, UUIDs and place in the hierarchy included
	class DestroyEntityCommand final : public Command
	{
	public:
		explicit DestroyEntityCommand(UUID id);

		std::string GetName() const override;
		[[nodiscard]] std::expected<void, Error> Execute(Scene& scene) override;
		[[nodiscard]] std::expected<void, Error> Undo(Scene& scene) override;

	private:
		UUID m_Id;
		// Known once the command has run
		std::optional<std::string> m_EntityName;
		Json::Value m_Tree;
		UUID m_Parent;
		size_t m_Index = 0;
	};

	// Copies an entity and its descendants, with new UUIDs, right after the original among its siblings
	class DuplicateEntityCommand final : public Command
	{
	public:
		explicit DuplicateEntityCommand(UUID source);

		std::string GetName() const override;
		[[nodiscard]] std::expected<void, Error> Execute(Scene& scene) override;
		[[nodiscard]] std::expected<void, Error> Undo(Scene& scene) override;

		// The copy's root, after the command has run
		UUID GetCopyId() const { return m_CopyId; }

	private:
		UUID m_Source;
		std::optional<std::string> m_SourceName;
		// The copy, saved the first time the command runs, so redoing it recreates the same UUIDs
		Json::Value m_Copy;
		UUID m_Parent;
		size_t m_Index = 0;
		UUID m_CopyId;
	};

	// Instances a prefab's entities (see PrefabSerializer) under a parent (or as a root entity)
	class InstantiatePrefabCommand final : public Command
	{
	public:
		InstantiatePrefabCommand(Json::Value prefab, UUID prefabAsset, std::string prefabName, UUID parent = {},
			std::optional<size_t> index = std::nullopt);

		std::string GetName() const override;
		[[nodiscard]] std::expected<void, Error> Execute(Scene& scene) override;
		[[nodiscard]] std::expected<void, Error> Undo(Scene& scene) override;

		// The instance's root, after the command has run
		UUID GetInstanceId() const { return m_InstanceId; }

	private:
		Json::Value m_Prefab;
		UUID m_PrefabAsset;
		std::string m_PrefabName;
		UUID m_Parent;
		std::optional<size_t> m_Index;
		// The instance, saved the first time the command runs, so redoing it recreates the same UUIDs
		Json::Value m_Instance;
		UUID m_InstanceId;
	};

	// Moves an entity under a new parent (or to the root, with the nil UUID) at a position among its siblings
	class SetParentCommand final : public Command
	{
	public:
		SetParentCommand(UUID id, UUID parent, std::optional<size_t> index = std::nullopt);

		std::string GetName() const override;
		[[nodiscard]] std::expected<void, Error> Execute(Scene& scene) override;
		[[nodiscard]] std::expected<void, Error> Undo(Scene& scene) override;

	private:
		UUID m_Id;
		UUID m_Parent;
		std::optional<size_t> m_Index;
		UUID m_OldParent;
		size_t m_OldIndex = 0;
	};

	// Adds a component, with field values; fields that aren't given keep their defaults
	class AddComponentCommand final : public Command
	{
	public:
		AddComponentCommand(UUID id, std::string type, Json::Value fields = Json::Value::object());

		std::string GetName() const override;
		[[nodiscard]] std::expected<void, Error> Execute(Scene& scene) override;
		[[nodiscard]] std::expected<void, Error> Undo(Scene& scene) override;

	private:
		UUID m_Id;
		std::string m_Type;
		Json::Value m_Fields;
	};

	// Removes a component that isn't required. Undo restores its values
	class RemoveComponentCommand final : public Command
	{
	public:
		RemoveComponentCommand(UUID id, std::string type);

		std::string GetName() const override;
		[[nodiscard]] std::expected<void, Error> Execute(Scene& scene) override;
		[[nodiscard]] std::expected<void, Error> Undo(Scene& scene) override;

	private:
		UUID m_Id;
		std::string m_Type;
		std::vector<std::pair<std::string, FieldValue>> m_Values;
	};

	// Sets fields of a component. Continuous edits - a gizmo drag, a slider - merge into one undo step while they set
	// the same fields of the same component
	class SetFieldsCommand final : public Command
	{
	public:
		SetFieldsCommand(UUID id, std::string type, Json::Value fields, bool continuous = false);

		std::string GetName() const override;
		[[nodiscard]] std::expected<void, Error> Execute(Scene& scene) override;
		[[nodiscard]] std::expected<void, Error> Undo(Scene& scene) override;
		bool MergeWith(const Command& next) override;

	private:
		UUID m_Id;
		std::string m_Type;
		Json::Value m_Fields;
		bool m_Continuous;
		// The values before the command, for undo; kept from the first command when commands merge
		std::vector<std::pair<std::string, FieldValue>> m_OldValues;
	};

}
