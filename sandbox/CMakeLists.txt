add_executable(sandbox src/main.cpp)

target_link_libraries(sandbox PRIVATE engine)

target_precompile_headers(sandbox REUSE_FROM engine)
