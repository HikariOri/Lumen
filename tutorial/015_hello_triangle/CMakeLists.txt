set(CUR_TARGET 015_hello_triangle)
add_executable(${CUR_TARGET} ./src/main.cpp)
target_include_directories(${CUR_TARGET} PUBLIC include)
target_link_libraries(${CUR_TARGET} PRIVATE glfw)
target_link_libraries(${CUR_TARGET} PRIVATE glm::glm)
target_link_libraries(${CUR_TARGET} PRIVATE Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
target_link_libraries(${CUR_TARGET} PRIVATE slang::slang)
target_include_directories(${CUR_TARGET} PRIVATE ${Stb_INCLUDE_DIR}) 

add_custom_command(
  TARGET ${CUR_TARGET}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/slang"
  COMMAND ${SLANG_COMPILER}
          "${PROJECT_SOURCE_DIR}/shaders/slang/shader.slang"
          -target spirv -profile spirv_1_4
          -emit-spirv-directly -fvk-use-entrypoint-name
          -entry vertMain -entry fragMain
          -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/slang/slang.spv"
  COMMENT "Ensuring shader output directory exists and compiling Slang shader"
  VERBATIM
)


