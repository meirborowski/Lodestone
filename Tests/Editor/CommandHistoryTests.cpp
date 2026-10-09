#include "Lodestone/Editor/Commands/CommandHistory.h"

#include "Common/LogLevelScope.h"

#include <doctest/doctest.h>

#include <string>
#include <utility>

namespace Lodestone {

	namespace {

		// Adds to a counter, so tests can see what ran. Commands with the same key merge
		class AddCommand final : public Command
		{
		public:
			AddCommand(int& counter, int amount, int mergeKey = 0)
				: m_Counter(&counter), m_Amount(amount), m_MergeKey(mergeKey)
			{
			}

			std::string GetName() const override { return "Add " + std::to_string(m_Amount); }

			std::expected<void, Error> Execute(Scene& /*scene*/) override
			{
				if (FailExecute)
					return std::unexpected(Error(ErrorCode::InvalidState, "Execute failed"));
				*m_Counter += m_Amount;
				return {};
			}

			std::expected<void, Error> Undo(Scene& /*scene*/) override
			{
				if (FailUndo)
					return std::unexpected(Error(ErrorCode::InvalidState, "Undo failed"));
				*m_Counter -= m_Amount;
				return {};
			}

			bool MergeWith(const Command& next) override
			{
				const auto* add = dynamic_cast<const AddCommand*>(&next);
				if (add == nullptr || m_MergeKey == 0 || add->m_MergeKey != m_MergeKey)
					return false;
				m_Amount += add->m_Amount;
				return true;
			}

			bool FailExecute = false;
			bool FailUndo = false;

		private:
			int* m_Counter;
			int m_Amount;
			int m_MergeKey;
		};

	}

	TEST_CASE("Commands run, undo and redo in order")
	{
		Scene scene;
		CommandHistory history;
		int counter = 0;

		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 1)).has_value());
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 10)).has_value());
		CHECK(counter == 11);
		CHECK(history.GetUndoName() == "Add 10");
		CHECK(history.GetUndoNames() == std::vector<std::string>{"Add 10", "Add 1"});
		CHECK_FALSE(history.CanRedo());

		REQUIRE(history.Undo(scene).has_value());
		CHECK(counter == 1);
		CHECK(history.GetRedoName() == "Add 10");
		REQUIRE(history.Undo(scene).has_value());
		CHECK(counter == 0);
		CHECK_FALSE(history.CanUndo());

		REQUIRE(history.Redo(scene).has_value());
		REQUIRE(history.Redo(scene).has_value());
		CHECK(counter == 11);
		CHECK_FALSE(history.CanRedo());
	}

	TEST_CASE("Undo and redo with nothing to do fail")
	{
		Scene scene;
		CommandHistory history;

		CHECK(history.Undo(scene).error().GetCode() == ErrorCode::InvalidState);
		CHECK(history.Redo(scene).error().GetCode() == ErrorCode::InvalidState);
		CHECK_FALSE(history.GetUndoName().has_value());
		CHECK_FALSE(history.GetRedoName().has_value());
	}

	TEST_CASE("A new command clears what could be redone")
	{
		Scene scene;
		CommandHistory history;
		int counter = 0;
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 1)).has_value());
		REQUIRE(history.Undo(scene).has_value());

		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 5)).has_value());

		CHECK_FALSE(history.CanRedo());
		CHECK(counter == 5);
	}

	TEST_CASE("A failed command isn't kept and changes nothing")
	{
		Scene scene;
		CommandHistory history;
		int counter = 0;
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 1)).has_value());
		REQUIRE(history.Undo(scene).has_value());
		const uint64_t state = history.GetStateId();
		auto failing = CreateScope<AddCommand>(counter, 7);
		failing->FailExecute = true;

		const auto executed = history.Execute(scene, std::move(failing));

		REQUIRE_FALSE(executed.has_value());
		CHECK(counter == 0);
		CHECK_FALSE(history.CanUndo());
		// What could be redone still can
		CHECK(history.CanRedo());
		CHECK(history.GetStateId() == state);
	}

	TEST_CASE("Continuous edits merge into one undo step until the merge ends")
	{
		Scene scene;
		CommandHistory history;
		int counter = 0;

		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 1, 1)).has_value());
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 2, 1)).has_value());
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 3, 1)).has_value());
		history.EndMerge();
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 4, 1)).has_value());

		CHECK(counter == 10);
		CHECK(history.GetUndoNames() == std::vector<std::string>{"Add 4", "Add 6"});
		REQUIRE(history.Undo(scene).has_value());
		REQUIRE(history.Undo(scene).has_value());
		CHECK(counter == 0);
	}

	TEST_CASE("Commands don't merge across an undo")
	{
		Scene scene;
		CommandHistory history;
		int counter = 0;
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 1, 1)).has_value());
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 2, 1)).has_value());
		REQUIRE(history.Undo(scene).has_value());

		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 5, 1)).has_value());

		CHECK(history.GetUndoNames().size() == 1);
		REQUIRE(history.Undo(scene).has_value());
		CHECK(counter == 0);
	}

	TEST_CASE("The oldest commands are forgotten beyond the capacity")
	{
		Scene scene;
		CommandHistory history(3);
		int counter = 0;
		for (int amount = 1; amount <= 5; ++amount)
			REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, amount)).has_value());

		CHECK(history.GetUndoNames() == std::vector<std::string>{"Add 5", "Add 4", "Add 3"});
		while (history.CanUndo())
			REQUIRE(history.Undo(scene).has_value());
		// The first two can't be undone any more
		CHECK(counter == 3);
	}

	TEST_CASE("The state ID tells whether the scene is back to an earlier state")
	{
		Scene scene;
		CommandHistory history;
		int counter = 0;
		const uint64_t empty = history.GetStateId();
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 1)).has_value());
		const uint64_t one = history.GetStateId();
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 2)).has_value());
		const uint64_t two = history.GetStateId();
		CHECK(empty != one);
		CHECK(one != two);

		REQUIRE(history.Undo(scene).has_value());
		CHECK(history.GetStateId() == one);
		REQUIRE(history.Undo(scene).has_value());
		CHECK(history.GetStateId() == empty);
		REQUIRE(history.Redo(scene).has_value());
		CHECK(history.GetStateId() == one);

		// A different second command is a different state, even with the same depth
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 2)).has_value());
		CHECK(history.GetStateId() != two);

		// A merge changes the state too
		history.EndMerge();
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 1, 9)).has_value());
		const uint64_t beforeMerge = history.GetStateId();
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 1, 9)).has_value());
		CHECK(history.GetStateId() != beforeMerge);
	}

	TEST_CASE("Forgetting old commands keeps the state IDs of the rest")
	{
		Scene scene;
		CommandHistory history(1);
		int counter = 0;
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 1)).has_value());
		const uint64_t one = history.GetStateId();
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 2)).has_value());

		REQUIRE(history.Undo(scene).has_value());

		// Back to the state after the first command, which can't be undone any more
		CHECK(history.GetStateId() == one);
	}

	TEST_CASE("A failed undo clears the history")
	{
		const Testing::LogLevelScope quiet(LogLevel::Critical);
		Scene scene;
		CommandHistory history;
		int counter = 0;
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 1)).has_value());
		auto failing = CreateScope<AddCommand>(counter, 2);
		failing->FailUndo = true;
		REQUIRE(history.Execute(scene, std::move(failing)).has_value());
		const uint64_t state = history.GetStateId();

		REQUIRE_FALSE(history.Undo(scene).has_value());

		CHECK_FALSE(history.CanUndo());
		CHECK_FALSE(history.CanRedo());
		CHECK(history.GetStateId() != state);
	}

	TEST_CASE("Clearing the history starts a new state")
	{
		Scene scene;
		CommandHistory history;
		int counter = 0;
		const uint64_t empty = history.GetStateId();
		REQUIRE(history.Execute(scene, CreateScope<AddCommand>(counter, 1)).has_value());

		history.Clear();

		CHECK_FALSE(history.CanUndo());
		CHECK(history.GetStateId() != empty);
		CHECK(counter == 1);
	}

}
