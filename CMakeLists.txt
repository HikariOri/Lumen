cmake_minimum_required(VERSION 3.31.0)

project(
    LearnVulkan
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "Learn Vulkan"
)

include(cmake/FindVlukan.cmake)
include(cmake/FindSpdlog.cmake)
include(cmake/FindGLFW.cmake)
include(cmake/FindGLM.cmake) 
include(cmake/FindSTB.cmake) 
include(cmake/FindSDL3.cmake)
include(cmake/FindSlang.cmake) 
include(cmake/FindGlslang.cmake) 
include(cmake/FindFlecs.cmake) 
include(cmake/FindFlecsTinygltf.cmake) 
include(cmake/FindTinyobjloader.cmake) 
include(cmake/FindImgui.cmake) 
include(cmake/FindBullet3.cmake) 
include(cmake/FindTabulate.cmake) 
include(cmake/utils.cmake) 

# set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-p=${CMAKE_BINARY_DIR};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy")

if (WIN32)
    add_compile_definitions(NOMINMAX _USE_MATH_DEFINES)
endif ()
if (MSVC)
    add_compile_options(-Zc:preprocessor -utf-8 -EHsc -bigobj)
else ()
    if (WIN32)
        add_compile_options(-finput-charset=utf-8 -fexec-charset=utf-8)
    endif ()
    add_compile_options(-Wall -Wextra -Werror=return-type)
endif ()

find_program(SLANG_COMPILER slangc REQUIRED)

include(CTest)
enable_testing()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(VulkanTutorial)
add_subdirectory(SDL3Examples)
add_subdirectory(shaders)
add_subdirectory(engine)
