add_executable(shader_reflect src/main.cpp)

target_link_libraries(shader_reflect PRIVATE engine)

target_precompile_headers(shader_reflect REUSE_FROM engine)

add_custom_command(
  TARGET shader_reflect
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:shader_reflect>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/shader_reflect.vert"
          -o "$<TARGET_FILE_DIR:shader_reflect>/shaders/shader_reflect.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/shader_reflect.frag"
          -o "$<TARGET_FILE_DIR:shader_reflect>/shaders/shader_reflect.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:shader_reflect>/assets/textures"
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_SOURCE_DIR}/assets/textures/ikun2026_happy_new_year.jpg"
          "$<TARGET_FILE_DIR:shader_reflect>/assets/textures/ikun2026_happy_new_year.jpg"
  COMMENT "Compile GLSL shaders for shader_reflect example and copy demo texture"
  VERBATIM
)

