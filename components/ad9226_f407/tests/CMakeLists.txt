cmake_minimum_required(VERSION 3.20)
project(ad9226_f407_host_tests C)

enable_testing()

add_executable(ad9226_f407_tests
    test_ad9226_f407.c
    ../src/ad9226_f407.c
)

target_include_directories(ad9226_f407_tests PRIVATE
    ../include
    fakes
)

target_compile_features(ad9226_f407_tests PRIVATE c_std_11)
target_compile_options(ad9226_f407_tests PRIVATE -Wall -Wextra -Werror)

add_test(NAME ad9226_f407_tests COMMAND ad9226_f407_tests)

add_executable(ad9226_code_normalizer_tests
    test_ad9226_code_normalizer.c
    ../src/ad9226_code_normalizer.c
)

target_include_directories(ad9226_code_normalizer_tests PRIVATE
    ../include
)

target_compile_features(ad9226_code_normalizer_tests PRIVATE c_std_11)
target_compile_options(ad9226_code_normalizer_tests PRIVATE -Wall -Wextra -Werror)

add_test(NAME ad9226_code_normalizer_tests COMMAND ad9226_code_normalizer_tests)

add_library(ad9226_example_compile OBJECT
    ../examples/stm32cube_hal_example.c
)
target_compile_definitions(ad9226_example_compile PRIVATE
    AD9226_EXAMPLE_HOST_BUILD=1
)
target_include_directories(ad9226_example_compile PRIVATE
    ../include
    fakes
)
target_compile_features(ad9226_example_compile PRIVATE c_std_11)
target_compile_options(ad9226_example_compile PRIVATE -Wall -Wextra -Werror)
