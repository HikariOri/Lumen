add_executable(sandbox src/main.cpp)

target_link_libraries(sandbox PRIVATE engine)

target_precompile_headers(sandbox REUSE_FROM engine)

add_custom_command(
  TARGET sandbox
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:sandbox>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/sandbox.vert"
          -o "$<TARGET_FILE_DIR:sandbox>/shaders/sandbox.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/sandbox.frag"
          -o "$<TARGET_FILE_DIR:sandbox>/shaders/sandbox.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:sandbox>/assets/textures"
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_SOURCE_DIR}/assets/textures/ikun2026_happy_new_year.jpg"
          "$<TARGET_FILE_DIR:sandbox>/assets/textures/ikun2026_happy_new_year.jpg"
  COMMENT "Compile GLSL shaders for sandbox example and copy demo texture"
  VERBATIM
)

