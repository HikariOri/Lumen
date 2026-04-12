add_executable(renderer_test src/main.cpp)

target_link_libraries(renderer_test PRIVATE engine)

target_precompile_headers(renderer_test REUSE_FROM engine)

add_custom_command(
  TARGET renderer_test
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:renderer_test>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/renderer_test.vert"
          -o "$<TARGET_FILE_DIR:renderer_test>/shaders/renderer_test.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/renderer_test.frag"
          -o "$<TARGET_FILE_DIR:renderer_test>/shaders/renderer_test.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:renderer_test>/assets/textures"
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_SOURCE_DIR}/assets/textures/ikun2026_happy_new_year.jpg"
          "$<TARGET_FILE_DIR:renderer_test>/assets/textures/ikun2026_happy_new_year.jpg"
  COMMENT "Compile GLSL shaders for renderer_test example and copy demo texture"
  VERBATIM
)

