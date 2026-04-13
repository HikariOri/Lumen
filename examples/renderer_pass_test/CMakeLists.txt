add_executable(renderer_pass_test src/main.cpp)

target_link_libraries(renderer_pass_test PRIVATE engine)

target_precompile_headers(renderer_pass_test REUSE_FROM engine)

add_custom_command(
  TARGET renderer_pass_test
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:renderer_pass_test>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/renderer_pass_test.vert"
          -o "$<TARGET_FILE_DIR:renderer_pass_test>/shaders/renderer_pass_test.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/renderer_pass_test.frag"
          -o "$<TARGET_FILE_DIR:renderer_pass_test>/shaders/renderer_pass_test.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/pass1_shadertoy.vert"
          -o "$<TARGET_FILE_DIR:renderer_pass_test>/shaders/pass1_shadertoy.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/pass1_shadertoy.frag"
          -o "$<TARGET_FILE_DIR:renderer_pass_test>/shaders/pass1_shadertoy.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:renderer_pass_test>/assets/textures"
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_SOURCE_DIR}/assets/textures/ikun2026_happy_new_year.jpg"
          "$<TARGET_FILE_DIR:renderer_pass_test>/assets/textures/ikun2026_happy_new_year.jpg"
  COMMENT "Compile GLSL shaders for renderer_pass_test example and copy demo texture"
  VERBATIM
)

