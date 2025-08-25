add_executable(004_logical_device ./src/main.cpp)
target_include_directories(004_logical_device PUBLIC include)
target_link_libraries(004_logical_device PRIVATE glfw)
target_link_libraries(004_logical_device PRIVATE glm::glm)
target_link_libraries(004_logical_device PRIVATE Vulkan::Vulkan)


