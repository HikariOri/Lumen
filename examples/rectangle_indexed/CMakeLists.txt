add_executable(rectangle_indexed src/main.cpp)

target_link_libraries(rectangle_indexed PRIVATE engine)

target_precompile_headers(rectangle_indexed REUSE_FROM engine)

add_custom_command(
  TARGET rectangle_indexed 
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:rectangle_indexed>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/rectangle_indexed.vert"
          -o "$<TARGET_FILE_DIR:rectangle_indexed>/shaders/rectangle_indexed.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/rectangle_indexed.frag"
          -o "$<TARGET_FILE_DIR:rectangle_indexed>/shaders/rectangle_indexed.frag.spv"
  COMMENT "Compile minimal rectangle_indexed GLSL shaders"
  VERBATIM
)

add_custom_command(
  TARGET rectangle_indexed
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:rectangle_indexed>/assets"
  VERBATIM
)

