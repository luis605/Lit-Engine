# Bent: a law language and property-based fuzzer for the engine (docs/Bent.md).
# BentLang is the engine-free language core (lexer, parser, evaluator, generators, shrinker, checker);
# BentEngine interprets scene programs against a headless Engine.

# Same dialect as the Engine target (the root sets C++26; module BMIs must match).
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
    EngineObserve.cpp)
target_compile_features(BentEngine PUBLIC cxx_std_23)
target_include_directories(BentEngine PRIVATE "${PROJECT_SOURCE_DIR}/src/vendors/glfw/include")
target_link_libraries(BentEngine PUBLIC BentLang Engine)
