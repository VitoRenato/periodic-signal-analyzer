cmake_minimum_required(VERSION 3.20)

project(H743SignalProcessor LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(signal_analyzer STATIC
    Algorithm/Src/signal_analyzer.c
    Algorithm/Src/clipped_harmonic_recovery.c
    Algorithm/Src/signal_preprocess.c
    Algorithm/Src/signal_fft_portable.c
    Algorithm/Src/spectrum_detect.c
    Algorithm/Src/czt_zoom.c
    Algorithm/Src/harmonic_model.c
    Algorithm/Src/orthogonal_fit.c
    Algorithm/Src/signal_metrics.c
    Algorithm/Src/weak_harmonic_probe.c
)

target_include_directories(signal_analyzer PUBLIC
    ${PROJECT_SOURCE_DIR}/Algorithm/Inc
)

target_include_directories(signal_analyzer PRIVATE
    ${PROJECT_SOURCE_DIR}/Algorithm/Private
)
if(NOT MSVC)
    target_link_libraries(signal_analyzer PUBLIC m)
endif()

if(MSVC)
    target_compile_options(signal_analyzer PRIVATE /W4 /WX)
else()
    target_compile_options(signal_analyzer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

add_library(h743_result_protocol STATIC
    Protocol/Src/h743_result_protocol.c
    Protocol/Src/h743_crc32.c
)

target_include_directories(h743_result_protocol PUBLIC
    ${PROJECT_SOURCE_DIR}/Algorithm/Inc
    ${PROJECT_SOURCE_DIR}/Protocol/Inc
)

if(NOT MSVC)
    target_link_libraries(h743_result_protocol PUBLIC m)
endif()

if(MSVC)
    target_compile_options(h743_result_protocol PRIVATE /W4 /WX)
else()
    target_compile_options(h743_result_protocol PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

enable_testing()
add_library(h743_set_fs_protocol STATIC
    Protocol/Src/h743_set_fs_protocol.c
)

target_include_directories(h743_set_fs_protocol PUBLIC
    ${PROJECT_SOURCE_DIR}/Protocol/Inc
)

if(MSVC)
    target_compile_options(h743_set_fs_protocol PRIVATE /W4 /WX)
else()
    target_compile_options(h743_set_fs_protocol PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

add_library(h743_measurement_pages STATIC
    Protocol/Src/h743_measurement_pages.c
)

target_include_directories(h743_measurement_pages PUBLIC
    ${PROJECT_SOURCE_DIR}/Algorithm/Inc
    ${PROJECT_SOURCE_DIR}/Protocol/Inc
)

target_link_libraries(h743_measurement_pages PUBLIC
    h743_set_fs_protocol
)

if(NOT MSVC)
    target_link_libraries(h743_measurement_pages PUBLIC m)
endif()

if(MSVC)
    target_compile_options(h743_measurement_pages PRIVATE /W4 /WX)
else()
    target_compile_options(h743_measurement_pages PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()
add_subdirectory(tests)
