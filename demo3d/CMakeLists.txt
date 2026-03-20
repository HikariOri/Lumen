add_executable(demo3d src/main.cpp)

target_link_libraries(demo3d PRIVATE engine)

target_precompile_headers(demo3d REUSE_FROM engine)

add_custom_command(
  TARGET demo3d
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:demo3d>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/cube.vert"
          -o "$<TARGET_FILE_DIR:demo3d>/shaders/cube.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/cube.frag"
          -o "$<TARGET_FILE_DIR:demo3d>/shaders/cube.frag.spv"
  COMMENT "Compiling 3D cube GLSL shaders"
  VERBATIM
)

add_custom_command(
  TARGET demo3d
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:demo3d>/assets"
  VERBATIM
)
