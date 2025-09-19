set(ENGINE_TARGET engine)

file(
    GLOB_RECURSE ENGINE_SOURCES
    CONFIGURE_DEPENDS
   ./src/**/*.cpp
)

add_library(${ENGINE_TARGET} STATIC ${ENGINE_SOURCES})

# add_executable(${CUR_TARGET} ./src/main.cpp)
target_include_directories(${ENGINE_TARGET} PUBLIC include)
target_link_libraries(${ENGINE_TARGET} PUBLIC glfw)
target_link_libraries(${ENGINE_TARGET} PUBLIC glm::glm)
target_link_libraries(${ENGINE_TARGET} PUBLIC Vulkan::Vulkan)
target_link_libraries(${ENGINE_TARGET} PUBLIC $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>)
target_link_libraries(${ENGINE_TARGET} PRIVATE SDL3::SDL3)
target_include_directories(${ENGINE_TARGET} PUBLIC ${Stb_INCLUDE_DIR})




