add_executable(shadertoy src/main.cpp)

target_link_libraries(shadertoy PRIVATE engine)

target_precompile_headers(shadertoy REUSE_FROM engine)

add_custom_command(
  TARGET shadertoy
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:shadertoy>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/shadertoy.vert"
          -o "$<TARGET_FILE_DIR:shadertoy>/shaders/shadertoy.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/shadertoy.frag"
          -o "$<TARGET_FILE_DIR:shadertoy>/shaders/shadertoy.frag.spv"
  COMMENT "Compiling Shadertoy GLSL shaders"
  VERBATIM
)
