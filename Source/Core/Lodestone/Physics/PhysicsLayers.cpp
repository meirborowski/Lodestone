#include "Lodestone/Physics/PhysicsLayers.h"

#include "Lodestone/Core/Assert.h"

#include <array>

namespace Lodestone {

	namespace {

		constexpr JPH::BroadPhaseLayer StaticBroadPhase(0);
		constexpr JPH::BroadPhaseLayer MovingBroadPhase(1);
		constexpr JPH::BroadPhaseLayer TriggerBroadPhase(2);
		constexpr JPH::uint BroadPhaseLayerCount = 3;

		JPH::BroadPhaseLayer GetBroadPhaseLayerOf(JPH::ObjectLayer layer)
		{
			switch (layer)
			{
				case PhysicsLayers::Static:
					return StaticBroadPhase;
				case PhysicsLayers::Trigger:
					return TriggerBroadPhase;
				default:
					// Moving bodies and characters' inner bodies
					return MovingBroadPhase;
			}
		}

	}

	bool PhysicsLayers::Collide(JPH::ObjectLayer first, JPH::ObjectLayer second)
	{
		LS_CORE_ASSERT(first < ObjectLayerCount && second < ObjectLayerCount, "Unknown object layer");
		// Static geometry never collides with itself; triggers detect moving bodies and characters only; characters
		// meet static geometry and each other through their own queries, not their inner bodies
		static constexpr std::array<std::array<bool, ObjectLayerCount>, ObjectLayerCount> Table{{
			// Static, Moving, Trigger, Character
			{false, true, false, false}, // Static
			{true, true, true, true}, // Moving
			{false, true, false, true}, // Trigger
			{false, true, true, false}, // Character
		}};
		return Table[first][second];
	}

	JPH::uint PhysicsLayers::BroadPhaseLayers::GetNumBroadPhaseLayers() const
	{
		return BroadPhaseLayerCount;
	}

	JPH::BroadPhaseLayer PhysicsLayers::BroadPhaseLayers::GetBroadPhaseLayer(JPH::ObjectLayer layer) const
	{
		LS_CORE_ASSERT(layer < ObjectLayerCount, "Unknown object layer");
		return GetBroadPhaseLayerOf(layer);
	}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	const char* PhysicsLayers::BroadPhaseLayers::GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const
	{
		if (layer == StaticBroadPhase)
			return "Static";
		if (layer == TriggerBroadPhase)
			return "Trigger";
		return "Moving";
	}
#endif

	bool PhysicsLayers::ObjectVsBroadPhase::ShouldCollide(
		JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const
	{
		for (JPH::ObjectLayer other = 0; other < ObjectLayerCount; ++other)
			if (GetBroadPhaseLayerOf(other) == broadPhaseLayer && Collide(layer, other))
				return true;
		return false;
	}

	bool PhysicsLayers::ObjectLayerPairs::ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const
	{
		return Collide(first, second);
	}

	bool LayerMaskFilter::ObjectLayers::ShouldCollide(JPH::ObjectLayer layer) const
	{
		return layer < PhysicsLayers::ObjectLayerCount && (m_Layers & (1u << layer)) != 0;
	}

	bool LayerMaskFilter::BroadPhaseLayers::ShouldCollide(JPH::BroadPhaseLayer layer) const
	{
		for (JPH::ObjectLayer other = 0; other < PhysicsLayers::ObjectLayerCount; ++other)
			if (GetBroadPhaseLayerOf(other) == layer && (m_Layers & (1u << other)) != 0)
				return true;
		return false;
	}

}
