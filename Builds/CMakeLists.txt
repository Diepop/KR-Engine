cmake_minimum_required(VERSION 3.17)

set(BuildsDir ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(Main
    "${BuildsDir}/Main.cpp"
)
target_compile_definitions(Main PUBLIC
    ASSETS_PATH="${ASSETS_PATH}"
    SHADERS_PATH="${ShadersDir}"
)
target_link_libraries(Main PUBLIC
    PCH
    Renderer
    ShaderCompiler
)
target_precompile_headers(Main REUSE_FROM PCH)

add_executable(Test
    "${BuildsDir}/Test.cpp"
)
target_compile_definitions(Test PUBLIC
    ASSETS_PATH="${ASSETS_PATH}"
    SHADERS_PATH="${ShadersDir}"
)
target_link_libraries(Test PUBLIC
    PCH
    Renderer
    ShaderCompiler
)
target_precompile_headers(Test REUSE_FROM PCH)

add_executable(Nodes
    "Nodes.cpp"
)
target_compile_definitions(Nodes PUBLIC
    ASSETS_PATH="${ASSETS_PATH}"
    SHADERS_PATH="${ShadersDir}"
)
target_link_libraries(Nodes PUBLIC
    PCH
    Renderer
    ShaderCompiler
)
target_precompile_headers(Nodes REUSE_FROM PCH)

add_executable(NodesOld
    "NodesOld.cpp"
)
target_link_libraries(NodesOld PUBLIC
    PCH
    Renderer
    ShaderCompiler
)
target_precompile_headers(NodesOld REUSE_FROM PCH)

add_executable(PVP
    "${BuildsDir}/PVP.cpp"
    "${BuildsDir}/Mesh.cpp"
    "${BuildsDir}/MeshFile.cpp"
)
target_compile_definitions(PVP PUBLIC
    ASSETS_PATH="${ASSETS_PATH}"
    SHADERS_PATH="${ShadersDir}"
)
target_link_libraries(PVP PUBLIC
    PCH
    Renderer
    ShaderCompiler
)
target_precompile_headers(PVP REUSE_FROM PCH)

add_executable(SSS
    "${BuildsDir}/SSS.cpp"
)
target_compile_definitions(SSS PUBLIC
    ASSETS_PATH="${ASSETS_PATH}"
    SHADERS_PATH="${ShadersDir}"
)
target_link_libraries(SSS PUBLIC
    PCH
    Renderer
    ShaderCompiler
)
target_precompile_headers(SSS REUSE_FROM PCH)

add_library(DLL SHARED
    "${BuildsDir}/DLL.cpp"
    "${BuildsDir}/MeshFile.cpp"
)
target_compile_definitions(DLL PUBLIC
    ASSETS_PATH="${ASSETS_PATH}"
    SHADERS_PATH="${ShadersDir}"
)
target_link_libraries(DLL PUBLIC
    PCH
)
target_precompile_headers(DLL REUSE_FROM PCH)
