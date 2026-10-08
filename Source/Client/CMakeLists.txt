# LodestoneClient: the window, device input, renderer and audio - everything a player needs that a dedicated server
# doesn't (see docs/Architecture.md#targets-and-layering)

add_library(LodestoneClient STATIC)
add_library(Lodestone::Client ALIAS LodestoneClient)

target_sources(LodestoneClient PRIVATE
	Lodestone/Client/ClientModule.cpp
)

target_include_directories(LodestoneClient PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(LodestoneClient PUBLIC LodestoneCore)

ls_configure_target(LodestoneClient)
