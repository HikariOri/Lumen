add_executable(blinn_phong src/main.cpp)

target_link_libraries(blinn_phong PRIVATE engine)

target_precompile_headers(blinn_phong REUSE_FROM engine)

add_custom_command(
  TARGET blinn_phong
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:blinn_phong>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/blinn_phong.vert"
          -o "$<TARGET_FILE_DIR:blinn_phong>/shaders/blinn_phong.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/blinn_phong.frag"
          -o "$<TARGET_FILE_DIR:blinn_phong>/shaders/blinn_phong.frag.spv"
  COMMENT "Compile blinn_phong GLSL shaders"
  VERBATIM
)

add_custom_command(
  TARGET blinn_phong
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:blinn_phong>/assets"
  VERBATIM
)

if(WIN32 AND TARGET ktx)
  add_custom_command(
    TARGET blinn_phong
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ktx>
            $<TARGET_FILE_DIR:blinn_phong>
    COMMENT "Copy libktx DLL next to blinn_phong (Windows shared build)"
    VERBATIM
  )
endif()
