# LodestoneEditor: the editor UI, the MCP server and headless mode. Never built in Dist, so exported games can't
# contain editor code (see docs/Architecture.md#targets-and-layering)

add_executable(LodestoneEditor)

target_sources(LodestoneEditor PRIVATE
	Lodestone/Editor/EditorMain.cpp
)

target_include_directories(LodestoneEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(LodestoneEditor PRIVATE LodestoneCore LodestoneClient)

ls_configure_target(LodestoneEditor)

if(LS_MULTI_CONFIG)
	# A multi-config build tree can't omit the target for a single configuration, so Dist builds skip it instead
	set_target_properties(LodestoneEditor PROPERTIES EXCLUDE_FROM_ALL "$<CONFIG:Dist>")
endif()
