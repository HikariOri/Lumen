add_executable(cube3d src/main.cpp)

target_link_libraries(cube3d PRIVATE engine)

target_precompile_headers(cube3d REUSE_FROM engine)

add_custom_command(
  TARGET cube3d
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:cube3d>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/cube3d.vert"
          -o "$<TARGET_FILE_DIR:cube3d>/shaders/cube3d.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/cube3d.frag"
          -o "$<TARGET_FILE_DIR:cube3d>/shaders/cube3d.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:cube3d>/assets/textures"
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_SOURCE_DIR}/assets/textures/ikun2026_happy_new_year.jpg"
          "$<TARGET_FILE_DIR:cube3d>/assets/textures/ikun2026_happy_new_year.jpg"
  COMMENT "Compile GLSL shaders for cube3d example and copy demo texture"
  VERBATIM
)

