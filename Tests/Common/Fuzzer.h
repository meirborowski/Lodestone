#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Lodestone::Testing {

	// Runs code that parses untrusted input on one input. It must handle any input without crashing, hanging or
	// tripping a sanitizer; returning an error is fine. It can also check properties of what it parsed, with doctest
	using FuzzTarget = std::function<void(std::string_view input)>;

	struct FuzzOptions
	{
		// Valid inputs to start from, one per file
		std::filesystem::path CorpusDirectory;
		// Strings that mutations insert, such as the keys and values of a file format
		std::vector<std::string> Dictionary;
		std::chrono::milliseconds Budget{5000};
		uint64_t Seed = 0;
		size_t MaxInputSize = 64ull * 1024;
	};

	struct FuzzReport
	{
		uint64_t Runs = 0;
	};

	// The options for a fuzz test, with its corpus in Tests/Fuzz/Corpus/<corpusName>. The time budget comes from
	// LS_FUZZ_SECONDS, in whole seconds (default 5), and the seed from LS_FUZZ_SEED (default random); the seed is
	// printed, so a failure can be reproduced by running again with it
	FuzzOptions MakeFuzzOptions(std::string_view corpusName);

	// Feeds the target every corpus input, then random mutations of them - bit flips, insertions, deletions,
	// duplications, splices and dictionary strings - until the time budget is spent. Mutations depend only on the
	// seed and the corpus, so the same seed replays the same inputs
	FuzzReport RunFuzzer(const FuzzTarget& target, const FuzzOptions& options);

}
