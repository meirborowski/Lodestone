#pragma once

#include "Lodestone/Core/Assert.h"
#include "Lodestone/Scene/Components.h"
#include "Lodestone/Scene/Scene.h"

#include <entt/entity/entity.hpp>

#include <string>
#include <utility>

namespace Lodestone {

	// A handle to an entity in a scene. Cheap to copy; it doesn't own the entity, and becomes invalid when the entity
	// is destroyed
	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity handle, Scene* scene)
			: m_Handle(handle), m_Scene(scene)
		{
		}

		// Adds a component, which the entity mustn't have already
		template <typename T, typename... Args>
		T& Add(Args&&... args)
		{
			LS_CORE_ASSERT(IsValid(), "Adding a component to an invalid entity");
			return GetRegistry().emplace<T>(m_Handle, std::forward<Args>(args)...);
		}

		// The entity's component, which must exist
		template <typename T>
		T& Get()
		{
			LS_CORE_ASSERT(Has<T>(), "The entity doesn't have the component");
			return GetRegistry().get<T>(m_Handle);
		}
		template <typename T>
		const T& Get() const
		{
			LS_CORE_ASSERT(Has<T>(), "The entity doesn't have the component");
			return GetRegistry().get<T>(m_Handle);
		}

		// The entity's component, or nullptr if it has none
		template <typename T>
		T* TryGet()
		{
			return GetRegistry().try_get<T>(m_Handle);
		}
		template <typename T>
		const T* TryGet() const
		{
			return GetRegistry().try_get<T>(m_Handle);
		}

		template <typename T>
		bool Has() const
		{
			return GetRegistry().all_of<T>(m_Handle);
		}

		// Removes a component, if the entity has one. Required components can't be removed
		template <typename T>
		void Remove()
		{
			const ComponentType* type = m_Scene->GetComponentRegistry().Find<T>();
			LS_CORE_ASSERT(type == nullptr || !type->IsRequired(), "{} components can't be removed", type->GetName());
			GetRegistry().remove<T>(m_Handle);
		}

		UUID GetId() const { return Get<IDComponent>().ID; }
		const std::string& GetName() const { return Get<NameComponent>().Name; }
		TransformComponent& GetTransform() { return Get<TransformComponent>(); }
		const TransformComponent& GetTransform() const { return Get<TransformComponent>(); }

		// Whether the handle refers to an entity that still exists
		bool IsValid() const { return m_Scene != nullptr && m_Scene->GetRegistry().valid(m_Handle); }
		explicit operator bool() const { return IsValid(); }

		entt::entity GetHandle() const { return m_Handle; }
		Scene* GetScene() const { return m_Scene; }

		bool operator==(const Entity& other) const = default;

	private:
		entt::registry& GetRegistry() const
		{
			LS_CORE_ASSERT(m_Scene != nullptr, "The entity handle is empty");
			return m_Scene->GetRegistry();
		}

	private:
		entt::entity m_Handle = entt::null;
		Scene* m_Scene = nullptr;
	};

}
