add_library(Engine STATIC)

target_sources(Engine
    PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
    FILES
        engine.cppm
    PRIVATE
        engine.cpp
)

target_compile_features(Engine PUBLIC cxx_std_23)
