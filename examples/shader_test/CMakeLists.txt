add_executable(shader_test src/main.cpp)

target_include_directories(shader_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")

target_link_libraries(shader_test PRIVATE engine)

target_precompile_headers(shader_test REUSE_FROM engine)

add_dependencies(shader_test engine)

add_custom_command(
  TARGET shader_test
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:shader_test>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/cube.vert"
          -o "$<TARGET_FILE_DIR:shader_test>/shaders/cube.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/cube.frag"
          -o "$<TARGET_FILE_DIR:shader_test>/shaders/cube.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/skybox.vert"
          -o "$<TARGET_FILE_DIR:shader_test>/shaders/skybox.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/skybox.frag"
          -o "$<TARGET_FILE_DIR:shader_test>/shaders/skybox.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/grid.vert"
          -o "$<TARGET_FILE_DIR:shader_test>/shaders/grid.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/grid.frag"
          -o "$<TARGET_FILE_DIR:shader_test>/shaders/grid.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE_DIR:engine>/shaders/light_icon.vert.spv"
          "$<TARGET_FILE_DIR:shader_test>/shaders/light_icon.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE_DIR:engine>/shaders/light_icon.frag.spv"
          "$<TARGET_FILE_DIR:shader_test>/shaders/light_icon.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE_DIR:engine>/shaders/light_debug.vert.spv"
          "$<TARGET_FILE_DIR:shader_test>/shaders/light_debug.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE_DIR:engine>/shaders/light_debug.frag.spv"
          "$<TARGET_FILE_DIR:shader_test>/shaders/light_debug.frag.spv"
  COMMENT "Compiling 3D cube GLSL shaders; copy engine light gizmo SPIR-V"
  VERBATIM
)

add_custom_command(
  TARGET shader_test
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:shader_test>/assets"
  VERBATIM
)

if(WIN32 AND TARGET ktx)
  add_custom_command(
    TARGET shader_test
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ktx>
            $<TARGET_FILE_DIR:shader_test>
    COMMENT "Copy libktx DLL next to shader_test (Windows shared build)"
    VERBATIM
  )
endif()
