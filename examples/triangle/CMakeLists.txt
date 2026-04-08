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
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:triangle>/assets/textures"
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_SOURCE_DIR}/assets/textures/ikun2026_happy_new_year.jpg"
          "$<TARGET_FILE_DIR:triangle>/assets/textures/ikun2026_happy_new_year.jpg"
  COMMENT "Compile GLSL shaders for triangle example and copy demo texture"
  VERBATIM
)

