add_executable(example_1 1.cpp)
target_include_directories(example_1 PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_executable(example_2 2.cpp)
target_include_directories(example_2 PRIVATE ${PROJECT_SOURCE_DIR}/include)
