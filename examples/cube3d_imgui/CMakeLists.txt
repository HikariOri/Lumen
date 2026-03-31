add_executable(cube3d_imgui src/main.cpp)

target_link_libraries(cube3d_imgui PRIVATE engine)

target_precompile_headers(cube3d_imgui REUSE_FROM engine)

add_custom_command(
  TARGET cube3d_imgui
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:cube3d_imgui>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/cube3d_imgui.vert"
          -o "$<TARGET_FILE_DIR:cube3d_imgui>/shaders/cube3d_imgui.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/cube3d_imgui.frag"
          -o "$<TARGET_FILE_DIR:cube3d_imgui>/shaders/cube3d_imgui.frag.spv"
  COMMENT "Compile cube3d_imgui GLSL shaders"
  VERBATIM
)

add_custom_command(
  TARGET cube3d_imgui
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:cube3d_imgui>/assets"
  VERBATIM
)

if(WIN32 AND TARGET ktx)
  add_custom_command(
    TARGET cube3d_imgui
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ktx>
            $<TARGET_FILE_DIR:cube3d_imgui>
    COMMENT "Copy libktx DLL next to cube3d_imgui (Windows shared build)"
    VERBATIM
  )
endif()
