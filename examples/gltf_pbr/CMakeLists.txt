add_executable(gltf_pbr src/main.cpp)

target_link_libraries(gltf_pbr PRIVATE engine)

target_precompile_headers(gltf_pbr REUSE_FROM engine)

add_dependencies(gltf_pbr engine)

add_custom_command(
  TARGET gltf_pbr
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:gltf_pbr>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/pbr.vert"
          -o "$<TARGET_FILE_DIR:gltf_pbr>/shaders/pbr.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/pbr.frag"
          -o "$<TARGET_FILE_DIR:gltf_pbr>/shaders/pbr.frag.spv"
  COMMENT "Compile gltf_pbr PBR shaders"
  VERBATIM
)

add_custom_command(
  TARGET gltf_pbr
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:gltf_pbr>/assets"
  VERBATIM
)
