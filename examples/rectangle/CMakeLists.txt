add_executable(rectangle src/main.cpp)

target_link_libraries(rectangle PRIVATE engine)

target_precompile_headers(rectangle REUSE_FROM engine)

add_custom_command(
  TARGET rectangle
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:rectangle>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/rectangle.vert"
          -o "$<TARGET_FILE_DIR:rectangle>/shaders/rectangle.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/rectangle.frag"
          -o "$<TARGET_FILE_DIR:rectangle>/shaders/rectangle.frag.spv"
  COMMENT "Compile minimal rectangle GLSL shaders"
  VERBATIM
)

add_custom_command(
  TARGET rectangle
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:rectangle>/assets"
  VERBATIM
)

