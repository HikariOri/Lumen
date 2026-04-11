add_executable(pfr_test src/main.cpp)

target_link_libraries(pfr_test PRIVATE engine)

target_precompile_headers(pfr_test REUSE_FROM engine)

