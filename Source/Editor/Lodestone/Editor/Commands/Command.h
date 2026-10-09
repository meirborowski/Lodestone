#pragma once

#include "Lodestone/Core/Error.h"
#include "Lodestone/Scene/Scene.h"

#include <expected>
#include <string>

namespace Lodestone {

	// An editing operation that can be undone. The editor UI and MCP change scenes only through commands, run by a
	// CommandHistory, so everything either of them does can be undone (see docs/Decisions/0015-editor-commands.md).
	//
	// Commands refer to entities by UUID, never by handle: undoing a delete recreates entities with their UUIDs, but
	// not with their handles
	class Command
	{
	public:
		Command() = default;
		virtual ~Command() = default;

		Command(const Command&) = delete;
		Command& operator=(const Command&) = delete;
		Command(Command&&) = delete;
		Command& operator=(Command&&) = delete;

		// What the command does, for menus and MCP: "Create entity 'Player'"
		virtual std::string GetName() const = 0;

		// Applies the command. Redo calls it again, so it must have the same result every time - including the UUIDs
		// of entities it creates. A failed command leaves the scene as it was
		[[nodiscard]] virtual std::expected<void, Error> Execute(Scene& scene) = 0;
		// Reverses a successful Execute(), restoring the scene exactly
		[[nodiscard]] virtual std::expected<void, Error> Undo(Scene& scene) = 0;

		// Absorbs the command that ran right after this one, so a continuous edit - a gizmo drag, a slider - undoes in
		// one step. Returns whether it did
		virtual bool MergeWith(const Command& /*next*/) { return false; }
	};

}
