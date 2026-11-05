cmake_minimum_required(VERSION 3.28)

add_executable(Editor)

target_sources(Editor
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
            Application.cppm
            Context.cppm
            Commands.cppm
            Viewport.cppm
            Palette.cppm
            Hud.cppm
            Look.cppm
            Autopilot.cppm
            Inspector/Inspector.cppm
            Gizmo/Gizmo.cppm
            Hierarchy/Hierarchy.cppm
            ProfilerPanel/ProfilerPanel.cppm
            HotReload/HotReload.cppm
            Diagnostics/Diagnostics.cppm
    PRIVATE
        Application/Application.cpp
        Application/ApplicationUi.cpp
        Application/ApplicationScenes.cpp
        Application/ApplicationShowcase.cpp
        Application/ApplicationEditing.cpp
        Application/ApplicationAutopilot.cpp
        Application/ApplicationCommands.cpp
        Application/ApplicationViewCommands.cpp
        Application/ApplicationRenderCommands.cpp
        Application/ApplicationIkCommands.cpp
        Context/Context.cpp
        Context/ContextTheme.cpp
        Context/ContextExpression.cpp
        Commands/Commands.cpp
        Viewport/Viewport.cpp
        Viewport/ViewportOverlays.cpp
        Viewport/ViewportIcons.cpp
        Palette/Palette.cpp
        Hud/Hud.cpp
        Hud/HudPanels.cpp
        Hud/HudPie.cpp
        Look/Look.cpp
        Look/LookSettings.cpp
        Look/LookWorld.cpp
        Look/LookQuality.cpp
        Inspector/InspectorReflection.cpp
        Inspector/InspectorRig.cpp
        Inspector/InspectorLightPresets.cpp
        Application/ApplicationQualityCommands.cpp
        Application/ApplicationProbeOverlay.cpp
        Autopilot/Autopilot.cpp
        Inspector/Inspector.cpp
        Inspector/InspectorMaterial.cpp
        Inspector/InspectorComponents.cpp
        Inspector/InspectorPlacement.cpp
        Gizmo/Gizmo.cpp
        Gizmo/GizmoApply.cpp
        Gizmo/GizmoHandles.cpp
        Hierarchy/Hierarchy.cpp
        Hierarchy/HierarchyRow.cpp
        Hierarchy/HierarchySearch.cpp
        ProfilerPanel/ProfilerPanel.cpp
        ProfilerPanel/ProfilerDraw.cpp
        HotReload/HotReload.cpp
        Diagnostics/Diagnostics.cpp
        Application/ApplicationToolsCommands.cpp
        main.cpp

)

target_compile_features(Editor PRIVATE cxx_std_23)

target_include_directories(Editor PRIVATE
    "${PROJECT_SOURCE_DIR}/src/vendors/glfw/include"

    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(Editor PRIVATE Engine glfw)
target_compile_definitions(Editor PRIVATE LIT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

set(RESOURCES_DIR "${CMAKE_SOURCE_DIR}/resources")
set(DESTINATION_DIR "${CMAKE_BINARY_DIR}/resources")

add_custom_command(
    TARGET Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_newer
            "${RESOURCES_DIR}"
            "${DESTINATION_DIR}"
)
if(LIT_SANITIZE MATCHES "undefined" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_target_property(editorModuleInterfaces Editor CXX_MODULE_SET_CXX_MODULES)
    set_source_files_properties(${editorModuleInterfaces} PROPERTIES COMPILE_OPTIONS -fno-sanitize=undefined)
endif()
