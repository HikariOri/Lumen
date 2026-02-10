set(CUR_TARGET EasyVulkan)
add_executable(${CUR_TARGET} ./src/main.cpp)
target_include_directories(${CUR_TARGET} PUBLIC include)
target_link_libraries(${CUR_TARGET} PRIVATE glm::glm)
target_link_libraries(${CUR_TARGET} PRIVATE Vulkan::Vulkan)
target_link_libraries(${CUR_TARGET} PRIVATE slang::slang)
target_include_directories(${CUR_TARGET} PRIVATE ${Stb_INCLUDE_DIR})
target_include_directories(${CUR_TARGET} PRIVATE ${TINYGLTF_INCLUDE_DIRS})
target_link_libraries(${CUR_TARGET} PRIVATE tinyobjloader::tinyobjloader_double)
target_link_libraries(${CUR_TARGET} PRIVATE glfw)

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
# add_custom_command(
#         TARGET ${CUR_TARGET}
#         POST_BUILD
#         COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
#                 "$<TARGET_FILE_DIR:${CUR_TARGET}>/assets"
#         VERBATIM
# )
