# LodestoneEditor: the editor UI, the MCP server and headless mode. Never built in Dist, so exported games can't
# contain editor code (see docs/Architecture.md#targets-and-layering).
#
# The editor's code is a library, so its tests link it; the executable is only its main()

add_library(LodestoneEditorLib STATIC)
add_library(Lodestone::Editor ALIAS LodestoneEditorLib)

target_sources(LodestoneEditorLib PRIVATE
	Lodestone/Editor/Commands/Command.h
	Lodestone/Editor/Commands/CommandHistory.cpp
	Lodestone/Editor/Commands/CommandHistory.h
	Lodestone/Editor/Commands/EntityCommands.cpp
	Lodestone/Editor/Commands/EntityCommands.h
	Lodestone/Editor/EditorApp.cpp
	Lodestone/Editor/EditorApp.h
	Lodestone/Editor/EditorCamera.cpp
	Lodestone/Editor/EditorCamera.h
	Lodestone/Editor/EditorContext.cpp
	Lodestone/Editor/EditorContext.h
	Lodestone/Editor/LogBuffer.cpp
	Lodestone/Editor/LogBuffer.h
	Lodestone/Editor/Mcp/EditorTools.cpp
	Lodestone/Editor/Mcp/EditorTools.h
	Lodestone/Editor/Mcp/HttpTransport.cpp
	Lodestone/Editor/Mcp/HttpTransport.h
	Lodestone/Editor/Mcp/MainThreadDispatcher.cpp
	Lodestone/Editor/Mcp/MainThreadDispatcher.h
	Lodestone/Editor/Mcp/McpServer.cpp
	Lodestone/Editor/Mcp/McpServer.h
	Lodestone/Editor/Mcp/StdioTransport.cpp
	Lodestone/Editor/Mcp/StdioTransport.h
	Lodestone/Editor/SceneView.cpp
	Lodestone/Editor/SceneView.h
	Lodestone/Editor/UI/ConsolePanel.cpp
	Lodestone/Editor/UI/ConsolePanel.h
	Lodestone/Editor/UI/ContentBrowserPanel.cpp
	Lodestone/Editor/UI/ContentBrowserPanel.h
	Lodestone/Editor/UI/EditorUI.cpp
	Lodestone/Editor/UI/EditorUI.h
	Lodestone/Editor/UI/HierarchyPanel.cpp
	Lodestone/Editor/UI/HierarchyPanel.h
	Lodestone/Editor/UI/InspectorPanel.cpp
	Lodestone/Editor/UI/InspectorPanel.h
	Lodestone/Editor/UI/UIHelpers.cpp
	Lodestone/Editor/UI/UIHelpers.h
	Lodestone/Editor/UI/ViewportPanel.cpp
	Lodestone/Editor/UI/ViewportPanel.h
)

target_include_directories(LodestoneEditorLib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(LodestoneEditorLib
	PUBLIC LodestoneCore LodestoneClient
	PRIVATE imguizmo httplib
)

ls_configure_target(LodestoneEditorLib)

add_executable(LodestoneEditor Lodestone/Editor/EditorMain.cpp)
target_link_libraries(LodestoneEditor PRIVATE LodestoneEditorLib)
ls_configure_target(LodestoneEditor)

if(LS_MULTI_CONFIG)
	# A multi-config build tree can't omit the targets for a single configuration, so Dist builds skip them instead
	set_target_properties(LodestoneEditorLib LodestoneEditor PROPERTIES EXCLUDE_FROM_ALL "$<CONFIG:Dist>")
endif()
