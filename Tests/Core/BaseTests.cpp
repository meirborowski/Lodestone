#include "Lodestone/Core/Base.h"

#include <doctest/doctest.h>

#include <string>
#include <utility>

namespace Lodestone {

	namespace {

		struct Widget
		{
			Widget(std::string name, int size)
				: Name(std::move(name)), Size(size)
			{
			}

			std::string Name;
			int Size = 0;
		};

	}

	TEST_CASE("CreateScope constructs a uniquely owned object from its arguments")
	{
		const Scope<Widget> widget = CreateScope<Widget>("Crate", 3);

		REQUIRE(widget != nullptr);
		CHECK(widget->Name == "Crate");
		CHECK(widget->Size == 3);
	}

	TEST_CASE("CreateRef constructs a shared object from its arguments")
	{
		const Ref<Widget> widget = CreateRef<Widget>("Barrel", 5);
		REQUIRE(widget != nullptr);
		CHECK(widget->Name == "Barrel");
		CHECK(widget.use_count() == 1);

		Ref<Widget> sharedWidget = widget;
		CHECK(sharedWidget.get() == widget.get());
		CHECK(widget.use_count() == 2);

		sharedWidget.reset();
		CHECK(widget.use_count() == 1);
	}

	TEST_CASE("Exactly one platform is detected")
	{
		int platforms = 0;
#if defined(LS_PLATFORM_WINDOWS)
		++platforms;
#endif
#if defined(LS_PLATFORM_MACOS)
		++platforms;
#endif
#if defined(LS_PLATFORM_LINUX)
		++platforms;
#endif
		CHECK(platforms == 1);
	}

	TEST_CASE("Release and Dist build with the optimized flags, and Debug without them")
	{
		// NDEBUG comes from the optimized configurations' compiler flags, so it shows they were applied
#if defined(LS_CONFIG_DEBUG)
		constexpr bool isOptimizedBuild = false;
#else
		constexpr bool isOptimizedBuild = true;
#endif
#if defined(NDEBUG)
		constexpr bool hasNDebug = true;
#else
		constexpr bool hasNDebug = false;
#endif
		CHECK(hasNDebug == isOptimizedBuild);
	}

	TEST_CASE("Asserts and developer logging are enabled in every configuration except Dist")
	{
#if defined(LS_CONFIG_DIST)
		CHECK(LS_ENABLE_ASSERTS == 0);
		CHECK(LS_ENABLE_DEVELOPER_LOGGING == 0);
#else
		CHECK(LS_ENABLE_ASSERTS == 1);
		CHECK(LS_ENABLE_DEVELOPER_LOGGING == 1);
#endif
	}

}
