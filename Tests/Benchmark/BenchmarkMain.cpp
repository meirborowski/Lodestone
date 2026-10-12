#include "Benchmark/BenchmarkScene.h"
#include "Lodestone/Core/CommandLine.h"
#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Core/RunMain.h"
#include "Lodestone/Graphics/GraphicsDevice.h"
#include "Lodestone/Renderer/SceneRenderer.h"
#include "Lodestone/Serialization/Json.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// The renderer's performance benchmark (see docs/Testing.md#performance): renders the benchmark scene along a fixed
// camera flight and reports CPU and GPU frame times, draw calls and triangles.
//
//   LodestoneBenchmark [--frames 300] [--size 1920x1080] [--vulkan-driver <library>] [--fixtures <directory>]
//                      [--baseline <file>] [--record <file>] [--min-fps <fps>] [--output <file>]
//
// The first frames are a fixed probe of the flight, which loads the assets and counts draw calls - they don't depend on
// the GPU, so CI checks them against the baseline (--baseline fails when they rise). Timings are reported, and compared
// with the baseline's when it was recorded on the same GPU. --record writes a new baseline; --min-fps fails when the
// frame rate is lower. The report is printed as JSON, and with --output also written to a file

namespace Lodestone::Benchmark {

	namespace {

		constexpr std::array<std::string_view, 0> Flags = {};
		constexpr std::array<std::string_view, 8> Options = {
			"--frames", "--size", "--vulkan-driver", "--fixtures", "--baseline", "--record", "--min-fps", "--output"};
		constexpr uint32_t DefaultFrames = 300;
		constexpr glm::uvec2 DefaultSize{1920, 1080};
		// Camera positions spread over the flight, rendered first to load assets and count draw calls
		constexpr uint32_t ProbeFrames = 16;
		// Slower than the baseline by more than this, on the same GPU, is worth a warning
		constexpr double RegressionThreshold = 0.1;
		constexpr std::string_view BaselineFormat = "Lodestone.BenchmarkBaseline";

		struct Summary
		{
			double Mean = 0.0;
			double P95 = 0.0;
		};

		Summary Summarize(std::vector<double> values)
		{
			if (values.empty())
				return {};
			std::ranges::sort(values);
			const auto p95 = static_cast<size_t>(std::ceil(0.95 * static_cast<double>(values.size()))) - 1;
			return {.Mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size()),
				.P95 = values[std::min(p95, values.size() - 1)]};
		}

		Json::Value ToJson(const Summary& summary)
		{
			return {{"mean", summary.Mean}, {"p95", summary.P95}};
		}

		std::optional<uint32_t> ParseDimension(std::string_view text)
		{
			uint32_t value = 0;
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
			if (error != std::errc() || end != text.data() + text.size() || value == 0 || value > 16384)
				return std::nullopt;
			return value;
		}

		std::expected<glm::uvec2, Error> ParseSize(std::string_view text)
		{
			const size_t separator = text.find('x');
			if (separator != std::string_view::npos)
			{
				const auto width = ParseDimension(text.substr(0, separator));
				const auto height = ParseDimension(text.substr(separator + 1));
				if (width && height)
					return glm::uvec2(*width, *height);
			}
			return std::unexpected(Error(ErrorCode::InvalidArgument,
				fmt::format("--size must be <width>x<height>, such as 1920x1080, not '{}'", text)));
		}

		class Benchmark
		{
		public:
			Benchmark(GraphicsDevice& device, SceneRenderer& renderer, SceneRenderTarget& target)
				: m_Device(device),
				  m_Renderer(renderer),
				  m_Target(target),
				  m_CommandList(device.GetNvrhiDevice()->createCommandList())
			{
				BuildScene(m_Scene);
			}

			// Renders a frame from a point of the flight, waits for the GPU to finish it, and returns its stats, with
			// the CPU time of extracting the scene added
			RenderStats RenderFrame(float time, double& wallMilliseconds)
			{
				nvrhi::IDevice* device = m_Device.GetNvrhiDevice();
				const auto start = std::chrono::steady_clock::now();
				const RenderWorld world = ExtractRenderWorld(m_Scene);
				const double extraction =
					std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				m_CommandList->open();
				m_Renderer.Render(m_CommandList, m_Target, world, GetCameraView(time));
				m_CommandList->close();
				device->executeCommandList(m_CommandList);
				device->waitForIdle();
				device->runGarbageCollection();
				wallMilliseconds =
					std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				RenderStats stats = m_Target.GetStats();
				stats.CpuTime += extraction;
				return stats;
			}

		private:
			GraphicsDevice& m_Device;
			SceneRenderer& m_Renderer;
			SceneRenderTarget& m_Target;
			nvrhi::CommandListHandle m_CommandList;
			Scene m_Scene;
		};

		Json::Value Measure(GraphicsDevice& device, Benchmark& benchmark, glm::uvec2 size, uint32_t frames)
		{
			// The probe: draw calls and triangles at fixed points of the flight, whatever the frame count
			uint64_t drawCalls = 0;
			uint64_t triangles = 0;
			for (uint32_t frame = 0; frame < ProbeFrames; ++frame)
			{
				double wall = 0.0;
				const RenderStats stats =
					benchmark.RenderFrame(static_cast<float>(frame) / static_cast<float>(ProbeFrames), wall);
				drawCalls += stats.DrawCalls;
				triangles += stats.Triangles;
			}

			std::vector<double> cpu;
			std::vector<double> gpu;
			std::vector<double> wall;
			std::array<std::vector<double>, RenderPassCount> passes;
			for (uint32_t frame = 0; frame < frames; ++frame)
			{
				double milliseconds = 0.0;
				const RenderStats stats =
					benchmark.RenderFrame(static_cast<float>(frame) / static_cast<float>(frames), milliseconds);
				cpu.push_back(stats.CpuTime);
				wall.push_back(milliseconds);
				// The GPU's timings are of the frame before, which finished before this one began
				if (const std::optional<double> gpuTime = stats.GetGpuTime();
					frame > 0 && gpuTime && stats.GpuPassTimes)
				{
					gpu.push_back(*gpuTime);
					for (size_t pass = 0; pass < RenderPassCount; ++pass)
						passes[pass].push_back((*stats.GpuPassTimes)[pass]);
				}
			}

			const GraphicsDeviceInfo& info = device.GetInfo();
			Json::Value report = {{"device", info.Name},
				{"driver", fmt::format("{} {}", info.DriverName, info.DriverInfo)}, {"size", {size.x, size.y}},
				{"frames", frames}, {"probeFrames", ProbeFrames}, {"drawCalls", drawCalls}, {"triangles", triangles}};
			report["cpuMilliseconds"] = ToJson(Summarize(cpu));
			report["wallMilliseconds"] = ToJson(Summarize(wall));
			const Summary cpuSummary = Summarize(cpu);
			if (!gpu.empty())
			{
				const Summary gpuSummary = Summarize(gpu);
				report["gpuMilliseconds"] = ToJson(gpuSummary);
				Json::Value passTimes = Json::Value::object();
				for (size_t pass = 0; pass < RenderPassCount; ++pass)
					passTimes[std::string(ToString(static_cast<RenderPass>(pass)))] = Summarize(passes[pass]).Mean;
				report["gpuPassMilliseconds"] = std::move(passTimes);
				// The CPU records a frame while the GPU renders the one before, so the slower of the two sets the pace
				report["fps"] = 1000.0 / std::max({cpuSummary.Mean, gpuSummary.Mean, 1e-6});
			}
			else
			{
				report["gpuMilliseconds"] = Json::Value();
				report["fps"] = 1000.0 / std::max(Summarize(wall).Mean, 1e-6);
			}
			return report;
		}

		std::expected<Json::Value, Error> LoadBaseline(const std::filesystem::path& path)
		{
			const auto text = ReadFile(path);
			if (!text)
				return std::unexpected(text.error());
			auto document = Json::Parse(*text);
			if (!document)
				return std::unexpected(document.error().WithContext(fmt::format("Reading {}", PathToUtf8(path))));
			if (!document->is_object() || document->value("format", std::string()) != BaselineFormat ||
				!(*document)["drawCalls"].is_number_unsigned())
				return std::unexpected(Error(ErrorCode::ParseError,
					fmt::format("{} isn't a benchmark baseline with a draw call count", PathToUtf8(path))));
			return std::move(*document);
		}

		// Checks the draw calls against the baseline, failing if they rose, and compares timings measured on the same
		// GPU
		bool CompareWithBaseline(const Json::Value& report, const Json::Value& baseline)
		{
			const auto drawCalls = report["drawCalls"].get<uint64_t>();
			const auto baselineDrawCalls = baseline["drawCalls"].get<uint64_t>();
			bool passed = true;
			if (drawCalls > baselineDrawCalls)
			{
				LS_ERROR("Draw calls rose from {} to {}: find what added them, or record a new baseline if it's meant",
					baselineDrawCalls, drawCalls);
				passed = false;
			}
			else if (drawCalls < baselineDrawCalls)
			{
				LS_INFO("Draw calls fell from {} to {}: record a new baseline to keep them there", baselineDrawCalls,
					drawCalls);
			}
			else
			{
				LS_INFO("Draw calls match the baseline: {}", drawCalls);
			}

			const Json::Value local = baseline.value("local", Json::Value());
			if (!local.is_object() || local.value("device", "") != report["device"].get<std::string>() ||
				!(local.value("size", Json::Value()) == report["size"]))
			{
				LS_INFO("The baseline's timings are from {} at another size or on another GPU, so they aren't compared",
					local.is_object() ? local.value("device", "an unknown GPU") : "nowhere");
				return passed;
			}
			for (const std::string key : {"gpuMilliseconds", "cpuMilliseconds"})
			{
				if (!report[key].is_object() || !local.value(key, Json::Value()).is_object())
					continue;
				const double now = report[key].value("mean", 0.0);
				const double before = local[key].value("mean", 0.0);
				const double change = before > 0.0 ? (now - before) / before : 0.0;
				if (change > RegressionThreshold)
					LS_WARN("{} {:.2f} ms is {:.0f}% slower than the baseline's {:.2f} ms", key, now, change * 100.0,
						before);
				else
					LS_INFO("{} {:.2f} ms against the baseline's {:.2f} ms", key, now, before);
			}
			return passed;
		}

		std::expected<int, Error> Run(const CommandLine& commandLine)
		{
			if (auto valid = commandLine.Validate(Flags, Options); !valid)
				return std::unexpected(valid.error());
			const auto frames = commandLine.GetUnsignedValue("--frames");
			if (!frames)
				return std::unexpected(frames.error());
			const auto minimumFps = commandLine.GetUnsignedValue("--min-fps");
			if (!minimumFps)
				return std::unexpected(minimumFps.error());
			glm::uvec2 size = DefaultSize;
			if (const auto text = commandLine.GetValue("--size"))
			{
				const auto parsed = ParseSize(*text);
				if (!parsed)
					return std::unexpected(parsed.error());
				size = *parsed;
			}
			const std::filesystem::path fixtures =
				PathFromUtf8(commandLine.GetValue("--fixtures").value_or(LS_TEST_FIXTURE_DIR));

			std::optional<Json::Value> baseline;
			if (const auto path = commandLine.GetValue("--baseline"))
			{
				auto loaded = LoadBaseline(PathFromUtf8(*path));
				if (!loaded)
					return std::unexpected(loaded.error());
				baseline = std::move(*loaded);
			}

			GraphicsDeviceConfig config;
			if (const auto driver = commandLine.GetValue("--vulkan-driver"))
				config.Driver = PathFromUtf8(*driver);
			auto device = GraphicsDevice::Create(config);
			if (!device)
				return std::unexpected(device.error());
			auto renderer = SceneRenderer::Create(**device);
			if (!renderer)
				return std::unexpected(renderer.error());
			(*renderer)->GetAssets().SetSource(MakeAssetSource(fixtures));
			auto target = (*renderer)->CreateTarget(size, "Benchmark");
			if (!target)
				return std::unexpected(target.error());

			LS_INFO("Benchmarking {} frames at {}x{} on {}", frames->value_or(DefaultFrames), size.x, size.y,
				(*device)->GetInfo().Name);
			Benchmark benchmark(**device, **renderer, **target);
			const Json::Value report = Measure(**device, benchmark, size, frames->value_or(DefaultFrames));
			fmt::print("{}\n", report.dump(2));
			if (const auto path = commandLine.GetValue("--output"))
			{
				if (auto written = WriteFileAtomically(PathFromUtf8(*path), report.dump(2) + "\n"); !written)
					return std::unexpected(written.error());
			}

			int exitCode = EXIT_SUCCESS;
			if (baseline && !CompareWithBaseline(report, *baseline))
				exitCode = EXIT_FAILURE;
			if (*minimumFps && report["fps"].get<double>() < static_cast<double>(**minimumFps))
			{
				LS_ERROR("{:.1f} fps is below the {} required", report["fps"].get<double>(), **minimumFps);
				exitCode = EXIT_FAILURE;
			}
			if (const auto path = commandLine.GetValue("--record"))
			{
				const Json::Value recorded = {{"format", BaselineFormat}, {"version", 1},
					{"drawCalls", report["drawCalls"]}, {"triangles", report["triangles"]}, {"local", report}};
				if (auto written = WriteFileAtomically(PathFromUtf8(*path), recorded.dump(2) + "\n"); !written)
					return std::unexpected(written.error());
				LS_INFO("Recorded the baseline in {}", *path);
			}
			if (const uint32_t errors = (*device)->GetValidationErrorCount(); errors > 0)
			{
				LS_ERROR("The validation layers reported {} errors", errors);
				exitCode = EXIT_FAILURE;
			}
			return exitCode;
		}

	}

}

int main(int argc, char** argv)
{
	return Lodestone::RunMain("Lodestone Benchmark", {},
		[argc, argv]
		{
			const auto result = Lodestone::Benchmark::Run(Lodestone::CommandLine(argc, argv));
			if (!result)
			{
				LS_CRITICAL("{}", result.error());
				return EXIT_FAILURE;
			}
			return *result;
		});
}
