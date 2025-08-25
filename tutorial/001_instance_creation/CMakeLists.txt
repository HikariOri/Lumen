add_executable(001_instance_creation ./src/main.cpp)
target_include_directories(001_instance_creation PUBLIC include)
target_link_libraries(001_instance_creation PRIVATE glfw)
target_link_libraries(001_instance_creation PRIVATE glm::glm)
target_link_libraries(001_instance_creation PRIVATE Vulkan::Vulkan)


