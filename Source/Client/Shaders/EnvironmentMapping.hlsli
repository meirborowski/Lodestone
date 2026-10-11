// How directions map to equirectangular images and cube map faces. Lodestone/Renderer/EnvironmentMap.cpp maps them the
// same way on the CPU - change both together.
//
// An equirectangular image's center column looks toward -Z (the way cameras face by default), its right quarter toward
// +X, its top row straight up

#ifndef LODESTONE_ENVIRONMENT_MAPPING_HLSLI
#define LODESTONE_ENVIRONMENT_MAPPING_HLSLI

// Where a unit direction falls in an equirectangular image, from (0, 0) at the top left to (1, 1)
float2 DirectionToEquirectangular(float3 direction)
{
	const float azimuth = atan2(direction.x, -direction.z);
	const float elevation = asin(clamp(direction.y, -1.0, 1.0));
	return float2(azimuth / (2.0 * 3.14159265358979) + 0.5, 0.5 - elevation / 3.14159265358979);
}

// The direction through a point of a cube map face (+X, -X, +Y, -Y, +Z, -Z), with the point from -1 to 1 across the
// face: x to the right, y down, as Vulkan samples cube maps
float3 CubeFaceDirection(uint face, float2 facePoint)
{
	float3 direction;
	switch (face)
	{
		case 0:
			direction = float3(1.0, -facePoint.y, -facePoint.x);
			break;
		case 1:
			direction = float3(-1.0, -facePoint.y, facePoint.x);
			break;
		case 2:
			direction = float3(facePoint.x, 1.0, facePoint.y);
			break;
		case 3:
			direction = float3(facePoint.x, -1.0, -facePoint.y);
			break;
		case 4:
			direction = float3(facePoint.x, -facePoint.y, 1.0);
			break;
		default:
			direction = float3(-facePoint.x, -facePoint.y, -1.0);
			break;
	}
	return normalize(direction);
}

#endif
