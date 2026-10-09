#include "Lodestone/Graphics/TextureReadback.h"

#include <nvrhi/utils.h>
#include <spdlog/fmt/fmt.h>

#include <cstring>
#include <utility>

namespace Lodestone {

	std::expected<Image, Error> ReadTexture(nvrhi::IDevice* device, nvrhi::ITexture* texture)
	{
		const nvrhi::TextureDesc& desc = texture->getDesc();
		bool swapRedAndBlue = false;
		switch (desc.format)
		{
			case nvrhi::Format::RGBA8_UNORM:
			case nvrhi::Format::SRGBA8_UNORM:
				break;
			case nvrhi::Format::BGRA8_UNORM:
			case nvrhi::Format::SBGRA8_UNORM:
				swapRedAndBlue = true;
				break;
			default:
				return std::unexpected(Error(ErrorCode::Unsupported,
					fmt::format(
						"Reading back {} textures isn't supported", nvrhi::utils::FormatToString(desc.format))));
		}

		nvrhi::TextureDesc stagingDesc;
		stagingDesc.width = desc.width;
		stagingDesc.height = desc.height;
		stagingDesc.format = desc.format;
		stagingDesc.debugName = "Texture readback";
		stagingDesc.isShaderResource = false;
		stagingDesc.initialState = nvrhi::ResourceStates::CopyDest;
		stagingDesc.keepInitialState = true;
		const nvrhi::StagingTextureHandle staging =
			device->createStagingTexture(stagingDesc, nvrhi::CpuAccessMode::Read);
		if (!staging)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating a texture readback buffer failed"));

		const nvrhi::CommandListHandle commandList = device->createCommandList();
		commandList->open();
		commandList->copyTexture(staging, nvrhi::TextureSlice(), texture, nvrhi::TextureSlice());
		commandList->close();
		device->executeCommandList(commandList);
		device->waitForIdle();

		size_t rowPitch = 0;
		const auto* mapped = static_cast<const uint8_t*>(
			device->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
		if (mapped == nullptr)
			return std::unexpected(Error(ErrorCode::DeviceError, "Mapping a texture readback buffer failed"));

		Image image(desc.width, desc.height);
		const size_t imageRowSize = static_cast<size_t>(desc.width) * 4;
		uint8_t* pixels = image.GetPixels().data();
		for (uint32_t y = 0; y < desc.height; ++y)
		{
			uint8_t* row = pixels + (y * imageRowSize);
			std::memcpy(row, mapped + (y * rowPitch), imageRowSize);
			if (swapRedAndBlue)
			{
				for (size_t x = 0; x < imageRowSize; x += 4)
					std::swap(row[x], row[x + 2]);
			}
		}
		device->unmapStagingTexture(staging);
		return image;
	}

}
