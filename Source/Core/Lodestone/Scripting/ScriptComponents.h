#pragma once

#include "Lodestone/Core/UUID.h"

#include <string>

// The component that attaches a Lua script to an entity (see docs/Features/Scripting.md and
// docs/Decisions/0022-scripting.md). Registered in RegisterScriptComponents()

namespace Lodestone {

	class ComponentRegistry;

	// Runs a script on the entity: the script's callbacks are called with an instance of its own for the entity
	struct ScriptComponent
	{
		// The script asset (a .lua file in the project)
		UUID Script;
		// Values for the script's properties, overriding its defaults: a JSON object, such as {"Speed": 6}. Numbers,
		// booleans and strings, and arrays of 2 to 4 numbers for vectors
		std::string Properties = "{}";
	};

	// Registers the component above
	void RegisterScriptComponents(ComponentRegistry& registry);

}
