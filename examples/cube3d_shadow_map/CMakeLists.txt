add_executable(cube3d_shadow_map src/main.cpp)

target_link_libraries(cube3d_shadow_map PRIVATE engine)

target_precompile_headers(cube3d_shadow_map REUSE_FROM engine)

add_custom_command(
  TARGET cube3d_shadow_map
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:cube3d_shadow_map>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/cube3d_shadow_map.vert"
          -o "$<TARGET_FILE_DIR:cube3d_shadow_map>/shaders/cube3d_shadow_map.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/cube3d_shadow_map.frag"
          -o "$<TARGET_FILE_DIR:cube3d_shadow_map>/shaders/cube3d_shadow_map.frag.spv"
  COMMENT "Compile cube3d_shadow_map GLSL shaders"
  VERBATIM
)

add_custom_command(
  TARGET cube3d_shadow_map
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:cube3d_shadow_map>/assets"
  VERBATIM
)

if(WIN32 AND TARGET ktx)
  add_custom_command(
    TARGET cube3d_shadow_map
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ktx>
            $<TARGET_FILE_DIR:cube3d_shadow_map>
    COMMENT "Copy libktx DLL next to cube3d_shadow_map (Windows shared build)"
    VERBATIM
  )
endif()
