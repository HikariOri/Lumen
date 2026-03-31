add_executable(rectangle_indexed_uniform src/main.cpp)

target_link_libraries(rectangle_indexed_uniform PRIVATE engine)

target_precompile_headers(rectangle_indexed_uniform REUSE_FROM engine)

add_custom_command(
  TARGET rectangle_indexed_uniform 
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:rectangle_indexed_uniform>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/rectangle_indexed_uniform.vert"
          -o "$<TARGET_FILE_DIR:rectangle_indexed_uniform>/shaders/rectangle_indexed_uniform.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/rectangle_indexed_uniform.frag"
          -o "$<TARGET_FILE_DIR:rectangle_indexed_uniform>/shaders/rectangle_indexed_uniform.frag.spv"
  COMMENT "Compile minimal rectangle_indexed_uniform GLSL shaders"
  VERBATIM
)

add_custom_command(
  TARGET rectangle_indexed_uniform
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:rectangle_indexed_uniform>/assets"
  VERBATIM
)

if(WIN32 AND TARGET ktx)
  add_custom_command(
    TARGET rectangle_indexed_uniform
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ktx>
            $<TARGET_FILE_DIR:rectangle_indexed_uniform>
    COMMENT "Copy libktx DLL next to rectangle_indexed_uniform (Windows shared build)"
    VERBATIM
  )
endif()


