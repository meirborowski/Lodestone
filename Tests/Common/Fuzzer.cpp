#include "Common/Fuzzer.h"

#include "Common/DescribeError.h"
#include "Lodestone/Core/Environment.h"
#include "Lodestone/Core/FileSystem.h"

#include <doctest/doctest.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <random>

// Set by Tests/CMakeLists.txt
#if !defined(LS_FUZZ_CORPUS_DIR)
	#error "LS_FUZZ_CORPUS_DIR must be defined"
#endif

namespace Lodestone::Testing {

	namespace {

		// Bytes that often matter to parsers
		constexpr std::array<char, 16> InterestingBytes = {
			'\0', '\xFF', '\x7F', '\x80', '"', '\\', '{', '}', '[', ']', ':', ',', '-', '0', '9', 'e'};

		std::vector<std::string> LoadCorpus(const std::filesystem::path& directory)
		{
			std::vector<std::filesystem::path> files;
			for (const auto& entry : std::filesystem::directory_iterator(directory))
			{
				if (entry.is_regular_file())
					files.push_back(entry.path());
			}
			// Sorted, so a seed replays the same mutations everywhere
			std::ranges::sort(files);

			std::vector<std::string> corpus;
			for (const std::filesystem::path& file : files)
			{
				auto text = ReadFile(file);
				REQUIRE_MESSAGE(text.has_value(), Testing::DescribeError(text));
				corpus.push_back(std::move(*text));
			}
			return corpus;
		}

		size_t Pick(std::mt19937_64& random, size_t count)
		{
			return std::uniform_int_distribution<size_t>(0, count - 1)(random);
		}

		std::string Mutate(std::string input, const std::vector<std::string>& corpus,
			const std::vector<std::string>& dictionary, std::mt19937_64& random)
		{
			// Several mutations at once, so inputs drift further from the corpus
			const size_t mutations = 1 + Pick(random, 4);
			for (size_t i = 0; i < mutations; ++i)
			{
				const size_t position = input.empty() ? 0 : Pick(random, input.size() + 1);
				switch (Pick(random, 8))
				{
					case 0: // Flip a bit
						if (!input.empty())
						{
							const size_t index = Pick(random, input.size());
							input[index] = static_cast<char>(input[index] ^ (1 << Pick(random, 8)));
						}
						break;
					case 1: // Replace a byte with an interesting one
						if (!input.empty())
							input[Pick(random, input.size())] = InterestingBytes[Pick(random, InterestingBytes.size())];
						break;
					case 2: // Insert an interesting byte
						input.insert(input.begin() + static_cast<std::ptrdiff_t>(position),
							InterestingBytes[Pick(random, InterestingBytes.size())]);
						break;
					case 3: // Delete a range
						if (!input.empty())
						{
							const size_t start = Pick(random, input.size());
							input.erase(start, 1 + Pick(random, std::min<size_t>(input.size() - start, 16)));
						}
						break;
					case 4: // Duplicate a range
						if (!input.empty())
						{
							const size_t start = Pick(random, input.size());
							const std::string range = input.substr(start, 1 + Pick(random, 64));
							input.insert(position, range);
						}
						break;
					case 5: // Splice in part of another input
					{
						const std::string& other = corpus[Pick(random, corpus.size())];
						if (!other.empty())
						{
							const size_t start = Pick(random, other.size());
							input.insert(position, other.substr(start, 1 + Pick(random, 256)));
						}
						break;
					}
					case 6: // Insert a dictionary string
						if (!dictionary.empty())
							input.insert(position, dictionary[Pick(random, dictionary.size())]);
						break;
					default: // Overwrite a range with a dictionary string
						if (!dictionary.empty() && !input.empty())
						{
							const std::string& word = dictionary[Pick(random, dictionary.size())];
							const size_t start = Pick(random, input.size());
							input.replace(start, std::min(word.size(), input.size() - start), word);
						}
						break;
				}
			}
			return input;
		}

	}

	FuzzOptions MakeFuzzOptions(std::string_view corpusName)
	{
		FuzzOptions options;
		options.CorpusDirectory = std::filesystem::path(LS_FUZZ_CORPUS_DIR) / corpusName;

		if (const auto seconds = ReadEnvironmentVariable("LS_FUZZ_SECONDS"))
		{
			uint32_t value = 0;
			const auto [end, error] = std::from_chars(seconds->data(), seconds->data() + seconds->size(), value);
			REQUIRE_MESSAGE((error == std::errc() && end == seconds->data() + seconds->size()),
				fmt::format("LS_FUZZ_SECONDS must be a whole number of seconds, not '{}'", *seconds));
			options.Budget = std::chrono::seconds(value);
		}

		if (const auto seed = ReadEnvironmentVariable("LS_FUZZ_SEED"))
		{
			const auto [end, error] = std::from_chars(seed->data(), seed->data() + seed->size(), options.Seed);
			REQUIRE_MESSAGE((error == std::errc() && end == seed->data() + seed->size()),
				fmt::format("LS_FUZZ_SEED must be a whole number, not '{}'", *seed));
		}
		else
		{
			options.Seed = std::random_device()() | (static_cast<uint64_t>(std::random_device()()) << 32);
		}
		return options;
	}

	FuzzReport RunFuzzer(const FuzzTarget& target, const FuzzOptions& options)
	{
		MESSAGE(fmt::format("Fuzzing with seed {} for {} ms (reproduce with LS_FUZZ_SEED={})", options.Seed,
			options.Budget.count(), options.Seed));

		const std::vector<std::string> corpus = LoadCorpus(options.CorpusDirectory);
		REQUIRE_MESSAGE(!corpus.empty(), fmt::format("The fuzz corpus {} is empty", options.CorpusDirectory));

		FuzzReport report;
		for (const std::string& input : corpus)
		{
			target(input);
			++report.Runs;
		}

		// Mutations of mutations reach further, so some mutated inputs join the pool, up to a limit
		constexpr size_t maxPoolSize = 1024;
		std::vector<std::string> pool = corpus;
		std::mt19937_64 random(options.Seed);
		const auto deadline = std::chrono::steady_clock::now() + options.Budget;
		while (std::chrono::steady_clock::now() < deadline)
		{
			std::string input = Mutate(pool[Pick(random, pool.size())], corpus, options.Dictionary, random);
			if (input.size() > options.MaxInputSize)
				input.resize(options.MaxInputSize);
			target(input);
			++report.Runs;
			if (pool.size() < maxPoolSize && Pick(random, 8) == 0)
				pool.push_back(std::move(input));
		}

		MESSAGE(fmt::format("Ran {} inputs", report.Runs));
		return report;
	}

}
