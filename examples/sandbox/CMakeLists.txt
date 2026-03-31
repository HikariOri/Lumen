add_executable(sandbox
  src/main.cpp
  src/ibl_bake.cpp
)

target_link_libraries(sandbox PRIVATE engine)

target_precompile_headers(sandbox REUSE_FROM engine)

add_dependencies(sandbox engine)

set(SANDBOX_SPV_DIR "$<TARGET_FILE_DIR:sandbox>/shaders")
foreach(_shader
  ibl_cube.vert
  ibl_irradiance.frag
  ibl_prefilter.vert
  ibl_prefilter.frag
  ibl_brdf.vert
  ibl_brdf.frag
  skybox.vert
  skybox.frag
)
  add_custom_command(
    TARGET sandbox
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SANDBOX_SPV_DIR}"
    COMMAND ${GLSLC_COMPILER}
            "${CMAKE_CURRENT_LIST_DIR}/shaders/${_shader}"
            -o "${SANDBOX_SPV_DIR}/${_shader}.spv"
    COMMENT "Compile ${_shader} for pbr IBL"
    VERBATIM
  )
endforeach()

add_custom_command(
  TARGET sandbox
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${SANDBOX_SPV_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE_DIR:engine>/shaders/pbr_forward.vert.spv"
          "${SANDBOX_SPV_DIR}/pbr_forward.vert.spv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE_DIR:engine>/shaders/pbr_forward.frag.spv"
          "${SANDBOX_SPV_DIR}/pbr_forward.frag.spv"
  COMMENT "Copy engine pbr_forward SPIR-V into sandbox"
  VERBATIM
)

add_custom_command(
  TARGET sandbox
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:sandbox>/assets"
  VERBATIM
)

if(WIN32 AND TARGET ktx)
  add_custom_command(
    TARGET sandbox
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ktx>
            $<TARGET_FILE_DIR:sandbox>
    COMMENT "Copy libktx DLL next to sandbox (Windows shared build)"
    VERBATIM
  )
endif()
