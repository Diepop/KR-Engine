cmake_minimum_required(VERSION 3.17)

project("KR Engine")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

set(LIBS_DIRECTORY "${CMAKE_SOURCE_DIR}/Libs")

include_directories(${CMAKE_SOURCE_DIR})
include_directories("${LIBS_DIRECTORY}/linmath")
include_directories("${CMAKE_SOURCE_DIR}/Source")

get_filename_component(ASSETS_PATH "../Assets/" ABSOLUTE)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wno-unqualified-std-cast-call -Wno-switch)
endif()

if (WIN32)
    add_compile_definitions(NOMINMAX)
    add_compile_options(/external:env:INCLUDE /external:I${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include /external:W0 /bigobj)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
    if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        add_compile_options(/external:anglebrackets)
    endif()
endif()

add_subdirectory("PCH")

add_subdirectory("Libs/Kaey-Renderer")

add_subdirectory("Source/Kaey/Engine")

add_library(Gltf STATIC "Libs/Kaey-Renderer/Gltf.cpp")
target_include_directories(Gltf PUBLIC
    ${TINYGLTF_INCLUDE_DIRS}
    ${Stb_INCLUDE_DIR}
)

add_subdirectory("Builds")
