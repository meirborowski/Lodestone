#pragma once

#include "Lodestone/Core/UUID.h"
#include "Lodestone/Editor/EditorContext.h"

#include <functional>
#include <vector>

namespace Lodestone {

	// The document's entities as a tree: select, create, duplicate, delete, and drag to reparent or to instance a
	// prefab
	class HierarchyPanel
	{
	public:
		static constexpr const char* Title = "Hierarchy";

		void Draw(EditorContext& context, bool* open);

	private:
		void DrawEntity(EditorContext& context, UUID id);
		void DrawEntityMenu(EditorContext& context, UUID id);
		// Changes to the scene wait until the tree is drawn, since they'd change what it's iterating over
		void Defer(std::function<void()> change) { m_Deferred.push_back(std::move(change)); }

	private:
		std::vector<std::function<void()>> m_Deferred;
	};

}
