cmake_minimum_required(VERSION 3.16)
project(onepage_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -g)
add_link_options(-fsanitize=address,undefined)
add_compile_definitions(_DEFAULT_SOURCE)

set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/core)
file(GLOB CORE_SOURCES ${CORE}/*.c)
add_library(opcore STATIC ${CORE_SOURCES})
target_include_directories(opcore PUBLIC ${CORE})

add_library(harness STATIC testing.c mini_json.c)
target_include_directories(harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

get_filename_component(VECTORS
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/test-vectors/vectors.json ABSOLUTE)

enable_testing()

add_executable(test_vectors test_vectors.c)
target_link_libraries(test_vectors opcore harness)
target_compile_definitions(test_vectors PRIVATE VECTORS_PATH="${VECTORS}")
add_test(NAME vectors COMMAND test_vectors)

add_executable(test_behaviour test_behaviour.c)
target_link_libraries(test_behaviour opcore harness)
add_test(NAME behaviour COMMAND test_behaviour)
