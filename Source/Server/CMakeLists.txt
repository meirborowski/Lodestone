# LodestoneServer: the headless dedicated server. It links only LodestoneCore - no window, renderer or audio
# (see docs/Architecture.md#targets-and-layering)

add_executable(LodestoneServer)

target_sources(LodestoneServer PRIVATE
	Lodestone/Server/ServerMain.cpp
)

target_include_directories(LodestoneServer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(LodestoneServer PRIVATE LodestoneCore)

ls_configure_target(LodestoneServer)
