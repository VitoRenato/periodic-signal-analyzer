add_executable(signal_analyzer_tests
    test_signal_analyzer.c
)

target_include_directories(signal_analyzer_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/Algorithm/Inc
    ${PROJECT_SOURCE_DIR}/Algorithm/Private
)

target_link_libraries(signal_analyzer_tests PRIVATE signal_analyzer)

if(MSVC)
    target_compile_options(signal_analyzer_tests PRIVATE /W4 /WX)
else()
    target_compile_options(signal_analyzer_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

add_test(NAME signal_analyzer_tests COMMAND signal_analyzer_tests)

add_executable(spectrum_detect_tests
    test_spectrum_detect.c
)

target_include_directories(spectrum_detect_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/Algorithm/Inc
    ${PROJECT_SOURCE_DIR}/Algorithm/Private
)

target_link_libraries(spectrum_detect_tests PRIVATE signal_analyzer)

if(NOT MSVC)
    target_link_libraries(spectrum_detect_tests PRIVATE m)
endif()

if(MSVC)
    target_compile_options(spectrum_detect_tests PRIVATE /W4 /WX)
else()
    target_compile_options(spectrum_detect_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

add_test(NAME spectrum_detect_tests COMMAND spectrum_detect_tests)
add_executable(signal_fft_tests
    test_signal_fft.c
)

target_include_directories(signal_fft_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/Algorithm/Inc
    ${PROJECT_SOURCE_DIR}/Algorithm/Private
)

target_link_libraries(signal_fft_tests PRIVATE signal_analyzer)

if(NOT MSVC)
    target_link_libraries(signal_fft_tests PRIVATE m)
endif()

if(MSVC)
    target_compile_options(signal_fft_tests PRIVATE /W4 /WX)
else()
    target_compile_options(signal_fft_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

add_test(NAME signal_fft_tests COMMAND signal_fft_tests)
add_executable(czt_zoom_tests
    test_czt_zoom.c
)

target_include_directories(czt_zoom_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/Algorithm/Inc
    ${PROJECT_SOURCE_DIR}/Algorithm/Private
)

target_link_libraries(czt_zoom_tests PRIVATE signal_analyzer)

if(NOT MSVC)
    target_link_libraries(czt_zoom_tests PRIVATE m)
endif()

if(MSVC)
    target_compile_options(czt_zoom_tests PRIVATE /W4 /WX)
else()
    target_compile_options(czt_zoom_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

add_test(NAME czt_zoom_tests COMMAND czt_zoom_tests)
add_executable(orthogonal_fit_tests
    test_orthogonal_fit.c
)

target_include_directories(orthogonal_fit_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/Algorithm/Inc
    ${PROJECT_SOURCE_DIR}/Algorithm/Private
)

target_link_libraries(orthogonal_fit_tests PRIVATE signal_analyzer)

if(NOT MSVC)
    target_link_libraries(orthogonal_fit_tests PRIVATE m)
endif()

if(MSVC)
    target_compile_options(orthogonal_fit_tests PRIVATE /W4 /WX)
else()
    target_compile_options(orthogonal_fit_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

add_test(NAME orthogonal_fit_tests COMMAND orthogonal_fit_tests)
add_executable(signal_end_to_end_tests
    test_signal_end_to_end.c
)

target_include_directories(signal_end_to_end_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/Algorithm/Inc
    ${PROJECT_SOURCE_DIR}/Algorithm/Private
)

target_link_libraries(signal_end_to_end_tests PRIVATE signal_analyzer)

if(NOT MSVC)
    target_link_libraries(signal_end_to_end_tests PRIVATE m)
endif()

if(MSVC)
    target_compile_options(signal_end_to_end_tests PRIVATE /W4 /WX)
else()
    target_compile_options(signal_end_to_end_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

add_test(NAME signal_end_to_end_tests COMMAND signal_end_to_end_tests)

add_executable(real_frame_regression_tests
    test_real_frame_regression.c
    ${PROJECT_SOURCE_DIR}/../../AD9226_driver3/AD9226_driver/USER/signal/ad9226_code_normalizer.c
)

target_include_directories(real_frame_regression_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/Algorithm/Inc
    ${PROJECT_SOURCE_DIR}/Algorithm/Private
    ${PROJECT_SOURCE_DIR}/../../AD9226_driver3/AD9226_driver/USER/signal
)

target_compile_definitions(real_frame_regression_tests PRIVATE
    AD9226_REAL_FRAME_FIXTURE="${PROJECT_SOURCE_DIR}/../../AD9226_driver3/AD9226_driver/tests/host/fixtures/h7_dma_1v50_100khz_raw_u16le.bin"
    AD9226_CURRENT_CAPTURE_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/f407_156khz_24code_u16le.bin"
)

target_link_libraries(real_frame_regression_tests PRIVATE signal_analyzer)

if(NOT MSVC)
    target_link_libraries(real_frame_regression_tests PRIVATE m)
endif()

if(MSVC)
    target_compile_options(real_frame_regression_tests PRIVATE /W4 /WX)
else()
    target_compile_options(real_frame_regression_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

add_test(NAME real_frame_regression_tests COMMAND real_frame_regression_tests)
add_executable(result_protocol_tests
    test_result_protocol.c
)

target_include_directories(result_protocol_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/Algorithm/Inc
    ${PROJECT_SOURCE_DIR}/Protocol/Inc
)

target_link_libraries(result_protocol_tests PRIVATE h743_result_protocol)

if(MSVC)
    target_compile_options(result_protocol_tests PRIVATE /W4 /WX)
else()
    target_compile_options(result_protocol_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

add_test(NAME result_protocol_tests COMMAND result_protocol_tests)
add_executable(set_fs_protocol_tests
    test_set_fs_protocol.c
)

target_include_directories(set_fs_protocol_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/Protocol/Inc
)

target_link_libraries(set_fs_protocol_tests PRIVATE h743_set_fs_protocol)

if(MSVC)
    target_compile_options(set_fs_protocol_tests PRIVATE /W4 /WX)
else()
    target_compile_options(set_fs_protocol_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

add_test(NAME set_fs_protocol_tests COMMAND set_fs_protocol_tests)

add_executable(measurement_pages_tests
    test_measurement_pages.c
)

target_include_directories(measurement_pages_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/Algorithm/Inc
    ${PROJECT_SOURCE_DIR}/Protocol/Inc
)

target_link_libraries(measurement_pages_tests PRIVATE
    h743_measurement_pages
)

if(MSVC)
    target_compile_options(measurement_pages_tests PRIVATE /W4 /WX)
else()
    target_compile_options(measurement_pages_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

add_test(NAME measurement_pages_tests COMMAND measurement_pages_tests)

add_executable(processor_telemetry_schedule_tests
    test_processor_telemetry_schedule.c
    ${PROJECT_SOURCE_DIR}/Firmware_H743/Core/Src/processor_telemetry_schedule.c
)

target_include_directories(processor_telemetry_schedule_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/Firmware_H743/Core/Inc
)

if(MSVC)
    target_compile_options(processor_telemetry_schedule_tests PRIVATE /W4 /WX)
else()
    target_compile_options(processor_telemetry_schedule_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

add_test(
    NAME processor_telemetry_schedule_tests
    COMMAND processor_telemetry_schedule_tests)
