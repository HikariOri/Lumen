# Triangle 的 .spv 仅在其 POST_BUILD 中生成到可执行目录，源码树无 .spv；测试在构建目录单独编译一份。
set(_test_triangle_shader_src "${CMAKE_SOURCE_DIR}/examples/triangle/shaders")
set(_test_triangle_shader_out "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(_test_triangle_vert_spv "${_test_triangle_shader_out}/triangle.vert.spv")
set(_test_triangle_frag_spv "${_test_triangle_shader_out}/triangle.frag.spv")

add_custom_command(
  OUTPUT ${_test_triangle_vert_spv} ${_test_triangle_frag_spv}
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_test_triangle_shader_out}"
  COMMAND ${GLSLC_COMPILER} "${_test_triangle_shader_src}/triangle.vert"
          -o "${_test_triangle_vert_spv}"
  COMMAND ${GLSLC_COMPILER} "${_test_triangle_shader_src}/triangle.frag"
          -o "${_test_triangle_frag_spv}"
  DEPENDS "${_test_triangle_shader_src}/triangle.vert"
          "${_test_triangle_shader_src}/triangle.frag"
  VERBATIM)

add_custom_target(
  test_shader_reflection_spv
  DEPENDS ${_test_triangle_vert_spv} ${_test_triangle_frag_spv})

add_executable(test_shader_reflection test_shader_reflection.cpp)
add_dependencies(test_shader_reflection test_shader_reflection_spv)
target_link_libraries(test_shader_reflection PRIVATE engine doctest::doctest)
target_compile_definitions(
  test_shader_reflection
  PRIVATE
    "LUMEN_TEST_TRIANGLE_SHADERS_DIR=\"${_test_triangle_shader_out}\"")
add_test(NAME shader_reflection COMMAND test_shader_reflection)

add_executable(test_render_graph test_render_graph.cpp)
target_link_libraries(test_render_graph PRIVATE engine doctest::doctest)
add_test(NAME render_graph COMMAND test_render_graph)

add_executable(test_pipeline_key test_pipeline_key.cpp)
target_link_libraries(test_pipeline_key PRIVATE engine doctest::doctest)
add_test(NAME pipeline_key COMMAND test_pipeline_key)
