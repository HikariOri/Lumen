set(CUR_TARGET sdl_cpp_028_model_loading)
add_executable(${CUR_TARGET} ./src/main.cpp)
target_include_directories(${CUR_TARGET} PUBLIC include)
target_link_libraries(${CUR_TARGET} PRIVATE SDL3::SDL3)
target_link_libraries(${CUR_TARGET} PRIVATE $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>)
target_link_libraries(${CUR_TARGET} PRIVATE quill::quill)
target_link_libraries(${CUR_TARGET} PRIVATE glm::glm)
target_link_libraries(${CUR_TARGET} PRIVATE Vulkan::Vulkan)
target_link_libraries(${CUR_TARGET} PRIVATE volk::volk volk::volk_headers)
target_link_libraries(${CUR_TARGET} PRIVATE slang::slang)
target_link_libraries(${CUR_TARGET} PRIVATE tabulate::tabulate)
target_include_directories(${CUR_TARGET} PRIVATE ${Stb_INCLUDE_DIR})
target_include_directories(${CUR_TARGET} PRIVATE ${TINYGLTF_INCLUDE_DIRS})
target_link_libraries(${CUR_TARGET} PRIVATE tinyobjloader::tinyobjloader_double)

add_custom_command(
  TARGET ${CUR_TARGET}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/glsl/shader.vert"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl/vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/glsl/shader.frag"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl/frag.spv"
  COMMENT "Ensuring shader output directory exists and compiling GLSL shader"
  VERBATIM
)

# Copy assets files after build.
add_custom_command(
        TARGET ${CUR_TARGET}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:${CUR_TARGET}>/assets"
        VERBATIM
)
