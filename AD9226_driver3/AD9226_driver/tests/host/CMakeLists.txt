cmake_minimum_required(VERSION 3.20)
project(F407MeasurementHostTests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_executable(ad9226_code_normalizer_tests
    test_ad9226_code_normalizer.c
    ../../USER/signal/ad9226_code_normalizer.c
)

target_include_directories(ad9226_code_normalizer_tests PRIVATE
    ../../USER/signal
)

target_compile_definitions(ad9226_code_normalizer_tests PRIVATE
    AD9226_REAL_FRAME_FIXTURE="${CMAKE_CURRENT_LIST_DIR}/fixtures/h7_dma_1v50_100khz_raw_u16le.bin"
)

if(MSVC)
    target_compile_options(ad9226_code_normalizer_tests PRIVATE /W4 /WX)
else()
    target_compile_options(ad9226_code_normalizer_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

add_test(NAME ad9226_code_normalizer_tests COMMAND ad9226_code_normalizer_tests)

add_executable(h743_measurement_rx_tests
    test_h743_measurement_rx.c
    ../../USER/protocol/h743_measurement_model.c
    ../../USER/protocol/h743_measurement_rx.c
)

target_include_directories(h743_measurement_rx_tests PRIVATE
    ../../USER/protocol
)

if(MSVC)
    target_compile_options(h743_measurement_rx_tests PRIVATE /W4 /WX)
else()
    target_compile_options(h743_measurement_rx_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

add_test(NAME h743_measurement_rx_tests COMMAND h743_measurement_rx_tests)

add_executable(screen_signal_model_tests
    test_screen_signal_model.c
    ../../USER/app/screen_signal_model.c
)

target_include_directories(screen_signal_model_tests PRIVATE
    ../../USER/app
    ../../USER/protocol
)

if(NOT MSVC)
    target_link_libraries(screen_signal_model_tests PRIVATE m)
endif()

if(MSVC)
    target_compile_options(screen_signal_model_tests PRIVATE /W4 /WX)
else()
    target_compile_options(screen_signal_model_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

add_test(NAME screen_signal_model_tests COMMAND screen_signal_model_tests)

add_executable(hmi_tjc_tests
    test_hmi_tjc.c
    ../../USER/drivers/hmi_tjc.c
)

target_include_directories(hmi_tjc_tests PRIVATE
    fakes
    ../../USER/drivers
    ../../USER/config
)

if(MSVC)
    target_compile_options(hmi_tjc_tests PRIVATE /W4 /WX)
else()
    target_compile_options(hmi_tjc_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

add_test(NAME hmi_tjc_tests COMMAND hmi_tjc_tests)

add_executable(screen_view_tests
    test_screen_view.c
    ../../USER/app/screen_view.c
)

target_include_directories(screen_view_tests PRIVATE
    fakes
    ../../USER/app
    ../../USER/config
    ../../USER/drivers
    ../../USER/protocol
)

if(MSVC)
    target_compile_options(screen_view_tests PRIVATE /W4 /WX)
else()
    target_compile_options(screen_view_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

add_test(NAME screen_view_tests COMMAND screen_view_tests)

add_executable(screen_curve_direction_tests
    test_screen_curve_direction.c
    ../../USER/app/screen_signal_model.c
    ../../USER/app/screen_view.c
    ../../USER/drivers/hmi_tjc.c
)

target_include_directories(screen_curve_direction_tests PRIVATE
    fakes
    ../../USER/app
    ../../USER/config
    ../../USER/drivers
    ../../USER/protocol
)

if(MSVC)
    target_compile_options(screen_curve_direction_tests PRIVATE /W4 /WX)
else()
    target_compile_options(screen_curve_direction_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_link_libraries(screen_curve_direction_tests PRIVATE m)
endif()

add_test(NAME screen_curve_direction_tests COMMAND screen_curve_direction_tests)

add_executable(measurement_lock_policy_tests
    test_measurement_lock_policy.c
    ../../USER/app/measurement_lock_policy.c
)

target_include_directories(measurement_lock_policy_tests PRIVATE
    ../../USER/app
)

if(MSVC)
    target_compile_options(measurement_lock_policy_tests PRIVATE /W4 /WX)
else()
    target_compile_options(measurement_lock_policy_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

add_test(NAME measurement_lock_policy_tests COMMAND measurement_lock_policy_tests)


add_executable(ad9226_raw_capture_tests
    test_ad9226_raw_capture.c
    ../../Core/Src/ad9226.c
)
target_include_directories(ad9226_raw_capture_tests PRIVATE
    ../../Core/Inc
)
if(MSVC)
    target_compile_options(ad9226_raw_capture_tests PRIVATE
        /W4 /WX /FI${CMAKE_CURRENT_SOURCE_DIR}/fakes/ad9226_hal.h
    )
else()
    target_compile_options(ad9226_raw_capture_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -include ${CMAKE_CURRENT_SOURCE_DIR}/fakes/ad9226_hal.h
    )
endif()
add_test(NAME ad9226_raw_capture_tests COMMAND ad9226_raw_capture_tests)
