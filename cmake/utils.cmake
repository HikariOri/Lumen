# CMake工具函数集合
# 用于简化项目构建配置

# 功能：为目录中的所有.cpp文件创建可执行文件
# 参数：
#   - target_namespace: 可执行文件名称的命名空间前缀
#   - module_name: 模块名称，用于构建目标名称
# 示例：
#   create_executables_from_cpp_files("foundation" "stl")
#   会为stl目录中的每个.cpp文件创建 foundation_stl_<filename> 可执行文件
function(create_executables_from_cpp_files target_namespace module_name)
    file(GLOB cpp_sources *.cpp)

    if(cpp_sources)
        message(STATUS "Found ${CMAKE_CURRENT_SOURCE_DIR}: creating executables for ${target_namespace}_${module_name}")

        foreach(source_file ${cpp_sources})
            get_filename_component(executable_name ${source_file} NAME_WE)

            # 判断 module_name 是否为空，构造目标名称
            if(module_name STREQUAL "")
                set(full_target_name "${target_namespace}_${executable_name}")
            else()
                set(full_target_name "${target_namespace}_${module_name}_${executable_name}")
            endif()

            # 创建可执行文件
            add_executable(${full_target_name} ${source_file})

            # 链接通用库
            target_link_libraries(${full_target_name} PRIVATE common)

            # 重用预编译头文件
            target_precompile_headers(${full_target_name} REUSE_FROM common)

            message(STATUS "  -> Created target: ${full_target_name}")
        endforeach()
    else()
        message(STATUS "No .cpp files found in ${module_name} module, skipping")
    endif()
endfunction()


# 功能：为目录中的所有.cpp文件创建可执行文件（简化版本）
# 参数：
#   - module_name: 模块名称
# 默认使用 "app" 作为命名空间
function(create_executables module_name)
    create_executables_from_cpp_files("app" ${module_name})
endfunction() 

function (add_slang_shader_target TARGET)
  cmake_parse_arguments ("SHADER" "" "SOURCES" ${ARGN})
  set (SHADERS_DIR ${CMAKE_CURRENT_LIST_DIR}/shaders/)
  set (ENTRY_POINTS -entry vertMain -entry fragMain)
  add_custom_command (
          OUTPUT ${SHADERS_DIR}
          COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
  )
  add_custom_command (
          OUTPUT  ${SHADERS_DIR}/slang.spv
          COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -o slang.spv
          WORKING_DIRECTORY ${SHADERS_DIR}
          DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
          COMMENT "Compiling Slang Shaders"
          VERBATIM
  )
  add_custom_target (${TARGET} DEPENDS ${SHADERS_DIR}/slang.spv)
endfunction()
