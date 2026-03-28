add_executable(cube3d src/main.cpp)

target_link_libraries(cube3d PRIVATE engine)

target_precompile_headers(cube3d REUSE_FROM engine)

add_custom_command(
  TARGET cube3d
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:cube3d>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/cube3d.vert"
          -o "$<TARGET_FILE_DIR:cube3d>/shaders/cube3d.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/cube3d.frag"
          -o "$<TARGET_FILE_DIR:cube3d>/shaders/cube3d.frag.spv"
  COMMENT "Compile cube3d GLSL shaders"
  VERBATIM
)

add_custom_command(
  TARGET cube3d
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:cube3d>/assets"
  VERBATIM
)

if(WIN32 AND TARGET ktx)
  add_custom_command(
    TARGET cube3d
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ktx>
            $<TARGET_FILE_DIR:cube3d>
    COMMENT "Copy libktx DLL next to cube3d (Windows shared build)"
    VERBATIM
  )
endif()
