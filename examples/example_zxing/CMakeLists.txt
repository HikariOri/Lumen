set(CUR_TARGET example_zxing)
add_executable(${CUR_TARGET} ./src/main.cpp)
target_include_directories(${CUR_TARGET} PUBLIC include)
target_link_libraries(${CUR_TARGET} PUBLIC ${OpenCV_LIBS})
target_link_libraries(${CUR_TARGET} PRIVATE ZXing::Core ZXing::ZXing)


