#include "Lodestone/Scripting/ScriptComponents.h"

#include "Lodestone/Reflection/ComponentRegistry.h"

namespace Lodestone {

	void RegisterScriptComponents(ComponentRegistry& registry)
	{
		// Scripts run on the server and reach clients through what they change, so neither field replicates
		registry.Register<ScriptComponent>("Script", "Runs a Lua script on the entity")
			.Field("Script", &ScriptComponent::Script, {.Description = "The script asset's UUID (a .lua file)"})
			.Field("Properties", &ScriptComponent::Properties,
				{.Description = "Values for the script's properties, overriding its defaults: a JSON object, such as "
								"{\"Speed\": 6}"});
	}

}
