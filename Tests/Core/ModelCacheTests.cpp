#include "Lodestone/Asset/ModelCache.h"

#include "Common/DescribeError.h"
#include "Common/LogCapture.h"
#include "Common/TemporaryDirectory.h"
#include "Lodestone/Asset/AssetRegistry.h"
#include "Lodestone/Core/FileSystem.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string_view>

namespace Lodestone {

	namespace {

		// A project's assets with the skinned arm model and a scene in them
		struct Assets
		{
			Assets()
			{
				const std::filesystem::path models = Directory.GetPath() / "Models";
				std::filesystem::create_directories(models);
				std::filesystem::copy_file(
					std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Models" / "SkinnedArm.glb", models / "Arm.glb");
				REQUIRE(WriteFileAtomically(Directory.GetPath() / "Main.lscene", "{}").has_value());
				const auto scanned = Registry.Scan();
				REQUIRE_MESSAGE(scanned.has_value(), Testing::DescribeError(scanned));
			}

			UUID GetId(std::string_view path) const
			{
				const AssetInfo* info = Registry.FindByPath(path);
				REQUIRE(info != nullptr);
				return info->Metadata.Id;
			}

			Testing::TemporaryDirectory Directory{"ModelCache"};
			AssetRegistry Registry{Directory.GetPath()};
		};

		// Changes a file and moves its time on, so it reads as changed
		void Rewrite(const std::filesystem::path& path, const std::filesystem::path& from)
		{
			const auto before = std::filesystem::last_write_time(path);
			std::filesystem::copy_file(from, path, std::filesystem::copy_options::overwrite_existing);
			std::filesystem::last_write_time(path, before + std::chrono::seconds(2));
		}

	}

	TEST_CASE("The model cache imports a model asset once, without its textures")
	{
		const Assets assets;
		ModelCache cache(assets.Registry);
		const UUID arm = assets.GetId("Models/Arm.glb");

		const auto first = cache.Load(arm);
		const auto second = cache.Load(arm);

		REQUIRE_MESSAGE(first.has_value(), Testing::DescribeError(first));
		REQUIRE(second.has_value());
		CHECK(first.value_or(nullptr) == second.value_or(nullptr));
		CHECK(first.value_or(nullptr)->Animations.size() == 3);
		CHECK(first.value_or(nullptr)->Images.empty());
	}

	TEST_CASE("The model cache refuses what isn't a model, and remembers failures until it's refreshed")
	{
		const Testing::LogCapture log;
		const Assets assets;
		ModelCache cache(assets.Registry);

		const auto missing = cache.Load(UUID::Generate());
		REQUIRE_FALSE(missing.has_value());
		CHECK(missing.error().GetCode() == ErrorCode::NotFound);
		const auto scene = cache.Load(assets.GetId("Main.lscene"));
		REQUIRE_FALSE(scene.has_value());
		CHECK(scene.error().GetCode() == ErrorCode::InvalidArgument);

		// A broken model fails, and keeps failing until the cache is refreshed after it's fixed
		const UUID arm = assets.GetId("Models/Arm.glb");
		const std::filesystem::path path = assets.Directory.GetPath() / "Models" / "Arm.glb";
		Rewrite(path, assets.Directory.GetPath() / "Main.lscene");
		CHECK_FALSE(cache.Load(arm).has_value());
		Rewrite(path, std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Models" / "SkinnedArm.glb");
		CHECK_FALSE(cache.Load(arm).has_value());
		cache.Refresh();
		CHECK(cache.Load(arm).has_value());
	}

	TEST_CASE("Refreshing the model cache imports models again when their files change or go")
	{
		Assets assets;
		ModelCache cache(assets.Registry);
		const UUID arm = assets.GetId("Models/Arm.glb");
		const std::filesystem::path path = assets.Directory.GetPath() / "Models" / "Arm.glb";
		REQUIRE(cache.Load(arm).has_value());

		// Unchanged, it's kept
		cache.Refresh();
		CHECK(cache.Load(arm).has_value());

		// Changed to something that doesn't import, the next load imports it again - and fails
		const Testing::LogCapture log;
		Rewrite(path, assets.Directory.GetPath() / "Main.lscene");
		cache.Refresh();
		CHECK_FALSE(cache.Load(arm).has_value());

		// Gone from the project, it's gone from the cache
		std::filesystem::remove(path);
		REQUIRE(assets.Registry.Scan().has_value());
		cache.Refresh();
		const auto gone = cache.Load(arm);
		REQUIRE_FALSE(gone.has_value());
		CHECK(gone.error().GetCode() == ErrorCode::NotFound);
	}

}
