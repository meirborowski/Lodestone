#include "Lodestone/Renderer/RenderStats.h"

#include <numeric>

namespace Lodestone {

	std::string_view ToString(RenderPass pass)
	{
		switch (pass)
		{
			case RenderPass::Shadows:
				return "Shadows";
			case RenderPass::Prepass:
				return "Prepass";
			case RenderPass::AmbientOcclusion:
				return "Ambient occlusion";
			case RenderPass::Opaque:
				return "Opaque";
			case RenderPass::Sky:
				return "Sky";
			case RenderPass::Transparent:
				return "Transparent";
			case RenderPass::Tonemap:
				return "Tonemap";
		}
		return "Unknown";
	}

	std::optional<double> RenderStats::GetGpuTime() const
	{
		if (!GpuPassTimes)
			return std::nullopt;
		return std::accumulate(GpuPassTimes->begin(), GpuPassTimes->end(), 0.0);
	}

}
