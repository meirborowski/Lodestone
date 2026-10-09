#include "Lodestone/Editor/Commands/EntityCommands.h"

#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/EntitySerializer.h"
#include "Lodestone/Scene/PrefabSerializer.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <iterator>

namespace Lodestone {

	namespace {

		using FieldValues = std::vector<std::pair<const FieldInfo*, FieldValue>>;

		std::expected<Entity, Error> FindEntity(Scene& scene, UUID id)
		{
			Entity entity = scene.FindEntity(id);
			if (!entity)
				return std::unexpected(Error(ErrorCode::NotFound, fmt::format("The scene has no entity {}", id)));
			return entity;
		}

		// A parent given by UUID: an invalid entity for the nil UUID, which means the root
		std::expected<Entity, Error> FindParent(Scene& scene, UUID id)
		{
			if (id.IsNil())
				return Entity();
			return FindEntity(scene, id);
		}

		// A component type that editing tools may use: registered, and not internal
		std::expected<const ComponentType*, Error> FindEditableType(const Scene& scene, std::string_view name)
		{
			const ComponentType* type = scene.GetComponentRegistry().Find(name);
			if (type == nullptr)
				return std::unexpected(Error(ErrorCode::NotFound, fmt::format("There's no {} component", name)));
			if (type->IsInternal())
				return std::unexpected(Error(ErrorCode::InvalidArgument,
					fmt::format("{} is an internal component, which can't be edited", name)));
			return type;
		}

		// Reads {"Field": value} for a component type, checking every value before anything uses them. Editing tools
		// can't set read-only fields
		std::expected<FieldValues, Error> ReadFieldValues(
			const ComponentType& type, const Json::Value& fields, std::string_view context)
		{
			if (!fields.is_object())
				return std::unexpected(Error(ErrorCode::InvalidArgument,
					fmt::format("{}: expected an object of fields, not {}", context, fields.type_name())));
			FieldValues values;
			for (const auto& [name, json] : fields.items())
			{
				const std::string fieldContext = fmt::format("{}.{}", context, name);
				const FieldInfo* field = type.FindField(name);
				if (field == nullptr)
					return std::unexpected(Error(ErrorCode::InvalidArgument,
						fmt::format("{}: {} has no field '{}'", fieldContext, type.GetName(), name)));
				if (field->IsReadOnly())
					return std::unexpected(
						Error(ErrorCode::InvalidArgument, fmt::format("{}: the field is read-only", fieldContext)));
				auto value = Json::ToFieldValue(json, field->GetType(), fieldContext);
				if (!value)
					return std::unexpected(Error(ErrorCode::InvalidArgument, value.error().GetMessageText()));
				if (auto valid = field->Validate(*value); !valid)
					return std::unexpected(valid.error().WithContext(fieldContext));
				values.emplace_back(field, std::move(*value));
			}
			return values;
		}

		void ApplyFieldValues(Scene& scene, Entity entity, const ComponentType& type, const FieldValues& values)
		{
			void* component = type.TryGet(scene.GetRegistry(), entity.GetHandle());
			LS_CORE_ASSERT(component != nullptr);
			for (const auto& [field, value] : values)
			{
				const auto set = field->Set(component, value);
				LS_CORE_ASSERT(set.has_value());
			}
			type.NotifyChanged(scene.GetRegistry(), entity.GetHandle());
		}

		// Every field's value, read-only ones included, by name
		std::vector<std::pair<std::string, FieldValue>> SaveFieldValues(
			const Scene& scene, Entity entity, const ComponentType& type)
		{
			const void* component = type.TryGet(scene.GetRegistry(), entity.GetHandle());
			LS_CORE_ASSERT(component != nullptr);
			std::vector<std::pair<std::string, FieldValue>> values;
			for (const FieldInfo& field : type.GetFields())
				values.emplace_back(field.GetName(), field.Get(component));
			return values;
		}

		void RestoreFieldValues(Scene& scene, Entity entity, const ComponentType& type,
			const std::vector<std::pair<std::string, FieldValue>>& values)
		{
			void* component = type.TryGet(scene.GetRegistry(), entity.GetHandle());
			LS_CORE_ASSERT(component != nullptr);
			for (const auto& [name, value] : values)
			{
				const FieldInfo* field = type.FindField(name);
				LS_CORE_ASSERT(field != nullptr);
				const auto set = field->Set(component, value);
				LS_CORE_ASSERT(set.has_value());
			}
			type.NotifyChanged(scene.GetRegistry(), entity.GetHandle());
		}

		// The entity's position among its siblings
		size_t GetSiblingIndex(const Scene& scene, Entity entity)
		{
			const UUID parent = entity.Get<HierarchyComponent>().Parent;
			const std::span<const UUID> siblings = parent.IsNil()
				? scene.GetRootEntities()
				: std::span<const UUID>(scene.GetRegistry().get<HierarchyComponent>(scene.FindHandle(parent)).Children);
			// find_if, not find: MSVC's vectorized find rejects UUIDs when clang compiles it
			const auto position =
				std::ranges::find_if(siblings, [id = entity.GetId()](UUID sibling) { return sibling == id; });
			LS_CORE_ASSERT(position != siblings.end());
			return static_cast<size_t>(std::distance(siblings.begin(), position));
		}

		std::expected<void, Error> DestroyById(Scene& scene, UUID id)
		{
			auto entity = FindEntity(scene, id);
			if (!entity)
				return std::unexpected(entity.error());
			scene.DestroyEntity(*entity);
			return {};
		}

	}

	CreateEntityCommand::CreateEntityCommand(
		std::string name, UUID parent, std::optional<size_t> index, Json::Value components)
		: m_Name(std::move(name)), m_Parent(parent), m_Index(index), m_Components(std::move(components))
	{
	}

	std::string CreateEntityCommand::GetName() const
	{
		return fmt::format("Create entity '{}'", m_Name);
	}

	std::expected<void, Error> CreateEntityCommand::Execute(Scene& scene)
	{
		if (!m_Components.is_object())
			return std::unexpected(Error(ErrorCode::InvalidArgument,
				fmt::format("components: expected an object, not {}", m_Components.type_name())));
		std::vector<std::pair<const ComponentType*, FieldValues>> components;
		for (const auto& [typeName, fields] : m_Components.items())
		{
			auto type = FindEditableType(scene, typeName);
			if (!type)
				return std::unexpected(type.error());
			auto values = ReadFieldValues(**type, fields, fmt::format("components.{}", typeName));
			if (!values)
				return std::unexpected(values.error());
			components.emplace_back(*type, std::move(*values));
		}
		const auto parent = FindParent(scene, m_Parent);
		if (!parent)
			return std::unexpected(parent.error());

		if (m_Id.IsNil())
			m_Id = scene.GenerateEntityId();
		auto entity = scene.CreateEntityWithId(m_Id, m_Name);
		if (!entity)
			return std::unexpected(entity.error());
		if (*parent || m_Index)
		{
			const auto placed = scene.SetParent(*entity, *parent, m_Index);
			LS_CORE_ASSERT(placed.has_value());
		}
		for (const auto& [type, values] : components)
		{
			if (!type->Has(scene.GetRegistry(), entity->GetHandle()))
				type->Add(scene.GetRegistry(), entity->GetHandle());
			ApplyFieldValues(scene, *entity, *type, values);
		}
		return {};
	}

	std::expected<void, Error> CreateEntityCommand::Undo(Scene& scene)
	{
		return DestroyById(scene, m_Id);
	}

	DestroyEntityCommand::DestroyEntityCommand(UUID id)
		: m_Id(id)
	{
	}

	std::string DestroyEntityCommand::GetName() const
	{
		// The entity's name is known once the command has run
		return m_EntityName ? fmt::format("Delete entity '{}'", *m_EntityName) : fmt::format("Delete entity {}", m_Id);
	}

	std::expected<void, Error> DestroyEntityCommand::Execute(Scene& scene)
	{
		const auto entity = FindEntity(scene, m_Id);
		if (!entity)
			return std::unexpected(entity.error());
		m_EntityName = entity->GetName();
		m_Parent = entity->Get<HierarchyComponent>().Parent;
		m_Index = GetSiblingIndex(scene, *entity);
		m_Tree = EntitySerializer::SerializeTree(scene, entity->GetHandle());
		scene.DestroyEntity(*entity);
		return {};
	}

	std::expected<void, Error> DestroyEntityCommand::Undo(Scene& scene)
	{
		const auto parent = FindParent(scene, m_Parent);
		if (!parent)
			return std::unexpected(parent.error());
		auto restored =
			EntitySerializer::InstantiateTree(scene, m_Tree, EntitySerializer::IdPolicy::Keep, *parent, m_Index);
		if (!restored)
			return std::unexpected(restored.error());
		return {};
	}

	DuplicateEntityCommand::DuplicateEntityCommand(UUID source)
		: m_Source(source)
	{
	}

	std::string DuplicateEntityCommand::GetName() const
	{
		return m_SourceName ? fmt::format("Duplicate entity '{}'", *m_SourceName)
							: fmt::format("Duplicate entity {}", m_Source);
	}

	std::expected<void, Error> DuplicateEntityCommand::Execute(Scene& scene)
	{
		const auto source = FindEntity(scene, m_Source);
		if (!source)
			return std::unexpected(source.error());
		m_SourceName = source->GetName();
		m_Parent = source->Get<HierarchyComponent>().Parent;
		m_Index = GetSiblingIndex(scene, *source) + 1;
		const Entity parent = scene.FindEntity(m_Parent);

		if (m_Copy.is_null())
		{
			auto copy =
				EntitySerializer::InstantiateTree(scene, EntitySerializer::SerializeTree(scene, source->GetHandle()),
					EntitySerializer::IdPolicy::Regenerate, parent, m_Index);
			if (!copy)
				return std::unexpected(copy.error());
			m_CopyId = copy->GetId();
			m_Copy = EntitySerializer::SerializeTree(scene, copy->GetHandle());
			return {};
		}
		auto copy = EntitySerializer::InstantiateTree(scene, m_Copy, EntitySerializer::IdPolicy::Keep, parent, m_Index);
		if (!copy)
			return std::unexpected(copy.error());
		return {};
	}

	std::expected<void, Error> DuplicateEntityCommand::Undo(Scene& scene)
	{
		return DestroyById(scene, m_CopyId);
	}

	InstantiatePrefabCommand::InstantiatePrefabCommand(
		Json::Value prefab, UUID prefabAsset, std::string prefabName, UUID parent, std::optional<size_t> index)
		: m_Prefab(std::move(prefab)),
		  m_PrefabAsset(prefabAsset),
		  m_PrefabName(std::move(prefabName)),
		  m_Parent(parent),
		  m_Index(index)
	{
	}

	std::string InstantiatePrefabCommand::GetName() const
	{
		return fmt::format("Instance prefab '{}'", m_PrefabName);
	}

	std::expected<void, Error> InstantiatePrefabCommand::Execute(Scene& scene)
	{
		const auto parent = FindParent(scene, m_Parent);
		if (!parent)
			return std::unexpected(parent.error());

		if (m_Instance.is_null())
		{
			auto instance = PrefabSerializer::Instantiate(scene, m_Prefab, m_PrefabAsset, *parent, m_Index);
			if (!instance)
				return std::unexpected(instance.error());
			m_InstanceId = instance->GetId();
			m_Instance = EntitySerializer::SerializeTree(scene, instance->GetHandle());
			return {};
		}
		auto instance =
			EntitySerializer::InstantiateTree(scene, m_Instance, EntitySerializer::IdPolicy::Keep, *parent, m_Index);
		if (!instance)
			return std::unexpected(instance.error());
		return {};
	}

	std::expected<void, Error> InstantiatePrefabCommand::Undo(Scene& scene)
	{
		return DestroyById(scene, m_InstanceId);
	}

	SetParentCommand::SetParentCommand(UUID id, UUID parent, std::optional<size_t> index)
		: m_Id(id), m_Parent(parent), m_Index(index)
	{
	}

	std::string SetParentCommand::GetName() const
	{
		return "Move entity";
	}

	std::expected<void, Error> SetParentCommand::Execute(Scene& scene)
	{
		const auto entity = FindEntity(scene, m_Id);
		if (!entity)
			return std::unexpected(entity.error());
		const auto parent = FindParent(scene, m_Parent);
		if (!parent)
			return std::unexpected(parent.error());
		m_OldParent = entity->Get<HierarchyComponent>().Parent;
		m_OldIndex = GetSiblingIndex(scene, *entity);
		return scene.SetParent(*entity, *parent, m_Index);
	}

	std::expected<void, Error> SetParentCommand::Undo(Scene& scene)
	{
		const auto entity = FindEntity(scene, m_Id);
		if (!entity)
			return std::unexpected(entity.error());
		const auto parent = FindParent(scene, m_OldParent);
		if (!parent)
			return std::unexpected(parent.error());
		return scene.SetParent(*entity, *parent, m_OldIndex);
	}

	AddComponentCommand::AddComponentCommand(UUID id, std::string type, Json::Value fields)
		: m_Id(id), m_Type(std::move(type)), m_Fields(std::move(fields))
	{
	}

	std::string AddComponentCommand::GetName() const
	{
		return fmt::format("Add {} component", m_Type);
	}

	std::expected<void, Error> AddComponentCommand::Execute(Scene& scene)
	{
		const auto entity = FindEntity(scene, m_Id);
		if (!entity)
			return std::unexpected(entity.error());
		const auto type = FindEditableType(scene, m_Type);
		if (!type)
			return std::unexpected(type.error());
		if ((*type)->Has(scene.GetRegistry(), entity->GetHandle()))
			return std::unexpected(Error(ErrorCode::AlreadyExists,
				fmt::format("Entity '{}' already has a {} component", entity->GetName(), m_Type)));
		const auto values = ReadFieldValues(**type, m_Fields, m_Type);
		if (!values)
			return std::unexpected(values.error());

		(*type)->Add(scene.GetRegistry(), entity->GetHandle());
		ApplyFieldValues(scene, *entity, **type, *values);
		return {};
	}

	std::expected<void, Error> AddComponentCommand::Undo(Scene& scene)
	{
		const auto entity = FindEntity(scene, m_Id);
		if (!entity)
			return std::unexpected(entity.error());
		const auto type = FindEditableType(scene, m_Type);
		if (!type)
			return std::unexpected(type.error());
		(*type)->Remove(scene.GetRegistry(), entity->GetHandle());
		return {};
	}

	RemoveComponentCommand::RemoveComponentCommand(UUID id, std::string type)
		: m_Id(id), m_Type(std::move(type))
	{
	}

	std::string RemoveComponentCommand::GetName() const
	{
		return fmt::format("Remove {} component", m_Type);
	}

	std::expected<void, Error> RemoveComponentCommand::Execute(Scene& scene)
	{
		const auto entity = FindEntity(scene, m_Id);
		if (!entity)
			return std::unexpected(entity.error());
		const auto type = FindEditableType(scene, m_Type);
		if (!type)
			return std::unexpected(type.error());
		if ((*type)->IsRequired())
			return std::unexpected(Error(ErrorCode::InvalidArgument,
				fmt::format("Every entity has a {} component, so it can't be removed", m_Type)));
		if (!(*type)->Has(scene.GetRegistry(), entity->GetHandle()))
			return std::unexpected(
				Error(ErrorCode::NotFound, fmt::format("Entity '{}' has no {} component", entity->GetName(), m_Type)));

		m_Values = SaveFieldValues(scene, *entity, **type);
		(*type)->Remove(scene.GetRegistry(), entity->GetHandle());
		return {};
	}

	std::expected<void, Error> RemoveComponentCommand::Undo(Scene& scene)
	{
		const auto entity = FindEntity(scene, m_Id);
		if (!entity)
			return std::unexpected(entity.error());
		const auto type = FindEditableType(scene, m_Type);
		if (!type)
			return std::unexpected(type.error());
		(*type)->Add(scene.GetRegistry(), entity->GetHandle());
		RestoreFieldValues(scene, *entity, **type, m_Values);
		return {};
	}

	SetFieldsCommand::SetFieldsCommand(UUID id, std::string type, Json::Value fields, bool continuous)
		: m_Id(id), m_Type(std::move(type)), m_Fields(std::move(fields)), m_Continuous(continuous)
	{
	}

	std::string SetFieldsCommand::GetName() const
	{
		std::string fields;
		if (m_Fields.is_object())
		{
			for (const auto& [name, value] : m_Fields.items())
				fields += fields.empty() ? name : ", " + name;
		}
		return fmt::format("Set {} {}", m_Type, fields);
	}

	std::expected<void, Error> SetFieldsCommand::Execute(Scene& scene)
	{
		const auto entity = FindEntity(scene, m_Id);
		if (!entity)
			return std::unexpected(entity.error());
		const auto type = FindEditableType(scene, m_Type);
		if (!type)
			return std::unexpected(type.error());
		if (!(*type)->Has(scene.GetRegistry(), entity->GetHandle()))
			return std::unexpected(
				Error(ErrorCode::NotFound, fmt::format("Entity '{}' has no {} component", entity->GetName(), m_Type)));
		const auto values = ReadFieldValues(**type, m_Fields, m_Type);
		if (!values)
			return std::unexpected(values.error());
		if (values->empty())
			return std::unexpected(Error(ErrorCode::InvalidArgument, fmt::format("{}: no fields to set", m_Type)));

		const void* component = (*type)->TryGet(scene.GetRegistry(), entity->GetHandle());
		m_OldValues.clear();
		for (const auto& [field, value] : *values)
			m_OldValues.emplace_back(field->GetName(), field->Get(component));
		ApplyFieldValues(scene, *entity, **type, *values);
		return {};
	}

	std::expected<void, Error> SetFieldsCommand::Undo(Scene& scene)
	{
		const auto entity = FindEntity(scene, m_Id);
		if (!entity)
			return std::unexpected(entity.error());
		const auto type = FindEditableType(scene, m_Type);
		if (!type)
			return std::unexpected(type.error());
		RestoreFieldValues(scene, *entity, **type, m_OldValues);
		return {};
	}

	bool SetFieldsCommand::MergeWith(const Command& next)
	{
		const auto* other = dynamic_cast<const SetFieldsCommand*>(&next);
		if (other == nullptr || !m_Continuous || !other->m_Continuous || other->m_Id != m_Id ||
			other->m_Type != m_Type || !m_Fields.is_object() || !other->m_Fields.is_object() ||
			m_Fields.size() != other->m_Fields.size())
			return false;
		for (const auto& [name, value] : m_Fields.items())
		{
			if (!other->m_Fields.contains(name))
				return false;
		}
		// The merged command undoes to this command's old values and redoes to the next command's new ones
		m_Fields = other->m_Fields;
		return true;
	}

}
