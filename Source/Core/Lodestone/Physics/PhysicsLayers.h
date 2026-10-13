#pragma once

// Internal to the physics module (see JoltRuntime.h)

#include "Lodestone/Physics/JoltRuntime.h"

#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

#include <cstdint>

namespace Lodestone {

	// Which bodies collide with which (see docs/Decisions/0021-physics.md): static geometry, moving bodies (dynamic
	// and kinematic), triggers, and characters' inner bodies. Static geometry, moving bodies and triggers each have a
	// broad-phase layer, so finding pairs never tests static geometry against itself or triggers against static
	// geometry; characters' inner bodies share the moving one
	class PhysicsLayers
	{
	public:
		static constexpr JPH::ObjectLayer Static = 0;
		static constexpr JPH::ObjectLayer Moving = 1;
		static constexpr JPH::ObjectLayer Trigger = 2;
		static constexpr JPH::ObjectLayer Character = 3;
		static constexpr uint32_t ObjectLayerCount = 4;

		// Whether bodies in the two layers collide (or, with a trigger, are detected)
		static bool Collide(JPH::ObjectLayer first, JPH::ObjectLayer second);

		const JPH::BroadPhaseLayerInterface& GetBroadPhaseLayers() const { return m_BroadPhaseLayers; }
		const JPH::ObjectVsBroadPhaseLayerFilter& GetObjectVsBroadPhaseFilter() const { return m_ObjectVsBroadPhase; }
		const JPH::ObjectLayerPairFilter& GetObjectLayerPairFilter() const { return m_ObjectLayerPairs; }

	private:
		class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
		{
		public:
			JPH::uint GetNumBroadPhaseLayers() const override;
			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override;
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override;
#endif
		};

		class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override;
		};

		class ObjectLayerPairs final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override;
		};

	private:
		BroadPhaseLayers m_BroadPhaseLayers;
		ObjectVsBroadPhase m_ObjectVsBroadPhase;
		ObjectLayerPairs m_ObjectLayerPairs;
	};

	// The layers a query is interested in, as the object layer and broad-phase layer filters Jolt's queries take
	class LayerMaskFilter
	{
	public:
		// A bit for each ObjectLayer
		explicit LayerMaskFilter(uint32_t layers)
			: m_ObjectLayers(layers), m_BroadPhaseLayers(layers)
		{
		}

		const JPH::ObjectLayerFilter& GetObjectLayers() const { return m_ObjectLayers; }
		const JPH::BroadPhaseLayerFilter& GetBroadPhaseLayers() const { return m_BroadPhaseLayers; }

	private:
		class ObjectLayers final : public JPH::ObjectLayerFilter
		{
		public:
			explicit ObjectLayers(uint32_t layers)
				: m_Layers(layers)
			{
			}

			bool ShouldCollide(JPH::ObjectLayer layer) const override;

		private:
			uint32_t m_Layers;
		};

		class BroadPhaseLayers final : public JPH::BroadPhaseLayerFilter
		{
		public:
			explicit BroadPhaseLayers(uint32_t layers)
				: m_Layers(layers)
			{
			}

			bool ShouldCollide(JPH::BroadPhaseLayer layer) const override;

		private:
			uint32_t m_Layers;
		};

	private:
		ObjectLayers m_ObjectLayers;
		BroadPhaseLayers m_BroadPhaseLayers;
	};

}
