add_executable(gltf_pbr
  src/main.cpp
  src/ibl_bake.cpp
)

target_link_libraries(gltf_pbr PRIVATE engine)

target_precompile_headers(gltf_pbr REUSE_FROM engine)

set(GLTF_PBR_SPV_DIR "$<TARGET_FILE_DIR:gltf_pbr>/shaders")
foreach(_shader
  ibl_cube.vert
  ibl_irradiance.frag
  ibl_prefilter.vert
  ibl_prefilter.frag
  ibl_brdf.vert
  ibl_brdf.frag
  skybox.vert
  skybox.frag
  helmet_pbr.vert
  helmet_pbr.frag
)
  add_custom_command(
    TARGET gltf_pbr
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GLTF_PBR_SPV_DIR}"
    COMMAND ${GLSLC_COMPILER}
            "${CMAKE_CURRENT_LIST_DIR}/shaders/${_shader}"
            -o "${GLTF_PBR_SPV_DIR}/${_shader}.spv"
    COMMENT "Compile ${_shader} for pbr IBL"
    VERBATIM
  )
endforeach()

add_custom_command(
  TARGET gltf_pbr
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:gltf_pbr>/assets"
  VERBATIM
)

if(WIN32 AND TARGET ktx)
  add_custom_command(
    TARGET gltf_pbr
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ktx>
            $<TARGET_FILE_DIR:gltf_pbr>
    COMMENT "Copy libktx DLL next to pbr (Windows shared build)"
    VERBATIM
  )
endif()
