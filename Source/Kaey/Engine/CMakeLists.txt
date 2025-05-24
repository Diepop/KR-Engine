cmake_minimum_required(VERSION 3.17)

set(EngineDir ${CMAKE_CURRENT_SOURCE_DIR})

set(EngineSources
    "Utils"
    "Engine"
    "Scene"
)

list(TRANSFORM EngineSources PREPEND "${EngineDir}/")

list(TRANSFORM EngineHeaders APPEND ".hpp")
list(TRANSFORM EngineSources APPEND ".cpp")

add_library(Engine STATIC
    ${EngineHeaders}
    ${EngineSources}
    "${EngineDir}/Utility.hpp"
    "${EngineDir}/AssetMap.hpp"
)

target_include_directories(Engine PUBLIC ${RendererDir})

target_link_libraries(Engine PUBLIC PCH)

target_precompile_headers(Engine REUSE_FROM PCH)
