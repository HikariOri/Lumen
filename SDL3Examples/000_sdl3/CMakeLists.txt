set(CUR_TARGET 000_sdl3)
add_executable(${CUR_TARGET} ./src/main.cpp)
target_include_directories(${CUR_TARGET} PUBLIC include)
target_link_libraries(${CUR_TARGET} PRIVATE SDL3::SDL3)
target_link_libraries(${CUR_TARGET} PRIVATE $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>)
target_link_libraries(${CUR_TARGET} PRIVATE glm::glm)
target_link_libraries(${CUR_TARGET} PRIVATE slang::slang)
target_link_libraries(${CUR_TARGET} PRIVATE imgui::imgui)
target_link_libraries(${CUR_TARGET} PRIVATE imguizmo::imguizmo)
target_link_libraries(${CUR_TARGET}  PRIVATE unofficial::imgui-node-editor::imgui-node-editor)
target_link_libraries(${CUR_TARGET} PRIVATE ${BULLET_LIBRARIES})
target_link_libraries(${CUR_TARGET} PRIVATE BulletSoftBody)
target_include_directories(${CUR_TARGET} PRIVATE ${Stb_INCLUDE_DIR})

# Copy assets files after build.
add_custom_command(
        TARGET ${CUR_TARGET}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:${CUR_TARGET}>/assets"
        VERBATIM
)
