cmake_minimum_required(VERSION 3.18)
cmake_policy(SET CMP0091 NEW)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_SOURCE_DIR}/Install/bin/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/Install/bin/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "Vcpkg toolchain file - prefer setting via -D CMAKE_TOOLCHAIN_FILE=")
    message(STATUS "Using Vcpkg toolchain from CMakeLists.txt: ${CMAKE_TOOLCHAIN_FILE}")
else()
    message(STATUS "Using Vcpkg toolchain (presumably from command line or cache): ${CMAKE_TOOLCHAIN_FILE}")
endif()

set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "Vcpkg target triplet")

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
message(STATUS "Attempting to force MSVC Runtime Library to: ${CMAKE_MSVC_RUNTIME_LIBRARY} (Static)")

project(LitEngine LANGUAGES CXX)

if(MSVC)
    message(STATUS "MSVC Runtime Library (after project command): ${CMAKE_MSVC_RUNTIME_LIBRARY}")
    if(NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "^MultiThreaded")
        message(WARNING "CMAKE_MSVC_RUNTIME_LIBRARY is NOT set to static as expected after project() command!")
    endif()
    message(STATUS "Initial CMAKE_CXX_FLAGS_RELEASE: ${CMAKE_CXX_FLAGS_RELEASE_INIT}")
    message(STATUS "Initial CMAKE_CXX_FLAGS_DEBUG: ${CMAKE_CXX_FLAGS_DEBUG_INIT}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    find_package(Bullet CONFIG REQUIRED)
else()
    find_package(Bullet REQUIRED)
    include(ExternalProject)
endif()

if(Bullet_FOUND)
    message(STATUS "Found Bullet: Version ${Bullet_VERSION}")
    message(STATUS "Bullet include dirs: ${Bullet_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "Bullet not found! Ensure it's installed via vcpkg for triplet ${VCPKG_TARGET_TRIPLET}.")
endif()

find_path(FFMPEG_INCLUDE_DIR NAMES libavcodec/avcodec.h libavformat/avformat.h)
find_library(FFMPEG_AVCODEC_LIBRARY NAMES avcodec)
find_library(FFMPEG_AVFORMAT_LIBRARY NAMES avformat)
find_library(FFMPEG_AVUTIL_LIBRARY NAMES avutil)
find_library(FFMPEG_SWSCALE_LIBRARY NAMES swscale)
find_library(FFMPEG_SWRESAMPLE_LIBRARY NAMES swresample)

set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
set(FFMPEG_LIBRARIES
    ${FFMPEG_AVCODEC_LIBRARY} ${FFMPEG_AVFORMAT_LIBRARY} ${FFMPEG_AVUTIL_LIBRARY}
    ${FFMPEG_SWSCALE_LIBRARY} ${FFMPEG_SWRESAMPLE_LIBRARY}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFMPEG REQUIRED_VARS FFMPEG_LIBRARIES FFMPEG_INCLUDE_DIRS)

if(NOT FFMPEG_FOUND)
    message(FATAL_ERROR "Could not find all required FFmpeg components via find_path/find_library. "
                        "Ensure FFmpeg (avcodec, avformat, avutil, swscale, swresample) is installed via Vcpkg "
                        "for triplet ${VCPKG_TARGET_TRIPLET}.")
else()
    message(STATUS "Found FFmpeg libraries: ${FFMPEG_LIBRARIES}")
    message(STATUS "Found FFmpeg includes: ${FFMPEG_INCLUDE_DIRS}")
endif()

file(GLOB_RECURSE SRC_FILES
    "${CMAKE_SOURCE_DIR}/Engine/*.cpp"
    )

    file(GLOB IMGUI_SRC_FILES "${CMAKE_SOURCE_DIR}/Include/imgui/*.cpp")
list(APPEND SRC_FILES
    ${IMGUI_SRC_FILES}
    "${CMAKE_SOURCE_DIR}/GameBuilder/builder.cpp"
    "${CMAKE_SOURCE_DIR}/Include/ImGuiColorTextEdit/TextEditor.cpp"
    "${CMAKE_SOURCE_DIR}/Include/rlImGui.cpp"
    "${CMAKE_SOURCE_DIR}/Include/rlFrustum.cpp"
    "${CMAKE_SOURCE_DIR}/Include/NodeEditor/crude_json.cpp"
    "${CMAKE_SOURCE_DIR}/Include/NodeEditor/imgui_node_editor.cpp"
    "${CMAKE_SOURCE_DIR}/Include/NodeEditor/imgui_canvas.cpp"
    "${CMAKE_SOURCE_DIR}/Include/NodeEditor/imgui_node_editor_api.cpp"
)

add_executable(LitEngine ${SRC_FILES})
set_target_properties(LitEngine PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

if(MSVC)
    set_target_properties(LitEngine PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    get_target_property(LitEngine_MSVC_RT LitEngine MSVC_RUNTIME_LIBRARY)
    message(STATUS "LitEngine target MSVC_RUNTIME_LIBRARY set to: ${LitEngine_MSVC_RT}")
endif()

target_include_directories(LitEngine PRIVATE
    ${CMAKE_SOURCE_DIR}/Include
    ${CMAKE_SOURCE_DIR}/Include/nlohmann/include
    ${CMAKE_SOURCE_DIR}/Include/imgui
    ${CMAKE_SOURCE_DIR}/Include/ImGuiColorTextEdit
    ${CMAKE_SOURCE_DIR}/Include/NodeEditor
    ${CMAKE_SOURCE_DIR}/Include/NodeEditor/external/stb_image
    ${CMAKE_SOURCE_DIR}/Include/NodeEditor/misc
    ${CMAKE_SOURCE_DIR}/Include/glm
    ${CMAKE_SOURCE_DIR}/Include/raylib/src/external
    ${CMAKE_SOURCE_DIR}/Include/raylib/src
    ${CMAKE_SOURCE_DIR}/Include/squish
    ${CMAKE_SOURCE_DIR}/Include/meshoptimizer
    ${CMAKE_SOURCE_DIR}/Include/pybind11/include
    ${CMAKE_SOURCE_DIR}/Include/bullet3/src/
    ${CMAKE_SOURCE_DIR}
    ${BULLET_}
    ${FFMPEG_INCLUDE_DIRS}
)

add_subdirectory("${CMAKE_SOURCE_DIR}/Include/pybind11"        pybind11_build)
add_subdirectory("${CMAKE_SOURCE_DIR}/Include/meshoptimizer"   meshoptimizer_build)
add_subdirectory("${CMAKE_SOURCE_DIR}/Include/squish"          squish_build)

if (MSVC)
    add_subdirectory("${CMAKE_SOURCE_DIR}/Include/raylib"          raylib_build)

    target_link_libraries(LitEngine PRIVATE
        raylib
    )
else()
    set(RAYLIB_SRC_DIR   "${CMAKE_SOURCE_DIR}/Include/raylib/src")
    set(RAYLIB_BUILD_DIR "${CMAKE_BINARY_DIR}/raylib_build")

    ExternalProject_Add(raylib_project
        PREFIX            "${RAYLIB_BUILD_DIR}"
        SOURCE_DIR        "${RAYLIB_SRC_DIR}"
        BINARY_DIR        "${RAYLIB_SRC_DIR}"
        DOWNLOAD_COMMAND    ""
        CONFIGURE_COMMAND   ""
        BUILD_COMMAND
            make
            -B
            GRAPHICS=GRAPHICS_API_OPENGL_43
            CUSTOM_CFLAGS+=-DSUPPORT_FILEFORMAT_BMP\ -DSUPPORT_FILEFORMAT_TGA\ -DSUPPORT_FILEFORMAT_JPG\ -DSUPPORT_FILEFORMAT_PSD\ -DSUPPORT_FILEFORMAT_HDR\ -DSUPPORT_FILEFORMAT_PIC\ -DSUPPORT_FILEFORMAT_KTX\ -DSUPPORT_FILEFORMAT_ASTC\ -DSUPPORT_FILEFORMAT_PKM\ -DSUPPORT_FILEFORMAT_PVR\ -DSUPPORT_FILEFORMAT_SVG
        INSTALL_COMMAND     ""
        BUILD_BYPRODUCTS    "${RAYLIB_BUILD_DIR}/libraylib.a"
    )

    target_link_libraries(LitEngine PRIVATE
        ${RAYLIB_SRC_DIR}/libraylib.a
    )
endif()

target_link_libraries(LitEngine PRIVATE
    BulletDynamics BulletCollision LinearMath
    ${FFMPEG_LIBRARIES}
    meshoptimizer
    pybind11::embed
    squish
)

if (MSVC)
    target_link_libraries(LitEngine PRIVATE
        Mfplat Mfuuid mfreadwrite strmiids ole32 Ws2_32 bcrypt secur32 crypt32 Rpcrt4
    )
else()
    set(DEBUGGER gdb)

    add_custom_target(run
        COMMAND "${CMAKE_SOURCE_DIR}/build/LitEngine"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS LitEngine
    )

    add_custom_target(debug
        COMMAND ${CMAKE_COMMAND} -E echo "Make sure you set -DCMAKE_BUILD_TYPE=Debug in your cmake command"
        COMMAND ${DEBUGGER} ${CMAKE_BINARY_DIR}/LitEngine
        DEPENDS LitEngine
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()