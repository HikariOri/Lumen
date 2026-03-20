add_executable(sandbox src/main.cpp)

target_link_libraries(sandbox PRIVATE engine)

target_precompile_headers(sandbox REUSE_FROM engine)

add_custom_command(
  TARGET sandbox
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:sandbox>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/triangle.vert"
          -o "$<TARGET_FILE_DIR:sandbox>/shaders/triangle.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/triangle.frag"
          -o "$<TARGET_FILE_DIR:sandbox>/shaders/triangle.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/texture.vert"
          -o "$<TARGET_FILE_DIR:sandbox>/shaders/texture.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/texture.frag"
          -o "$<TARGET_FILE_DIR:sandbox>/shaders/texture.frag.spv"
  COMMENT "Ensuring shader output directory exists and compiling GLSL shaders"
  VERBATIM
)


# Copy assets files after build.
add_custom_command(
        TARGET sandbox
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:sandbox>/assets"
        VERBATIM
)
