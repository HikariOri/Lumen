set(CUR_TARGET example_sdlgpu)
add_executable(${CUR_TARGET} ./src/main.cpp)
target_include_directories(${CUR_TARGET} PUBLIC include)
target_link_libraries(${CUR_TARGET} PRIVATE SDL3::SDL3)
target_link_libraries(${CUR_TARGET} PRIVATE imgui::imgui)

add_custom_command(
  TARGET ${CUR_TARGET}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/shader.vert"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/shader.frag"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/frag.spv"
  COMMENT "Ensuring shader output directory exists and compiling GLSL shader"
  VERBATIM
)


