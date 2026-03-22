set(ENGINE_TARGET engine)

file(
    GLOB_RECURSE ENGINE_SOURCES
    CONFIGURE_DEPENDS
   ./src/*.cpp
)

add_library(${ENGINE_TARGET} STATIC ${ENGINE_SOURCES})

target_include_directories(${ENGINE_TARGET} PUBLIC include)

target_link_libraries(${ENGINE_TARGET} PUBLIC Vulkan::Vulkan)
target_link_libraries(${ENGINE_TARGET} PUBLIC SDL3::SDL3)
target_link_libraries(${ENGINE_TARGET} PUBLIC $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>)
target_link_libraries(${ENGINE_TARGET} PUBLIC glm::glm)
target_link_libraries(${ENGINE_TARGET} PUBLIC $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>)
target_link_libraries(${ENGINE_TARGET} PUBLIC spdlog::spdlog)
target_link_libraries(${ENGINE_TARGET} PUBLIC slang::slang)
target_link_libraries(${ENGINE_TARGET} PUBLIC glslang::glslang glslang::glslang-default-resource-limits glslang::SPIRV glslang::SPVRemapper)
target_include_directories(${ENGINE_TARGET} PUBLIC ${TINYGLTF_INCLUDE_DIRS})
target_link_libraries(${ENGINE_TARGET} PUBLIC tinyobjloader::tinyobjloader_double)
target_include_directories(${ENGINE_TARGET} PUBLIC ${Stb_INCLUDE_DIR})
target_include_directories(${ENGINE_TARGET} PUBLIC imgui::imgui)
target_link_libraries(${ENGINE_TARGET} PRIVATE imgui::imgui)
target_link_libraries(${ENGINE_TARGET} PUBLIC imguizmo::imguizmo)
target_link_libraries(${ENGINE_TARGET} PRIVATE cpr::cpr)
target_link_libraries(${ENGINE_TARGET} PRIVATE Vulkan::Headers GPUOpen::VulkanMemoryAllocator)
target_link_libraries(${ENGINE_TARGET} PRIVATE ghcFilesystem::ghc_filesystem)
target_link_libraries(${ENGINE_TARGET} PRIVATE EnTT::EnTT)

target_precompile_headers(${ENGINE_TARGET} PUBLIC ./include/pch.hpp)

add_custom_command(
  TARGET ${ENGINE_TARGET}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/light_icon.vert"
          -o "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders/light_icon.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/light_icon.frag"
          -o "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders/light_icon.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/light_debug.vert"
          -o "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders/light_debug.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/light_debug.frag"
          -o "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders/light_debug.frag.spv"
  COMMENT "Compile engine light viewport gizmo shaders"
  VERBATIM
)

