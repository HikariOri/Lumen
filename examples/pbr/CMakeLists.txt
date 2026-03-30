add_executable(pbr
  src/main.cpp
  src/ibl_bake.cpp
)

target_link_libraries(pbr PRIVATE engine)

target_precompile_headers(pbr REUSE_FROM engine)

set(PBR_SPV_DIR "$<TARGET_FILE_DIR:pbr>/shaders")
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
    TARGET pbr
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PBR_SPV_DIR}"
    COMMAND ${GLSLC_COMPILER}
            "${CMAKE_CURRENT_LIST_DIR}/shaders/${_shader}"
            -o "${PBR_SPV_DIR}/${_shader}.spv"
    COMMENT "Compile ${_shader} for pbr IBL"
    VERBATIM
  )
endforeach()

add_custom_command(
  TARGET pbr
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:pbr>/assets"
  VERBATIM
)

if(WIN32 AND TARGET ktx)
  add_custom_command(
    TARGET pbr
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ktx>
            $<TARGET_FILE_DIR:pbr>
    COMMENT "Copy libktx DLL next to pbr (Windows shared build)"
    VERBATIM
  )
endif()
