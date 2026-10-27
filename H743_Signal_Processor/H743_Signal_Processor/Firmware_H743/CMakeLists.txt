cmake_minimum_required(VERSION 3.20)

project(H743_Signal_Processor_Firmware LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(MCU_OPTIONS
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
)

add_executable(${PROJECT_NAME}
    Startup/startup_stm32h743xx.s
    Core/Src/main.c
    Core/Src/processor_app.c
    Core/Src/processor_telemetry_schedule.c
    Core/Src/sample_rate_policy.c
    Core/Src/spi_links.c
    Core/Src/syscalls.c
    Core/Src/stm32h7xx_hal_msp.c
    Core/Src/stm32h7xx_it.c
    Core/Src/system_stm32h7xx.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_usart.c
    ../Algorithm/Src/signal_analyzer.c
    ../Algorithm/Src/clipped_harmonic_recovery.c
    ../Algorithm/Src/signal_preprocess.c
    ../Algorithm/Src/signal_fft_portable.c
    ../Algorithm/Src/spectrum_detect.c
    ../Algorithm/Src/czt_zoom.c
    ../Algorithm/Src/harmonic_model.c
    ../Algorithm/Src/orthogonal_fit.c
    ../Algorithm/Src/signal_metrics.c
    ../Algorithm/Src/weak_harmonic_probe.c
    ../Protocol/Src/acq_frame_header.c
    ../Protocol/Src/h743_crc32.c
    ../Protocol/Src/h743_result_protocol.c
    ../Protocol/Src/h743_set_fs_protocol.c
    ../Protocol/Src/h743_measurement_pages.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    Core/Inc
    Drivers/STM32H7xx_HAL_Driver/Inc
    Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32H7xx/Include
    Drivers/CMSIS/Include
    ../Algorithm/Inc
    ../Algorithm/Private
    ../Protocol/Inc
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER
    USE_PWR_LDO_SUPPLY
    STM32H743xx
)

target_compile_options(${PROJECT_NAME} PRIVATE
    ${MCU_OPTIONS}
    -Wall
    -Wextra
    -Wpedantic
    -ffunction-sections
    -fdata-sections
    -fstack-usage
)

target_link_options(${PROJECT_NAME} PRIVATE
    ${MCU_OPTIONS}
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32H743ZITX_FLASH.ld
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--cref
    -Wl,-Map=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map
)

target_link_libraries(${PROJECT_NAME} PRIVATE m)
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".elf")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:${PROJECT_NAME}>
            ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${PROJECT_NAME}>
            ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    VERBATIM
)
