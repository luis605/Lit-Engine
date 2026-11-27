
set(CMAKE_CXX_STANDARD 26)

add_library(BentLang STATIC
    Value.cpp
    Lexer.cpp
    Parser.cpp
    Interp.cpp
    Gen.cpp
    Builtins.cpp
    Check.cpp)
target_compile_features(BentLang PUBLIC cxx_std_23)
target_include_directories(BentLang PUBLIC "${PROJECT_SOURCE_DIR}/src")
set_target_properties(BentLang PROPERTIES CXX_SCAN_FOR_MODULES OFF)

add_library(BentEngine STATIC
    EngineHost.cpp
    EngineObserve.cpp
    Ext_Portal.cpp
    Ext_PortalStreaming.cpp
    Ext_PortalTerrain.cpp
    Ext_PortalSave.cpp
    Ext_Render.cpp
    Ext_Gpu.cpp
    Ext_Physics.cpp
    Ext_Sim.cpp
    Ext_World.cpp)
target_compile_features(BentEngine PUBLIC cxx_std_23)
target_include_directories(BentEngine PRIVATE "${PROJECT_SOURCE_DIR}/src/vendors/glfw/include")
target_link_libraries(BentEngine PUBLIC BentLang Engine)
