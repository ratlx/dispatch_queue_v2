cmake_minimum_required(VERSION 3.22)
project(dispatch_queue_v3)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_TSAN AND ENABLE_ASAN)
    message(FATAL_ERROR "TSAN and ASAN cannot be enabled at the same time")
endif()

if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -g -O1)
    add_link_options(-fsanitize=thread)
endif()

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -g -O1)
    add_link_options(-fsanitize=address)
endif()

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_subdirectory(test)
add_subdirectory(example)

# clang-format target
find_program(CLANG_FORMAT "clang-format")
if(CLANG_FORMAT)
    file(GLOB_RECURSE FORMAT_SOURCES
        "include/*.h" "example/*.cpp"
        "test/*.cpp"
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${FORMAT_SOURCES}
        COMMENT "Running clang-format..."
    )
endif()