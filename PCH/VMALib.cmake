find_package(Vulkan)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)

#find_package(spirv_cross_c)
find_package(spirv_cross_core)
#find_package(spirv_cross_cpp)
#find_package(spirv_cross_glsl)
#find_package(spirv_cross_hlsl)
#find_package(spirv_cross_msl)
#find_package(spirv_cross_reflect)
#find_package(spirv_cross_util)

add_library(VMALib STATIC "${PCHDir}VMALib.cpp")

target_compile_definitions(VMALib PRIVATE
    VMA_IMPLEMENTATION=1
)

target_link_libraries(VMALib PUBLIC
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
    #spirv-cross-c
    spirv-cross-core
    #spirv-cross-cpp
    #spirv-cross-glsl
    #spirv-cross-hlsl
    #spirv-cross-msl
    #spirv-cross-reflect
    #spirv-cross-util
)
