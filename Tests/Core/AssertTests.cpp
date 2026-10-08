#include "Lodestone/Core/Assert.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Lodestone {

	namespace {

		struct RecordedFailure
		{
			AssertOrigin Origin = AssertOrigin::Core;
			std::string Condition;
			std::string Message;
			std::string File;
			uint32_t Line = 0;
		};

		// The handler is a plain function pointer, so it records into a test-global list
		std::vector<RecordedFailure> s_Failures;

		void RecordingAssertHandler(const AssertionFailure& failure)
		{
			s_Failures.push_back({
				.Origin = failure.Origin,
				.Condition = std::string(failure.Condition),
				.Message = std::string(failure.Message),
				.File = failure.Location.file_name(),
				.Line = failure.Location.line(),
			});
		}

		// Installs the recording handler for the lifetime of a test, then restores the previous handler
		class RecordingAssertHandlerScope
		{
		public:
			RecordingAssertHandlerScope()
				: m_PreviousHandler(SetAssertHandler(&RecordingAssertHandler))
			{
				s_Failures.clear();
			}

			~RecordingAssertHandlerScope()
			{
				SetAssertHandler(m_PreviousHandler);
				s_Failures.clear();
			}

			RecordingAssertHandlerScope(const RecordingAssertHandlerScope&) = delete;
			RecordingAssertHandlerScope& operator=(const RecordingAssertHandlerScope&) = delete;
			RecordingAssertHandlerScope(RecordingAssertHandlerScope&&) = delete;
			RecordingAssertHandlerScope& operator=(RecordingAssertHandlerScope&&) = delete;

		private:
			AssertHandler m_PreviousHandler;
		};

	}

	TEST_CASE("A passing assert doesn't call the handler")
	{
		const RecordingAssertHandlerScope scope;
		const int value = 3;

		LS_CORE_ASSERT(value == 3);
		LS_ASSERT(value > 0, "Value {} must be positive", value);

		CHECK(s_Failures.empty());
	}

#if LS_ENABLE_ASSERTS
	TEST_CASE("A failing core assert reports its condition, message and location")
	{
		const RecordingAssertHandlerScope scope;
		const int index = 7;
		const int size = 4;

		const uint32_t expectedLine = __LINE__ + 1;
		LS_CORE_ASSERT(index < size, "Index {} is out of range for size {}", index, size);

		REQUIRE(s_Failures.size() == 1);
		const RecordedFailure& failure = s_Failures.front();
		CHECK(failure.Origin == AssertOrigin::Core);
		CHECK(failure.Condition == "index < size");
		CHECK(failure.Message == "Index 7 is out of range for size 4");
		CHECK(failure.File.ends_with("AssertTests.cpp"));
		CHECK(failure.Line == expectedLine);
	}

	TEST_CASE("A failing app assert reports the app origin")
	{
		const RecordingAssertHandlerScope scope;

		LS_ASSERT(1 + 1 == 3);

		REQUIRE(s_Failures.size() == 1);
		CHECK(s_Failures.front().Origin == AssertOrigin::App);
		CHECK(s_Failures.front().Condition == "1 + 1 == 3");
		CHECK(s_Failures.front().Message.empty());
	}

	TEST_CASE("An assert evaluates its condition exactly once")
	{
		const RecordingAssertHandlerScope scope;
		int evaluations = 0;

		LS_CORE_ASSERT(++evaluations > 0);

		CHECK(evaluations == 1);
		CHECK(s_Failures.empty());
	}
#else
	TEST_CASE("Asserts are compiled out of Dist and don't evaluate their condition")
	{
		const RecordingAssertHandlerScope scope;
		int evaluations = 0;
		const auto countEvaluation = [&evaluations]()
		{
			++evaluations;
			return false;
		};

		LS_CORE_ASSERT(countEvaluation(), "Evaluated {} times", evaluations);

		CHECK(evaluations == 0);
		CHECK(s_Failures.empty());
	}
#endif

	TEST_CASE("SetAssertHandler returns the previous handler")
	{
		const AssertHandler original = SetAssertHandler(&RecordingAssertHandler);
		CHECK(SetAssertHandler(original) == &RecordingAssertHandler);
	}

}
