add_executable(triangle src/main.cpp)


target_link_libraries(triangle PUBLIC SDL3::SDL3)
target_link_libraries(triangle PUBLIC $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>)
target_link_libraries(triangle PUBLIC glm::glm)

add_custom_command(
  TARGET triangle
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:triangle>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/triangle.vert"
          -o "$<TARGET_FILE_DIR:triangle>/shaders/triangle.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/triangle.frag"
          -o "$<TARGET_FILE_DIR:triangle>/shaders/triangle.frag.spv"
  COMMENT "Compile GLSL shaders for SDL GPU triangle example"
  VERBATIM
)
