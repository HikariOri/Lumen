add_executable(two_subpass_merge_test src/main.cpp)

target_link_libraries(two_subpass_merge_test PRIVATE engine)

target_precompile_headers(two_subpass_merge_test REUSE_FROM engine)

add_custom_command(
  TARGET two_subpass_merge_test
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:two_subpass_merge_test>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/pass1_mrt.vert"
          -o "$<TARGET_FILE_DIR:two_subpass_merge_test>/shaders/pass1_mrt.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/pass1_mrt.frag"
          -o "$<TARGET_FILE_DIR:two_subpass_merge_test>/shaders/pass1_mrt.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/pass2_comp.vert"
          -o "$<TARGET_FILE_DIR:two_subpass_merge_test>/shaders/pass2_comp.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/pass2_comp.frag"
          -o "$<TARGET_FILE_DIR:two_subpass_merge_test>/shaders/pass2_comp.frag.spv"
  COMMENT "Compile GLSL shaders for two_subpass_merge_test example"
  VERBATIM
)
