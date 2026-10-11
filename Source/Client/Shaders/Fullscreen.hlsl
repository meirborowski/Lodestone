// The vertex shader of every pass that runs a pixel shader on each pixel: a triangle covering the image at the far
// plane - depth 0, as the scene's depth is reversed - so the sky can draw only where nothing else did

#include "Fullscreen.hlsli"

FullscreenVertex VSMain(uint vertexId : SV_VertexID)
{
	return MakeFullscreenVertex(vertexId, 0.0);
}
