set(CUR_TARGET 020_staging_buffer)
add_executable(${CUR_TARGET} ./src/main.cpp)
target_include_directories(${CUR_TARGET} PUBLIC include)
target_link_libraries(${CUR_TARGET} PRIVATE glfw)
target_link_libraries(${CUR_TARGET} PRIVATE glm::glm)
target_link_libraries(${CUR_TARGET} PRIVATE Vulkan::Vulkan)
target_link_libraries(${CUR_TARGET} PRIVATE volk::volk volk::volk_headers)
target_link_libraries(${CUR_TARGET} PRIVATE slang::slang)
target_link_libraries(${CUR_TARGET} PRIVATE tabulate::tabulate)
target_link_libraries(${CUR_TARGET} PRIVATE imgui::imgui)
target_link_libraries(${CUR_TARGET} PRIVATE quill::quill)
target_include_directories(${CUR_TARGET} PRIVATE ${Stb_INCLUDE_DIR})


