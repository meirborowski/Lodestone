#pragma once

#include "Lodestone/Editor/EditorContext.h"

namespace Lodestone {

	// The timeline of the selected entity's animator (its own, or its model instance's): pick a clip, play it, or scrub
	// through it, previewed in the viewport without changing the document. While the scene plays, it follows the
	// running animator
	class AnimationPanel
	{
	public:
		static constexpr const char* Title = "Animation";

		void Draw(EditorContext& context, bool* open);
	};

}
