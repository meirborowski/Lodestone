#include "Render/RenderTestDevice.h"

namespace Lodestone::Testing {

	GraphicsDeviceConfig MakeRenderTestDeviceConfig()
	{
		GraphicsDeviceConfig config;
#if defined(LS_LAVAPIPE_DRIVER)
		config.Driver = LS_LAVAPIPE_DRIVER;
#endif
		return config;
	}

}
