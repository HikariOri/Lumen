add_executable(005_window_surface ./src/main.cpp)
target_include_directories(005_window_surface PUBLIC include)
target_link_libraries(005_window_surface PRIVATE glfw)
target_link_libraries(005_window_surface PRIVATE glm::glm)
target_link_libraries(005_window_surface PRIVATE Vulkan::Vulkan)


