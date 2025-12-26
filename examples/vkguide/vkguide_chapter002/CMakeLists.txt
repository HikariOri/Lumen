set(CUR_TARGET vkguide_chapter002)

# 搜集源文件
file(GLOB_RECURSE ${CUR_TARGET}_SOURCES
    CONFIGURE_DEPENDS
    src/*.cpp
)

add_executable(${CUR_TARGET} ${${CUR_TARGET}_SOURCES})

# include
target_include_directories(${CUR_TARGET} PUBLIC include)
target_include_directories(${CUR_TARGET} PRIVATE ${Stb_INCLUDE_DIR})

# 链接库
target_link_libraries(${CUR_TARGET}
    PRIVATE SDL3::SDL3
    PRIVATE $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
    PRIVATE glm::glm
    PRIVATE Vulkan::Vulkan
    PRIVATE GPUOpen::VulkanMemoryAllocator
    PRIVATE volk::volk
    PRIVATE volk::volk_headers
    PRIVATE imgui::imgui
    PRIVATE fastgltf::fastgltf
    PRIVATE fmt::fmt
    PRIVATE vk-bootstrap::vk-bootstrap
    PRIVATE vk-bootstrap::vk-bootstrap-compiler-warnings
    PRIVATE vkguide_shared  
)
