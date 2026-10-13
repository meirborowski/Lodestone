#pragma once

#include "Lodestone/Asset/Model.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Scene/Components.h"
#include "Lodestone/Serialization/Json.h"

#include <string_view>

namespace Lodestone {

	class ComponentRegistry;

	// The entities that place a model in a scene, as an entity list in EntitySerializer's tree format: a root entity
	// named after the model, with a ModelInstance (and an Animator playing the first clip, if the model has clips), and
	// below it an entity for each of the model's nodes, in the model's hierarchy, with the node's name, transform and
	// ModelNode - and for nodes with a mesh, a MeshRenderer drawing that mesh of the model asset, and a Skin if the
	// node skins it. Copy it into a scene with EntitySerializer::InstantiateTree() and IdPolicy::Regenerate.
	//
	// Takes time in proportion to the number of nodes, however deep the hierarchy is. A node reached twice (which a
	// valid model doesn't have) is placed only the first time
	Json::Value BuildModelEntities(const Model& model, UUID modelAsset, std::string_view name,
		const ComponentRegistry& components = GetEngineComponentRegistry());

}
