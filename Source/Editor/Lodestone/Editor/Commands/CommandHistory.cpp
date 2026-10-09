#include "Lodestone/Editor/Commands/CommandHistory.h"

#include "Lodestone/Core/Assert.h"
#include "Lodestone/Core/Log.h"

#include <utility>

namespace Lodestone {

	CommandHistory::CommandHistory(size_t capacity)
		: m_Capacity(capacity)
	{
		LS_CORE_ASSERT(capacity > 0, "A command history needs room for at least one command");
	}

	std::expected<void, Error> CommandHistory::Execute(Scene& scene, Scope<Command> command)
	{
		LS_CORE_ASSERT(command != nullptr);
		if (auto executed = command->Execute(scene); !executed)
			return executed;

		m_Redo.clear();
		if (m_MergeAllowed && !m_Undo.empty() && m_Undo.back().Action->MergeWith(*command))
		{
			m_Undo.back().StateId = m_NextStateId++;
			return {};
		}

		m_Undo.push_back({.Action = std::move(command), .StateId = m_NextStateId++});
		if (m_Undo.size() > m_Capacity)
		{
			m_BaseStateId = m_Undo.front().StateId;
			m_Undo.pop_front();
		}
		m_MergeAllowed = true;
		return {};
	}

	std::expected<void, Error> CommandHistory::Undo(Scene& scene)
	{
		if (m_Undo.empty())
			return std::unexpected(Error(ErrorCode::InvalidState, "There's nothing to undo"));

		Entry entry = std::move(m_Undo.back());
		m_Undo.pop_back();
		m_MergeAllowed = false;
		if (auto undone = entry.Action->Undo(scene); !undone)
		{
			// The scene no longer matches what the history expects, so none of it can be trusted
			LS_CORE_ERROR(
				"Undoing '{}' failed, so the undo history was cleared: {}", entry.Action->GetName(), undone.error());
			Clear();
			return undone;
		}
		m_Redo.push_back(std::move(entry));
		return {};
	}

	std::expected<void, Error> CommandHistory::Redo(Scene& scene)
	{
		if (m_Redo.empty())
			return std::unexpected(Error(ErrorCode::InvalidState, "There's nothing to redo"));

		Entry entry = std::move(m_Redo.back());
		m_Redo.pop_back();
		m_MergeAllowed = false;
		if (auto redone = entry.Action->Execute(scene); !redone)
		{
			LS_CORE_ERROR(
				"Redoing '{}' failed, so the redo history was cleared: {}", entry.Action->GetName(), redone.error());
			m_Redo.clear();
			return redone;
		}
		m_Undo.push_back(std::move(entry));
		return {};
	}

	std::optional<std::string> CommandHistory::GetUndoName() const
	{
		return m_Undo.empty() ? std::nullopt : std::optional(m_Undo.back().Action->GetName());
	}

	std::optional<std::string> CommandHistory::GetRedoName() const
	{
		return m_Redo.empty() ? std::nullopt : std::optional(m_Redo.back().Action->GetName());
	}

	std::vector<std::string> CommandHistory::GetUndoNames() const
	{
		std::vector<std::string> names;
		names.reserve(m_Undo.size());
		for (auto entry = m_Undo.rbegin(); entry != m_Undo.rend(); ++entry)
			names.push_back(entry->Action->GetName());
		return names;
	}

	void CommandHistory::Clear()
	{
		m_Undo.clear();
		m_Redo.clear();
		m_BaseStateId = m_NextStateId++;
		m_MergeAllowed = false;
	}

	uint64_t CommandHistory::GetStateId() const
	{
		return m_Undo.empty() ? m_BaseStateId : m_Undo.back().StateId;
	}

}
