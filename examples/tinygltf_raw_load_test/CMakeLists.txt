add_executable(tinygltf_raw_load_test src/main.cpp src/ibl_cpu.cpp)

target_link_libraries(tinygltf_raw_load_test PRIVATE engine)

target_precompile_headers(tinygltf_raw_load_test REUSE_FROM engine)

target_compile_features(tinygltf_raw_load_test PRIVATE cxx_std_23)

add_dependencies(tinygltf_raw_load_test engine)

add_custom_command(
  TARGET tinygltf_raw_load_test
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:tinygltf_raw_load_test>/shaders"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/gltf_pbr.vert"
          -o "$<TARGET_FILE_DIR:tinygltf_raw_load_test>/shaders/gltf_pbr.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/gltf_pbr.frag"
          -o "$<TARGET_FILE_DIR:tinygltf_raw_load_test>/shaders/gltf_pbr.frag.spv"
  COMMENT "Compile tinygltf_raw_load_test PBR GLSL shaders"
  VERBATIM
)

add_custom_command(
  TARGET tinygltf_raw_load_test
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:tinygltf_raw_load_test>/assets"
  VERBATIM
)

if(WIN32 AND TARGET ktx)
  add_custom_command(
    TARGET tinygltf_raw_load_test
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ktx>
            $<TARGET_FILE_DIR:tinygltf_raw_load_test>
    COMMENT "Copy libktx DLL next to tinygltf_raw_load_test (Windows shared build)"
    VERBATIM
  )
endif()

