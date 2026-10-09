#pragma once

#include "Lodestone/Editor/EditorContext.h"
#include "Lodestone/Editor/UI/UIHelpers.h"

#include <functional>
#include <string>
#include <vector>

namespace Lodestone {

	// The project's assets, a directory at a time: open scenes and prefabs, and drag prefabs into the scene
	class ContentBrowserPanel
	{
	public:
		static constexpr const char* Title = "Content Browser";

		void Draw(EditorContext& context, const ConfirmDiscard& confirmDiscard, bool* open);

	private:
		void DrawToolbar(EditorContext& context);
		void DrawAsset(EditorContext& context, const ConfirmDiscard& confirmDiscard, const AssetInfo& asset);

	private:
		// Relative to the asset directory, with forward slashes; empty for the asset directory itself
		std::string m_Directory;
		std::string m_Selected;
		std::string m_Filter;
		// Changes wait until the list is drawn, since they can change the assets it's iterating over
		std::vector<std::function<void()>> m_Deferred;
	};

}
