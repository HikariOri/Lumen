set(cur_target 018_vertex_input)
add_executable(${cur_target} ./src/main.cpp)
target_include_directories(${cur_target} PUBLIC include)
target_link_libraries(${cur_target} PRIVATE glfw)
target_link_libraries(${cur_target} PRIVATE glm::glm)
target_link_libraries(${cur_target} PRIVATE Vulkan::Vulkan)

add_custom_command(
  TARGET ${cur_target}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:${cur_target}>/shaders/slang"
  COMMAND ${SLANG_COMPILER}
          "${PROJECT_SOURCE_DIR}/shaders/slang/018_shader_vertexbuffer.slang"
          -target spirv -profile spirv_1_4
          -emit-spirv-directly -fvk-use-entrypoint-name
          -entry vertMain -entry fragMain
          -o "$<TARGET_FILE_DIR:${cur_target}>/shaders/slang/018_shader_vertexbuffer.spv"
  COMMENT "Ensuring shader output directory exists and compiling Slang shader"
  VERBATIM
)


