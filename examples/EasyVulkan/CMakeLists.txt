set(CUR_TARGET EasyVulkan)
add_executable(${CUR_TARGET} ./src/main.cpp)
target_include_directories(${CUR_TARGET} PUBLIC include)
target_link_libraries(${CUR_TARGET} PRIVATE glm::glm)
target_link_libraries(${CUR_TARGET} PRIVATE Vulkan::Vulkan)
target_link_libraries(${CUR_TARGET} PRIVATE slang::slang)
target_include_directories(${CUR_TARGET} PRIVATE ${Stb_INCLUDE_DIR})
target_include_directories(${CUR_TARGET} PRIVATE ${TINYGLTF_INCLUDE_DIRS})
target_link_libraries(${CUR_TARGET} PRIVATE tinyobjloader::tinyobjloader_double)
target_link_libraries(${CUR_TARGET} PRIVATE glfw)
target_link_libraries(${CUR_TARGET} PRIVATE imgui::imgui)

add_custom_command(
  TARGET ${CUR_TARGET}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/glsl/FirstTriangle.vert.shader"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl/FirstTriangle.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/glsl/FirstTriangle.frag.shader"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl/FirstTriangle.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/glsl/VertexBuffer.vert.shader"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl/VertexBuffer.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/glsl/VertexBuffer.frag.shader"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl/VertexBuffer.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/glsl/Texture.vert.shader"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl/Texture.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/glsl/Texture.frag.shader"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl/Texture.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/glsl/Texture2DArray.vert.shader"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl/Texture2DArray.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/glsl/Texture2DArray.frag.shader"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl/Texture2DArray.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/glsl/InstancedRendering.vert.shader"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl/InstancedRendering.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/glsl/InstancedRendering.frag.shader"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl/InstancedRendering.frag.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/glsl/PushConstant.vert.shader"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl/PushConstant.vert.spv"
  COMMAND ${GLSLC_COMPILER}
          "${CMAKE_CURRENT_LIST_DIR}/shaders/glsl/PushConstant.frag.shader"
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/glsl/PushConstant.frag.spv"
  COMMENT "Ensuring shader output directory exists and compiling GLSL shader"
  VERBATIM
)



# Copy assets files after build.
add_custom_command(
        TARGET ${CUR_TARGET}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:${CUR_TARGET}>/assets"
        VERBATIM
)
