// The sample games, played in the editor's play mode from start to finish: Milestone 8's acceptance test (see
// docs/Milestones.md). Each plays a copy of its project, so the samples in the repository stay as they are

#include "Common/DescribeError.h"
#include "Common/TemporaryDirectory.h"
#include "Lodestone/Editor/EditorContext.h"
#include "Lodestone/Scene/Entity.h"

#include <doctest/doctest.h>
#include <glm/vec3.hpp>
#include <spdlog/fmt/fmt.h>

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Lodestone {

	namespace {

		// The editor with a copy of a sample project open, and one of its scenes
		struct SampleFixture
		{
			SampleFixture(std::string_view sample, std::string_view scene)
			{
				const std::filesystem::path project = Directory.GetPath() / sample;
				std::filesystem::copy(
					std::filesystem::path(LS_SAMPLES_DIR) / sample, project, std::filesystem::copy_options::recursive);
				const auto opened = Context.OpenProject(project);
				REQUIRE_MESSAGE(opened.has_value(), Testing::DescribeError(opened));
				const auto loaded = Context.OpenScene(scene);
				REQUIRE_MESSAGE(loaded.has_value(), Testing::DescribeError(loaded));
			}

			// The entity with a name, which must be the only one
			UUID Find(std::string_view name)
			{
				std::optional<UUID> found;
				for (const auto [handle, id, entityName] :
					Context.GetScene().GetRegistry().view<const IDComponent, const NameComponent>().each())
				{
					if (entityName.Name != name)
						continue;
					REQUIRE_MESSAGE(!found.has_value(), "More than one entity is called ", name);
					found = id.ID;
				}
				REQUIRE_MESSAGE(found.has_value(), "No entity is called ", name);
				return found.value_or(UUID());
			}

			Entity Get(UUID id) { return Context.GetScene().FindEntity(id); }

			void Step(const InputCommand& input = {})
			{
				InputCommand command = input;
				command.Tick = Context.GetSimulation()->GetTick();
				Context.QueueInput({&command, 1});
				const auto stepped = Context.Step(1);
				REQUIRE_MESSAGE(stepped.has_value(), Testing::DescribeError(stepped));
			}

			Testing::TemporaryDirectory Directory{"SampleGame"};
			EditorContext Context;
		};

		InputCommand Holding(std::initializer_list<Key> keys)
		{
			InputCommand command;
			for (const Key key : keys)
				command.KeysDown.set(std::to_underlying(key));
			return command;
		}

	}

	TEST_CASE("Pong plays from start to finish, and starts again")
	{
		SampleFixture pong("Pong", "Scenes/Pong.lscene");
		const UUID ball = pong.Find("Ball");
		const UUID paddle = pong.Find("Left Paddle");
		const UUID status = pong.Find("Status");
		const UUID leftScore = pong.Find("Left Score");
		const UUID rightScore = pong.Find("Right Score");

		REQUIRE(pong.Context.StartPlay().has_value());
		REQUIRE(pong.Context.SetPaused(true).has_value());
		pong.Step();
		CHECK(pong.Get(status).GetName() == "Serving 0 - 0");

		// The player follows the ball with W and S, meaning to hit it off center, so it goes back at an angle the
		// computer can miss
		// Ten minutes of play at most
		constexpr uint64_t maxTicks = uint64_t{60} * 60 * 10;
		while (pong.Context.GetSimulation()->GetTick() < maxTicks && !pong.Get(status).GetName().contains(" wins "))
		{
			const glm::vec3 ballPosition = pong.Get(ball).GetTransform().Position;
			const float aim = ballPosition.z > 0.0f ? 0.7f : -0.7f;
			const float difference = ballPosition.z - aim - pong.Get(paddle).GetTransform().Position.z;
			if (difference > 0.2f)
				pong.Step(Holding({Key::S}));
			else if (difference < -0.2f)
				pong.Step(Holding({Key::W}));
			else
				pong.Step();
		}

		// A match: one side won, its score shown by its markers
		const std::string result = pong.Get(status).GetName();
		INFO("The match ended ", result, " after ", pong.Context.GetSimulation()->GetTick(), " ticks");
		REQUIRE(result.contains(" wins "));
		const size_t leftPoints = pong.Get(leftScore).Get<HierarchyComponent>().Children.size();
		const size_t rightPoints = pong.Get(rightScore).Get<HierarchyComponent>().Children.size();
		CHECK(result ==
			fmt::format(
				"{} wins {} - {} - Space to play again", leftPoints == 5 ? "Left" : "Right", leftPoints, rightPoints));
		CHECK((leftPoints == 5) != (rightPoints == 5));
		CHECK(pong.Get(ball).GetTransform().Position == glm::vec3(0.0f, 0.5f, 0.0f));
		REQUIRE(pong.Context.GetScripts() != nullptr);
		CHECK(pong.Context.GetScripts()->GetErrorCount() == 0);
		CHECK(pong.Context.GetScripts()->GetInstanceCount() == 6);

		// Space starts a new match, served after a second
		InputCommand space;
		space.KeysDown.set(std::to_underlying(Key::Space));
		space.KeysPressed.set(std::to_underlying(Key::Space));
		pong.Step(space);
		CHECK(pong.Get(status).GetName() == "Serving 0 - 0");
		CHECK(pong.Get(leftScore).Get<HierarchyComponent>().Children.empty());
		CHECK(pong.Get(rightScore).Get<HierarchyComponent>().Children.empty());
		for (int tick = 0; tick < 61; ++tick)
			pong.Step();
		CHECK(pong.Get(status).GetName() == "Playing 0 - 0");
		CHECK(pong.Get(ball).GetTransform().Position.x < 0.0f);

		REQUIRE(pong.Context.StopPlay().has_value());
		CHECK(pong.Get(status).GetName() == "Status");
	}

}
