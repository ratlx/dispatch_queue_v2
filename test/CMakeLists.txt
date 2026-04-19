include(GoogleTest)

# task queue tests
add_executable(tq_tests
    mpsc_queue_test.cpp
    mpmc_queue_test.cpp
)
target_include_directories(tq_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tq_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(tq_tests)

# dispatch queue tests
add_executable(dq_tests
    dq_serial_test.cpp
)
target_include_directories(dq_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dq_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(dq_tests)