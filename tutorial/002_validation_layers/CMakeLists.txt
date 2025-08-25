add_executable(002_validation_layers ./src/main.cpp)
target_include_directories(002_validation_layers PUBLIC include)
target_link_libraries(002_validation_layers PRIVATE glfw)
target_link_libraries(002_validation_layers PRIVATE glm::glm)
target_link_libraries(002_validation_layers PRIVATE Vulkan::Vulkan)


