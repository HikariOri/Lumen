# =============================================================================
# compile_shaders_for_target(<TGT> [EXTENSIONS <list-of-exts>])
#
# 自动遍历 shaders 目录中的 shader 文件并为指定目标生成 glslc 编译规则
#
# <TGT>            : CMake target name
# EXTENSIONS       : 指定 shader 扩展名（默认 vert frag comp）
#
# 输出为: $<TARGET_FILE_DIR:TGT>/shaders/*.spv
# =============================================================================

function(compile_shaders_for_target TGT)
    # 默认扩展名列表
    set(_exts "vert" "frag" "comp" "tesc" "tese" "geom" "glsl")
    if(ARGN)
        set(_exts ${ARGN})
    endif()

    # 找到 glslc 编译器
    find_program(GLSLC_COMPILER glslc REQUIRED)

    # 遍历所有扩展名
    foreach(_e IN LISTS _exts)
        file(GLOB_RECURSE _shader_files
            "${CMAKE_CURRENT_SOURCE_DIR}/*.${_e}"
        )
        foreach(_src IN LISTS _shader_files)
            get_filename_component(_f ${_src} NAME)

            # 输出 SPIR-V 到目标输出目录下 shaders 子目录
            set(_out "$<TARGET_FILE_DIR:${TGT}>/shaders/${_f}.spv")

            # 添加 POST_BUILD 自定义命令
            add_custom_command(
                TARGET ${TGT}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${TGT}>/shaders"
                COMMAND ${GLSLC_COMPILER} -o ${_out} ${_src}
                COMMENT "Compiling shader ${_f} for target ${TGT}"
                VERBATIM
            )
        endforeach()
    endforeach()
endfunction()
