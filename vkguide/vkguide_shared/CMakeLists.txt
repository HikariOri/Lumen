add_library (vkguide_shared STATIC
  vk_types.h
  vk_initializers.cpp
  vk_initializers.h
  vk_images.h
  vk_images.cpp 
  vk_descriptors.h
  vk_descriptors.cpp
  vk_pipelines.h
  vk_pipelines.cpp
  camera.cpp
  camera.h
  meshes.h
  meshes.cpp
)

target_compile_definitions(vkguide_shared PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_include_directories(vkguide_shared PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_precompile_headers(vkguide_shared PUBLIC <optional> <vector> <memory> <string> <vector> <unordered_map> <glm/mat4x4.hpp>  <glm/vec4.hpp> <vulkan/vulkan.h>)

# include
target_include_directories(vkguide_shared PRIVATE ${Stb_INCLUDE_DIR})

get_target_property(vulkan_include_dirs Vulkan::Vulkan INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "Vulkan include dirs = ${vulkan_include_dirs}")

# 链接库
target_link_libraries(vkguide_shared
    PRIVATE SDL3::SDL3
    PRIVATE $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
    PRIVATE glm::glm
    PRIVATE Vulkan::Vulkan
    PRIVATE GPUOpen::VulkanMemoryAllocator
    PRIVATE volk::volk
    PRIVATE volk::volk_headers
    # PRIVATE imgui::imgui
    PRIVATE fastgltf::fastgltf
    PRIVATE fmt::fmt
    PRIVATE vk-bootstrap::vk-bootstrap
    PRIVATE vk-bootstrap::vk-bootstrap-compiler-warnings
    PRIVATE Vulkan::Headers
)
