#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Editor/Commands/Command.h"
#include "Lodestone/Editor/EditorContext.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Lodestone {

	// Drag and drop payloads: an entity's UUID, and an asset's path relative to the asset directory
	inline constexpr const char* EntityPayload = "LS_ENTITY";
	inline constexpr const char* AssetPayload = "LS_ASSET";

	// Asks before an action that would lose unsaved changes to the document; runs it right away when there are none
	using UnsavedChangesGuard = std::function<void(std::string_view action, std::function<void()> run)>;

	// The UI has no caller to return errors to, so a failed action is logged, and the console shows it
	void ReportFailure(std::string_view action, const Error& error);
	// Runs a command, reporting a failure. Returns whether it ran
	bool RunCommand(EditorContext& context, Scope<Command> command);

	// How an entity appears in lists: its name, or a placeholder when it has none
	std::string GetEntityLabel(const Scene& scene, UUID id);

	// Makes the last item a drag source for an entity
	void SetEntityDragSource(const Scene& scene, UUID id);
	// The entity dropped on the last item, if any
	std::optional<UUID> AcceptEntityDrop();
	// The path of an asset dropped on the last item, if any
	std::optional<std::string> AcceptAssetDrop();

}
