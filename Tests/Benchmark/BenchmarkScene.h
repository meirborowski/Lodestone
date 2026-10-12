#pragma once

#include "Lodestone/Renderer/RenderAssets.h"
#include "Lodestone/Renderer/RenderWorld.h"
#include "Lodestone/Scene/Scene.h"

#include <filesystem>

namespace Lodestone::Benchmark {

	// The benchmark scene: a small city on a ground plane - blocks of buildings, rows of spheres in every material,
	// textured crates, alpha-tested and transparent fences - under a sun that casts shadows, lit by the sky and by
	// dozens of point and spot lights, with ambient occlusion. Every renderer feature is on. It's built the same every
	// time, from the test fixtures' models and environment
	void BuildScene(Scene& scene);

	// Loads the assets the scene refers to from the test fixtures in a directory (Tests/Fixtures)
	RenderAssetSource MakeAssetSource(const std::filesystem::path& fixtureDirectory);

	// Where the camera is at a point of the benchmark's flight, from 0 to 1: once around the city, rising and falling
	RenderView GetCameraView(float time);

}
