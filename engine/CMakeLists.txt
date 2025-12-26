set(ENGINE_TARGET engine)

file(
    GLOB_RECURSE ENGINE_SOURCES
    CONFIGURE_DEPENDS
   ./src/*.cpp
)

add_library(${ENGINE_TARGET} STATIC ${ENGINE_SOURCES})

target_include_directories(${ENGINE_TARGET} PUBLIC include)

target_link_libraries(${ENGINE_TARGET} PUBLIC Vulkan::Vulkan)
target_link_libraries(${ENGINE_TARGET} PUBLIC SDL3::SDL3)
target_link_libraries(${ENGINE_TARGET} PUBLIC $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>)
target_link_libraries(${ENGINE_TARGET} PUBLIC SDL3_ttf::SDL3_ttf)
target_link_libraries(${ENGINE_TARGET} PUBLIC glm::glm)
target_link_libraries(${ENGINE_TARGET} PUBLIC $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>)
target_link_libraries(${ENGINE_TARGET} PUBLIC spdlog::spdlog)
target_link_libraries(${ENGINE_TARGET} PUBLIC slang::slang)
target_link_libraries(${ENGINE_TARGET} PUBLIC glslang::glslang glslang::glslang-default-resource-limits glslang::SPIRV glslang::SPVRemapper)
target_include_directories(${ENGINE_TARGET} PUBLIC ${TINYGLTF_INCLUDE_DIRS})
target_link_libraries(${ENGINE_TARGET} PUBLIC tinyobjloader::tinyobjloader_double)
target_include_directories(${ENGINE_TARGET} PUBLIC ${Stb_INCLUDE_DIR})
target_include_directories(${ENGINE_TARGET} PUBLIC imgui::imgui)
target_include_directories(${ENGINE_TARGET} PUBLIC imguizmo::imguizmo)
target_link_libraries(${ENGINE_TARGET} PRIVATE cpr::cpr)

target_precompile_headers(${ENGINE_TARGET} PUBLIC ./include/pch.hpp)



