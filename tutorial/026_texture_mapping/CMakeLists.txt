set(CUR_TARGET 026_texture_mapping)
add_executable(${CUR_TARGET} ./src/main.cpp)
target_include_directories(${CUR_TARGET} PUBLIC include)
target_link_libraries(${CUR_TARGET} PRIVATE glfw)
target_link_libraries(${CUR_TARGET} PRIVATE glm::glm)
target_link_libraries(${CUR_TARGET} PRIVATE Vulkan::Vulkan)
target_include_directories(${CUR_TARGET} PRIVATE ${Stb_INCLUDE_DIR})

add_custom_command(
        TARGET ${CUR_TARGET}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/slang"
        COMMAND ${SLANG_COMPILER}
                "${PROJECT_SOURCE_DIR}/shaders/slang/026_shader_textures.slang"
                -target spirv -profile spirv_1_4
                -emit-spirv-directly -fvk-use-entrypoint-name
                -entry vertMain -entry fragMain
                -o "$<TARGET_FILE_DIR:${CUR_TARGET}>/shaders/slang/026_shader_textures.spv"
        COMMENT "Ensuring shader output directory exists and compiling Slang shader"
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
