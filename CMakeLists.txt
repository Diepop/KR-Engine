cmake_minimum_required(VERSION 3.17)

project("Kaey Renderer")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

include_directories(${CMAKE_SOURCE_DIR})
include_directories("${CMAKE_SOURCE_DIR}/lib/linmath")

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wno-unqualified-std-cast-call -Wno-switch)
endif()

if (WIN32)
    add_compile_definitions(NOMINMAX)
    add_compile_options(/external:env:INCLUDE /external:I${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include /external:W0 /bigobj)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        add_compile_options(/external:anglebrackets)
    endif()
endif()

add_executable(Main
    "Source.cpp"
    
)
