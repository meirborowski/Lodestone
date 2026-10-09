#pragma once

#include "Lodestone/Core/Assert.h"
#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/EnumFlags.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Reflection/FieldValue.h"
#include "Lodestone/Reflection/SnapshotArchive.h"

#include <entt/core/type_info.hpp>
#include <entt/entity/registry.hpp>
#include <entt/entity/snapshot.hpp>

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Lodestone {

	enum class FieldFlags : uint32_t
	{
		None = 0,
		// Editing tools (the inspector, MCP, scripts) can read the field but not change it. Loading files still sets it
		ReadOnly = 1 << 0,
		// Sent to clients when it changes (see docs/Features/Networking.md#replication)
		Replicated = 1 << 1,
	};

	template <>
	struct EnableFlagOperators<FieldFlags> : std::true_type
	{
	};

	enum class ComponentFlags : uint32_t
	{
		None = 0,
		// Every entity has one, from creation to destruction: editing tools can't add or remove it
		Required = 1 << 0,
		// Engine bookkeeping, such as interpolation state: part of the simulation state, but never saved to files or
		// exposed to editing tools
		Internal = 1 << 1,
	};

	template <>
	struct EnableFlagOperators<ComponentFlags> : std::true_type
	{
	};

	// Options for a reflected field
	struct FieldOptions
	{
		// What the field means, for tooltips, MCP schemas and script documentation
		std::string_view Description;
		// Inclusive limits for Int, UInt and Float fields, and for each element of vector fields
		std::optional<double> Min;
		std::optional<double> Max;
		FieldFlags Flags = FieldFlags::None;
	};

	// One field of a reflected component: its name, type, limits and flags, and how to read and write it in a
	// component instance
	class FieldInfo
	{
	public:
		using Getter = std::function<FieldValue(const void* component)>;
		using Setter = std::function<void(void* component, const FieldValue& value)>;

		FieldInfo(std::string name, FieldType type, const FieldOptions& options, FieldValue defaultValue, Getter getter,
			Setter setter);

		const std::string& GetName() const { return m_Name; }
		FieldType GetType() const { return m_Type; }
		const std::string& GetDescription() const { return m_Description; }
		std::optional<double> GetMin() const { return m_Min; }
		std::optional<double> GetMax() const { return m_Max; }
		FieldFlags GetFlags() const { return m_Flags; }
		bool IsReadOnly() const { return HasFlag(m_Flags, FieldFlags::ReadOnly); }
		bool IsReplicated() const { return HasFlag(m_Flags, FieldFlags::Replicated); }
		// The field's value in a default-constructed component
		const FieldValue& GetDefault() const { return m_Default; }

		FieldValue Get(const void* component) const { return m_Getter(component); }
		// Checks that a value has the field's type and is within its limits
		[[nodiscard]] std::expected<void, Error> Validate(const FieldValue& value) const;
		// Validates a value, then writes it into a component. Read-only fields can be set too: the flag is a rule for
		// editing tools, which check it themselves
		[[nodiscard]] std::expected<void, Error> Set(void* component, const FieldValue& value) const;

	private:
		std::string m_Name;
		FieldType m_Type;
		std::string m_Description;
		std::optional<double> m_Min;
		std::optional<double> m_Max;
		FieldFlags m_Flags;
		FieldValue m_Default;
		Getter m_Getter;
		Setter m_Setter;
	};

	namespace Detail {

		// The operations on one C++ component type that ComponentType performs without knowing the type
		struct ComponentOperations
		{
			bool (*Has)(const entt::registry& registry, entt::entity entity) = nullptr;
			void* (*TryGet)(entt::registry& registry, entt::entity entity) = nullptr;
			const void* (*TryGetConst)(const entt::registry& registry, entt::entity entity) = nullptr;
			void* (*Add)(entt::registry& registry, entt::entity entity) = nullptr;
			void (*Remove)(entt::registry& registry, entt::entity entity) = nullptr;
			void (*NotifyChanged)(entt::registry& registry, entt::entity entity) = nullptr;
			void (*SaveSnapshot)(const entt::snapshot& snapshot, SnapshotArchive::Writer& writer) = nullptr;
			void (*LoadSnapshot)(entt::snapshot_loader& loader, SnapshotArchive::Reader& reader) = nullptr;

			template <typename T>
			static ComponentOperations For()
			{
				return {
					.Has = [](const entt::registry& registry, entt::entity entity)
					{ return registry.all_of<T>(entity); },
					.TryGet = [](entt::registry& registry, entt::entity entity) -> void*
					{ return registry.try_get<T>(entity); },
					.TryGetConst = [](const entt::registry& registry, entt::entity entity) -> const void*
					{ return registry.try_get<T>(entity); },
					.Add = [](entt::registry& registry, entt::entity entity) -> void*
					{ return &registry.emplace<T>(entity); },
					.Remove = [](entt::registry& registry, entt::entity entity) { registry.remove<T>(entity); },
					.NotifyChanged = [](entt::registry& registry, entt::entity entity) { registry.patch<T>(entity); },
					.SaveSnapshot = [](const entt::snapshot& snapshot, SnapshotArchive::Writer& writer)
					{ snapshot.get<T>(writer); },
					.LoadSnapshot = [](entt::snapshot_loader& loader, SnapshotArchive::Reader& reader)
					{ loader.get<T>(reader); },
				};
			}
		};

	}

	// A component type known to the reflection registry: its name, fields and flags, and the operations that let
	// serialization, the inspector, MCP, scripts and replication work with it without knowing the C++ type
	class ComponentType
	{
	public:
		ComponentType(std::string name, std::string description, ComponentFlags flags, entt::id_type typeId,
			Detail::ComponentOperations operations);

		const std::string& GetName() const { return m_Name; }
		const std::string& GetDescription() const { return m_Description; }
		ComponentFlags GetFlags() const { return m_Flags; }
		bool IsRequired() const { return HasFlag(m_Flags, ComponentFlags::Required); }
		bool IsInternal() const { return HasFlag(m_Flags, ComponentFlags::Internal); }
		// The C++ type's identifier (entt::type_hash)
		entt::id_type GetTypeId() const { return m_TypeId; }

		std::span<const FieldInfo> GetFields() const { return m_Fields; }
		const FieldInfo* FindField(std::string_view name) const;

		bool Has(const entt::registry& registry, entt::entity entity) const
		{
			return m_Operations.Has(registry, entity);
		}
		// The entity's component, or nullptr if it has none
		void* TryGet(entt::registry& registry, entt::entity entity) const
		{
			return m_Operations.TryGet(registry, entity);
		}
		const void* TryGet(const entt::registry& registry, entt::entity entity) const
		{
			return m_Operations.TryGetConst(registry, entity);
		}
		// Adds a default-constructed component. The entity mustn't have one already
		void* Add(entt::registry& registry, entt::entity entity) const;
		// Removes the entity's component, if it has one
		void Remove(entt::registry& registry, entt::entity entity) const { m_Operations.Remove(registry, entity); }
		// Tells observers (replication, the editor) that a component changed through a pointer from TryGet()
		void NotifyChanged(entt::registry& registry, entt::entity entity) const
		{
			m_Operations.NotifyChanged(registry, entity);
		}
		// Sets a field of the entity's component, which must exist, and notifies observers
		[[nodiscard]] std::expected<void, Error> SetField(
			entt::registry& registry, entt::entity entity, const FieldInfo& field, const FieldValue& value) const;

		// Writes or reads every component of this type, for simulation state snapshots
		void SaveSnapshot(const entt::snapshot& snapshot, SnapshotArchive::Writer& writer) const
		{
			m_Operations.SaveSnapshot(snapshot, writer);
		}
		void LoadSnapshot(entt::snapshot_loader& loader, SnapshotArchive::Reader& reader) const
		{
			m_Operations.LoadSnapshot(loader, reader);
		}

	private:
		template <typename T>
		friend class ComponentBuilder;

		void AddField(FieldInfo field);

	private:
		std::string m_Name;
		std::string m_Description;
		ComponentFlags m_Flags;
		entt::id_type m_TypeId;
		Detail::ComponentOperations m_Operations;
		std::vector<FieldInfo> m_Fields;
	};

	// Adds fields to a component type as it's registered:
	//   registry.Register<TransformComponent>("Transform", "Position, rotation and scale")
	//       .Field("Position", &TransformComponent::Position, {.Description = "Relative to the parent"});
	template <typename T>
	class ComponentBuilder
	{
	public:
		explicit ComponentBuilder(ComponentType& type)
			: m_Type(&type)
		{
		}

		template <ReflectableFieldType Value>
		ComponentBuilder& Field(std::string name, Value T::* member, const FieldOptions& options = {})
		{
			const T defaults{};
			m_Type->AddField(FieldInfo(
				std::move(name), FieldTypeOf<Value>::Value, options, FieldValue(defaults.*member),
				[member](const void* component) { return FieldValue(static_cast<const T*>(component)->*member); },
				[member](void* component, const FieldValue& value)
				{ static_cast<T*>(component)->*member = std::get<Value>(value); }));
			return *this;
		}

	private:
		ComponentType* m_Type;
	};

	// Every reflected component type. Component types are registered once, at startup, before scenes use them; after
	// that the registry is only read, from any thread.
	//
	// Names identify component types in files, MCP and scripts, so they must be unique identifiers ("Transform") and
	// never change once files use them
	class ComponentRegistry
	{
	public:
		ComponentRegistry() = default;
		~ComponentRegistry() = default;

		ComponentRegistry(const ComponentRegistry&) = delete;
		ComponentRegistry& operator=(const ComponentRegistry&) = delete;
		ComponentRegistry(ComponentRegistry&&) = delete;
		ComponentRegistry& operator=(ComponentRegistry&&) = delete;

		template <typename T>
		ComponentBuilder<T> Register(
			std::string name, std::string description = {}, ComponentFlags flags = ComponentFlags::None)
		{
			static_assert(std::is_default_constructible_v<T> && std::is_copy_constructible_v<T>,
				"Components must be default-constructible and copyable");
			ComponentType& type = AddType(CreateScope<ComponentType>(std::move(name), std::move(description), flags,
				entt::type_hash<T>::value(), Detail::ComponentOperations::For<T>()));
			return ComponentBuilder<T>(type);
		}

		const ComponentType* Find(std::string_view name) const;
		const ComponentType* FindByTypeId(entt::id_type typeId) const;
		template <typename T>
		const ComponentType* Find() const
		{
			return FindByTypeId(entt::type_hash<T>::value());
		}

		// In the order they were registered
		std::span<const ComponentType* const> GetTypes() const { return m_Order; }

	private:
		ComponentType& AddType(Scope<ComponentType> type);

	private:
		std::vector<Scope<ComponentType>> m_Types;
		std::vector<const ComponentType*> m_Order;
		std::unordered_map<std::string_view, const ComponentType*> m_ByName;
		std::unordered_map<entt::id_type, const ComponentType*> m_ByTypeId;
	};

}
