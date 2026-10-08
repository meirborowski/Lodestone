# LodestoneRuntime: the player for exported games (see docs/Features/Export.md)

add_executable(LodestoneRuntime)

target_sources(LodestoneRuntime PRIVATE
	Lodestone/Runtime/RuntimeMain.cpp
)

target_include_directories(LodestoneRuntime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(LodestoneRuntime PRIVATE LodestoneCore LodestoneClient)

ls_configure_target(LodestoneRuntime)
