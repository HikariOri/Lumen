set(ENGINE_TARGET engine)

file(
    GLOB_RECURSE ENGINE_SOURCES
    CONFIGURE_DEPENDS
   ./src/*.cpp
)

add_library(${ENGINE_TARGET} STATIC ${ENGINE_SOURCES})

set_source_files_properties(
  ./src/stb_impl.cpp ./src/vma_impl.cpp
  PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

target_include_directories(${ENGINE_TARGET} PUBLIC src)

target_link_libraries(${ENGINE_TARGET} PUBLIC Vulkan::Vulkan)
target_link_libraries(${ENGINE_TARGET} PUBLIC SDL3::SDL3)
target_link_libraries(${ENGINE_TARGET} PUBLIC $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>)
target_link_libraries(${ENGINE_TARGET} PUBLIC glm::glm)
target_link_libraries(${ENGINE_TARGET} PUBLIC spdlog::spdlog)
target_include_directories(${ENGINE_TARGET} PUBLIC ${TINYGLTF_INCLUDE_DIRS})
target_include_directories(${ENGINE_TARGET} PUBLIC ${Stb_INCLUDE_DIR})
target_include_directories(${ENGINE_TARGET} PUBLIC imgui::imgui)
target_link_libraries(${ENGINE_TARGET} PUBLIC imgui::imgui)
target_link_libraries(${ENGINE_TARGET} PUBLIC imguizmo::imguizmo)
target_link_libraries(${ENGINE_TARGET} PUBLIC Vulkan::Headers)
target_link_libraries(${ENGINE_TARGET} PUBLIC GPUOpen::VulkanMemoryAllocator)
target_link_libraries(${ENGINE_TARGET} PUBLIC ghcFilesystem::ghc_filesystem)
target_link_libraries(${ENGINE_TARGET} PUBLIC EnTT::EnTT)
target_link_libraries(${ENGINE_TARGET} PUBLIC Tracy::TracyClient)
target_link_libraries(${ENGINE_TARGET} PRIVATE KTX::ktx)
target_link_libraries(${ENGINE_TARGET} PUBLIC spirv-cross-core)
target_link_libraries(${ENGINE_TARGET} PUBLIC spirv-cross-reflect)
target_link_libraries(${ENGINE_TARGET} PUBLIC spirv-cross-util)
target_link_libraries(${ENGINE_TARGET} PUBLIC spirv-cross-glsl)
target_link_libraries(${ENGINE_TARGET} PUBLIC spirv-cross-cpp)
target_link_libraries(${ENGINE_TARGET} PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(${ENGINE_TARGET} PUBLIC volk::volk volk::volk_headers)
target_link_libraries(${ENGINE_TARGET} PUBLIC unofficial::spirv-reflect)
target_link_libraries(${ENGINE_TARGET} PUBLIC unofficial::shaderc::shaderc)
target_link_libraries(${ENGINE_TARGET} PUBLIC Boost::pfr)
target_link_libraries(
        ${ENGINE_TARGET} PUBLIC 
        absl::base
        absl::strings
        absl::str_format
        absl::flat_hash_map
        absl::flat_hash_set
        absl::synchronization
        absl::time
        absl::status
)

target_precompile_headers(${ENGINE_TARGET} PUBLIC ./src/pch.hpp)

add_custom_command(
  TARGET ${ENGINE_TARGET}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_SOURCE_DIR}/shaders/glsl/light_icon.vert"
          -o "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders/light_icon.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_SOURCE_DIR}/shaders/glsl/light_icon.frag"
          -o "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders/light_icon.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_SOURCE_DIR}/shaders/glsl/light_debug.vert"
          -o "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders/light_debug.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_SOURCE_DIR}/shaders/glsl/light_debug.frag"
          -o "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders/light_debug.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_SOURCE_DIR}/shaders/glsl/pbr_forward.vert"
          -o "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders/pbr_forward.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_SOURCE_DIR}/shaders/glsl/pbr_forward.frag"
          -o "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders/pbr_forward.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_SOURCE_DIR}/shaders/glsl/pick_id.vert"
          -o "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders/pick_id.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_SOURCE_DIR}/shaders/glsl/pick_id.frag"
          -o "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders/pick_id.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_SOURCE_DIR}/shaders/glsl/pick_id_visualize.vert"
          -o "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders/pick_id_visualize.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_SOURCE_DIR}/shaders/glsl/pick_id_visualize.frag"
          -o "$<TARGET_FILE_DIR:${ENGINE_TARGET}>/shaders/pick_id_visualize.frag.spv"
  COMMENT "Compile engine light viewport gizmo + pbr_forward + pick_id shaders"
  VERBATIM
)

