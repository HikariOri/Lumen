cmake_minimum_required(VERSION 3.31.0)

project(
    LearnVulkan
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "Learn Vulkan"
) 

include(cmake/FindExternal.cmake)
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

find_package(Doxygen OPTIONAL_COMPONENTS dot)

if (Doxygen_FOUND)
    set(MY_DOXYFILE ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile)

    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${MY_DOXYFILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating documentation with Doxygen"
        VERBATIM
    )
else()
    message(WARNING "Doxygen not found — documentation target will be skipped")
endif()

find_program(SLANGC_COMPILER slangc REQUIRED)
find_program(GLSLC_COMPILER glslc REQUIRED)

include(CTest)
enable_testing()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(third_party)

add_subdirectory(engine)
# add_subdirectory(examples)
add_subdirectory(test)
