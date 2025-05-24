cmake_minimum_required(VERSION 3.17)

set(PCHDir ${CMAKE_CURRENT_SOURCE_DIR}/)

set(PCHHeaders "pch.hpp")
set(PCHSources "pch.cpp")

list(TRANSFORM PCHHeaders PREPEND ${PCHDir})
list(TRANSFORM PCHSources PREPEND ${PCHDir})

include("VMALib.cmake")

add_library(PCH STATIC ${PCHSources})

find_package(magic_enum CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(tinyexr CONFIG REQUIRED)
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")
find_package(Stb REQUIRED)
find_path(HALF_INCLUDE_DIRS "half.hpp")
find_path(FP16_INCLUDE_DIRS "fp16.h")
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nameof CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)

target_include_directories(PCH PUBLIC
    "${LIBS_DIRECTORY}/linmath/include"
    "${LIBS_DIRECTORY}/imnodes"
    "${LIBS_DIRECTORY}/Kaey-Renderer"
    "${LIBS_DIRECTORY}/imgui"
    "${LIBS_DIRECTORY}/imgui/backends"
    "${LIBS_DIRECTORY}/imgui-node-editor"
    "${LIBS_DIRECTORY}/Kaey-Renderer/Source"
    "${LIBS_DIRECTORY}/ImGuizmo"

    ${TINYGLTF_INCLUDE_DIRS}
    ${Stb_INCLUDE_DIR}
    ${FP16_INCLUDE_DIRS}
    ${HALF_INCLUDE_DIRS}
)

target_link_libraries(PCH PUBLIC
    glfw
    magic_enum::magic_enum
    glslang::glslang glslang::glslang-default-resource-limits glslang::SPIRV glslang::SPVRemapper
    Gltf
    unofficial::tinyexr::tinyexr
    nlohmann_json::nlohmann_json
    nameof::nameof
    VMALib
    tinyobjloader::tinyobjloader
    "$ENV{VULKAN_SDK}/Lib/slang.lib"
)

target_compile_definitions(PCH PUBLIC
    GLFW_INCLUDE_VULKAN=1
    IMGUI_DEFINE_MATH_OPERATORS=1
)

target_precompile_headers(PCH PUBLIC ${PCHHeaders})

