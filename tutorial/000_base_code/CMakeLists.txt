add_executable(000_base_code ./src/main.cpp)
target_include_directories(000_base_code PUBLIC include)
target_link_libraries(000_base_code PRIVATE glfw)
target_link_libraries(000_base_code PRIVATE glm::glm)
target_link_libraries(000_base_code PRIVATE Vulkan::Vulkan)


