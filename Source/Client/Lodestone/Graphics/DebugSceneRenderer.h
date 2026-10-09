#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <nvrhi/nvrhi.h>

#include <expected>
#include <span>

namespace Lodestone {

	struct DebugCube
	{
		// The unit cube's transform: an entity's world matrix
		glm::mat4 World{1.0f};
		glm::vec4 Color{1.0f};
	};

	struct DebugView
	{
		glm::mat4 ViewProjection{1.0f};
		glm::vec4 ClearColor{0.12f, 0.12f, 0.14f, 1.0f};
		bool ShowGrid = true;
	};

	// The editor's view of a scene until the scene renderer arrives in Milestone 5: a ground grid, and a shaded cube
	// for each entity. It draws into framebuffers like RenderTarget's (8-bit RGBA color, 32-bit depth)
	class DebugSceneRenderer
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		[[nodiscard]] static std::expected<Scope<DebugSceneRenderer>, Error> Create(nvrhi::IDevice* device);

		explicit DebugSceneRenderer(Passkey passkey);

		// Records the clear, the grid and the cubes into an open command list
		void Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer, const DebugView& view,
			std::span<const DebugCube> cubes) const;

	private:
		nvrhi::ShaderHandle m_CubeVertexShader;
		nvrhi::ShaderHandle m_CubePixelShader;
		nvrhi::ShaderHandle m_GridVertexShader;
		nvrhi::ShaderHandle m_GridPixelShader;
		nvrhi::BindingLayoutHandle m_CubeLayout;
		nvrhi::BindingLayoutHandle m_GridLayout;
		nvrhi::BindingSetHandle m_CubeBindings;
		nvrhi::BindingSetHandle m_GridBindings;
		nvrhi::GraphicsPipelineHandle m_CubePipeline;
		nvrhi::GraphicsPipelineHandle m_GridPipeline;
	};

}
