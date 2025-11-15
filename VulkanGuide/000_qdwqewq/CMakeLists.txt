set(CUR_TARGET 000_qdwqewq)
add_executable(
    ${CUR_TARGET} 
    ./src/main.cpp 
    ./src/camera.cpp
    ./src/vk_descriptors.cpp
    ./src/vk_engine.cpp
    ./src/vk_images.cpp
    ./src/vk_initializers.cpp
    ./src/vk_loader.cpp
    ./src/vk_pipelines.cpp
)
target_include_directories(${CUR_TARGET} PUBLIC include)
target_link_libraries(${CUR_TARGET} PRIVATE SDL3::SDL3)
target_link_libraries(${CUR_TARGET} PRIVATE $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>)
target_link_libraries(${CUR_TARGET} PRIVATE glm::glm)
target_link_libraries(${CUR_TARGET} PRIVATE Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
target_link_libraries(${CUR_TARGET} PRIVATE slang::slang)
target_link_libraries(${CUR_TARGET} PRIVATE imgui::imgui)
target_link_libraries(${CUR_TARGET} PRIVATE imguizmo::imguizmo)
target_link_libraries(${CUR_TARGET}  PRIVATE unofficial::imgui-node-editor::imgui-node-editor)
target_link_libraries(${CUR_TARGET} PRIVATE ${BULLET_LIBRARIES})
target_link_libraries(${CUR_TARGET} PRIVATE BulletSoftBody)
target_link_libraries(${CUR_TARGET} PRIVATE tinyobjloader::tinyobjloader_double)
target_link_libraries(${CUR_TARGET} PRIVATE vk-bootstrap::vk-bootstrap vk-bootstrap::vk-bootstrap-compiler-warnings)
target_include_directories(${CUR_TARGET} PRIVATE ${Stb_INCLUDE_DIR})

