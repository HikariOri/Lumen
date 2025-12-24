set(CUR_TARGET test_sqlite3)
add_executable(${CUR_TARGET} ./src/main.cpp)
target_include_directories(${CUR_TARGET} PUBLIC include)

target_link_libraries(${CUR_TARGET} PRIVATE unofficial::sqlite3::sqlite3)
target_link_libraries(${CUR_TARGET} PRIVATE sqlite_orm::sqlite_orm)
