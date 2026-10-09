#include "Lodestone/Editor/EditorCamera.h"
#include "Lodestone/Editor/LogBuffer.h"

#include <doctest/doctest.h>
#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <string>
#include <thread>
#include <vector>

namespace Lodestone {

	namespace {

		bool IsNear(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
		{
			return glm::length(a - b) <= tolerance;
		}

	}

	TEST_CASE("The log buffer keeps messages in order, with sequence numbers, levels and loggers")
	{
		LogBuffer log;
		log.Attach();
		const uint64_t start = log.GetLatestSequence();

		LS_CORE_INFO("First");
		LS_WARN("Second {}", 2);

		const std::vector<LogEntry> entries = log.GetEntries(start);
		REQUIRE(entries.size() == 2);
		CHECK(entries[0].Message == "First");
		CHECK(entries[0].Level == LogLevel::Info);
		CHECK(entries[0].Logger == "Engine");
		CHECK(entries[1].Message == "Second 2");
		CHECK(entries[1].Level == LogLevel::Warn);
		CHECK(entries[1].Logger == "App");
		CHECK(entries[1].Sequence == entries[0].Sequence + 1);
		CHECK(log.GetLatestSequence() == entries[1].Sequence);
		// UTC, ISO 8601, to the millisecond: 2026-10-09T12:34:56.789Z
		CHECK(entries[0].Time.size() == 24);
		CHECK(entries[0].Time[10] == 'T');
		CHECK(entries[0].Time.back() == 'Z');
	}

	TEST_CASE("The log buffer returns what came after a sequence number, filtered by level, the latest first kept")
	{
		LogBuffer log;
		log.Attach();
		const uint64_t start = log.GetLatestSequence();
		for (int index = 0; index < 5; ++index)
			LS_CORE_INFO("Info {}", index);
		LS_CORE_ERROR("Error");

		CHECK(log.GetEntries(start, LogLevel::Error).size() == 1);
		const std::vector<LogEntry> latest = log.GetEntries(start, LogLevel::Trace, 2);
		REQUIRE(latest.size() == 2);
		CHECK(latest[0].Message == "Info 4");
		CHECK(latest[1].Message == "Error");
		CHECK(log.GetEntries(log.GetLatestSequence()).empty());
	}

	TEST_CASE("The log buffer forgets the oldest messages beyond its capacity, and can be cleared")
	{
		LogBuffer log(3);
		log.Attach();
		for (int index = 0; index < 5; ++index)
			LS_CORE_INFO("Message {}", index);

		const std::vector<LogEntry> entries = log.GetEntries();
		REQUIRE(entries.size() == 3);
		CHECK(entries[0].Message == "Message 2");

		const uint64_t latest = log.GetLatestSequence();
		log.Clear();
		CHECK(log.GetEntries().empty());
		// Sequence numbers keep counting
		CHECK(log.GetLatestSequence() == latest);
	}

	TEST_CASE("A log buffer that isn't attached receives nothing, and detaches when destroyed")
	{
		const LogBuffer detached;
		LS_CORE_INFO("Not for the detached buffer");
		CHECK(detached.GetEntries().empty());

		{
			LogBuffer attached;
			attached.Attach();
			attached.Attach();
			LS_CORE_INFO("Once");
			CHECK(attached.GetEntries().size() == 1);
		}
		// Logging after the buffer is gone must be safe
		LS_CORE_INFO("After");
	}

	TEST_CASE("The log buffer takes messages from many threads")
	{
		LogBuffer log(10000);
		log.Attach();
		const uint64_t start = log.GetLatestSequence();
		std::vector<std::thread> threads;
		threads.reserve(4);
		for (int thread = 0; thread < 4; ++thread)
		{
			threads.emplace_back(
				[thread]
				{
					for (int index = 0; index < 100; ++index)
						LS_CORE_TRACE("Thread {} message {}", thread, index);
				});
		}
		for (std::thread& thread : threads)
			thread.join();

		const std::vector<LogEntry> entries = log.GetEntries(start);
		REQUIRE(entries.size() == 400);
		for (size_t index = 1; index < entries.size(); ++index)
			CHECK(entries[index].Sequence == entries[index - 1].Sequence + 1);
	}

	TEST_CASE("The editor camera looks at a target from a position")
	{
		EditorCamera camera;

		camera.LookAt(glm::vec3(0.0f, 5.0f, 5.0f), glm::vec3(0.0f));

		CHECK(IsNear(camera.GetPosition(), glm::vec3(0.0f, 5.0f, 5.0f)));
		CHECK(IsNear(camera.GetTarget(), glm::vec3(0.0f)));
		CHECK(camera.GetDistance() == doctest::Approx(std::sqrt(50.0f)));
		CHECK(camera.GetPitch() == doctest::Approx(glm::pi<float>() / 4.0f));
		CHECK(camera.GetYaw() == doctest::Approx(0.0f));

		// The view puts the target straight ahead, in the middle of the image
		const glm::vec4 target = camera.GetProjection(1.0f) * camera.GetView() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		CHECK(target.x / target.w == doctest::Approx(0.0f).epsilon(1e-5));
		CHECK(target.y / target.w == doctest::Approx(0.0f).epsilon(1e-5));
		CHECK(target.z / target.w > 0.0f);
		CHECK(target.z / target.w < 1.0f);
	}

	TEST_CASE("The editor camera's projection puts up at the top and depth from 0 to 1")
	{
		EditorCamera camera;
		camera.LookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f));
		const glm::mat4 viewProjection = camera.GetProjection(16.0f / 9.0f) * camera.GetView();

		const glm::vec4 above = viewProjection * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
		const glm::vec4 right = viewProjection * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
		const glm::vec4 nearer = viewProjection * glm::vec4(0.0f, 0.0f, 5.0f, 1.0f);
		const glm::vec4 farther = viewProjection * glm::vec4(0.0f, 0.0f, -5.0f, 1.0f);

		CHECK(above.y / above.w > 0.0f);
		CHECK(right.x / right.w > 0.0f);
		CHECK(nearer.z / nearer.w < farther.z / farther.w);
		CHECK(nearer.z / nearer.w >= 0.0f);
		CHECK(farther.z / farther.w <= 1.0f);
	}

	TEST_CASE("The editor camera orbits its target, never quite straight up or down")
	{
		EditorCamera camera;
		camera.LookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(1.0f, 2.0f, 3.0f));
		const float distance = camera.GetDistance();

		camera.Orbit(glm::pi<float>() / 2.0f, 0.0f);
		CHECK(camera.GetDistance() == doctest::Approx(distance));
		CHECK(IsNear(camera.GetTarget(), glm::vec3(1.0f, 2.0f, 3.0f)));

		camera.Orbit(0.0f, 10.0f);
		CHECK(camera.GetPitch() < glm::pi<float>() / 2.0f);
		CHECK(camera.GetPitch() > 1.5f);
		camera.Orbit(0.0f, -20.0f);
		CHECK(camera.GetPitch() > -glm::pi<float>() / 2.0f);

		// Yaw wraps around
		camera.Orbit(100.0f * glm::pi<float>(), 0.0f);
		CHECK(std::abs(camera.GetYaw()) <= glm::pi<float>() + 1e-4f);
	}

	TEST_CASE("The editor camera pans in its view plane and zooms within limits")
	{
		EditorCamera camera;
		camera.LookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f));

		camera.Pan(2.0f, 1.0f);
		CHECK(IsNear(camera.GetTarget(), glm::vec3(2.0f, 1.0f, 0.0f)));
		CHECK(IsNear(camera.GetPosition(), glm::vec3(2.0f, 1.0f, 10.0f)));

		camera.Zoom(0.5f);
		CHECK(camera.GetDistance() == doctest::Approx(5.0f));
		camera.Zoom(1e-9f);
		CHECK(camera.GetDistance() == EditorCamera::MinDistance);
		camera.Zoom(1e12f);
		CHECK(camera.GetDistance() == EditorCamera::MaxDistance);
		// Nonsense is ignored
		camera.Zoom(0.0f);
		camera.Zoom(-1.0f);
		camera.Zoom(std::nanf(""));
		CHECK(camera.GetDistance() == EditorCamera::MaxDistance);
	}

	TEST_CASE("The editor camera ignores a position at its target")
	{
		EditorCamera camera;
		const glm::vec3 position = camera.GetPosition();

		camera.LookAt(glm::vec3(1.0f), glm::vec3(1.0f));

		CHECK(IsNear(camera.GetPosition(), position));
	}

}
