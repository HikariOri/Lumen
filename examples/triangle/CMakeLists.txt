add_executable(triangle src/main.cpp)

target_link_libraries(triangle PRIVATE engine)

target_precompile_headers(triangle REUSE_FROM engine)

add_custom_command(
  TARGET triangle
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:triangle>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/triangle.vert"
          -o "$<TARGET_FILE_DIR:triangle>/shaders/triangle.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/triangle.frag"
          -o "$<TARGET_FILE_DIR:triangle>/shaders/triangle.frag.spv"
  COMMENT "Compile minimal triangle GLSL shaders"
  VERBATIM
)

