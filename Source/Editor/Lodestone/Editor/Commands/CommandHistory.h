#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Editor/Commands/Command.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace Lodestone {

	// Runs commands and keeps them for undo and redo. Running a new command clears what could be redone; the oldest
	// commands are forgotten beyond the capacity
	class CommandHistory
	{
	public:
		explicit CommandHistory(size_t capacity = 512);

		// Runs a command. When it succeeds, it can be undone, and it may merge into the previous command (see
		// Command::MergeWith); when it fails, nothing changes
		[[nodiscard]] std::expected<void, Error> Execute(Scene& scene, Scope<Command> command);
		[[nodiscard]] std::expected<void, Error> Undo(Scene& scene);
		[[nodiscard]] std::expected<void, Error> Redo(Scene& scene);

		bool CanUndo() const { return !m_Undo.empty(); }
		bool CanRedo() const { return !m_Redo.empty(); }
		// What Undo() and Redo() would do, if anything
		std::optional<std::string> GetUndoName() const;
		std::optional<std::string> GetRedoName() const;
		// Every command that can be undone, the most recent first
		std::vector<std::string> GetUndoNames() const;

		// Ends a continuous edit, so the next command starts a new undo step even if it could merge
		void EndMerge() { m_MergeAllowed = false; }
		void Clear();

		// Identifies the scene's state: it changes with every command, undo and redo, and returns to an earlier value
		// when undo or redo returns to that state - so comparing it with the value when the scene was saved tells
		// whether there are unsaved changes
		uint64_t GetStateId() const;

	private:
		struct Entry
		{
			Scope<Command> Action;
			uint64_t StateId = 0;
		};

	private:
		size_t m_Capacity;
		std::deque<Entry> m_Undo;
		std::vector<Entry> m_Redo;
		// The state with nothing left to undo
		uint64_t m_BaseStateId = 0;
		uint64_t m_NextStateId = 1;
		bool m_MergeAllowed = false;
	};

}
