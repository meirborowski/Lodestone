#pragma once

#include <nvrhi/nvrhi.h>

#include <cstddef>
#include <cstdint>

namespace Lodestone {

	// A shader from SPIR-V compiled at build time (see cmake/Shaders.cmake): one entry point of a generated header's
	// code. Returns null if the device rejects it
	inline nvrhi::ShaderHandle CreateShaderModule(nvrhi::IDevice* device, nvrhi::ShaderType type,
		const char* entryPoint, const uint8_t* code, size_t size, const char* debugName)
	{
		return device->createShader(
			nvrhi::ShaderDesc().setShaderType(type).setEntryName(entryPoint).setDebugName(debugName), code, size);
	}

}
