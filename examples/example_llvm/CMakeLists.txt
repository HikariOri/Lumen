set(CUR_TARGET example_llvm)
add_executable(${CUR_TARGET} ./src/main.cpp)
target_include_directories(${CUR_TARGET} PUBLIC include)

