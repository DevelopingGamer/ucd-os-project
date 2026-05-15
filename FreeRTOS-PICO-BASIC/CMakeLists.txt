cmake_minimum_required(VERSION 3.13)

#Include Pico SDK boilerplate
include(pico_sdk_import.cmake)

project(rtos_benchmark_freertos C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

#Set FreeRTOS Kernel path
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/FreeRTOS-Kernel)

#Tell FreeRTOS Pico 2W Cortex-M33 architecture
set(FREERTOS_PORT GCC_ARM_CM33_NTZ CACHE STRING "" FORCE)

#Create the FreeRTOS config target so the kernel can find FreeRTOSConfig.h
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config INTERFACE ${CMAKE_CURRENT_LIST_DIR})

#Load the FreeRTOS Kernel build system
add_subdirectory(${FREERTOS_KERNEL_PATH} FreeRTOS-Kernel)

#Manually provide the sources for the M33 port
target_sources(freertos_kernel_port PRIVATE 
    ${FREERTOS_KERNEL_PATH}/portable/GCC/ARM_CM33_NTZ/non_secure/port.c
    ${FREERTOS_KERNEL_PATH}/portable/GCC/ARM_CM33_NTZ/non_secure/portasm.c
)

target_include_directories(freertos_kernel_port PUBLIC
    ${FREERTOS_KERNEL_PATH}/portable/GCC/ARM_CM33_NTZ/non_secure
)

#Initialize the Pico SDK and the external SD Card library
pico_sdk_init()
add_subdirectory(no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI)

#Create the executable and point to your source files
add_executable(benchmark_pico2w
    src/main.c
    src/bench_log.c
    src/boot_recovery.c
    src/cpu_monitor.c
    src/latency_meas.c
    src/mem_profile.c
    src/io_bench.c
    src/fs_bench.c
    src/workload_sim.c
    src/hw_config.c
    src/dht22.c
    src/wifi.c
    ${FREERTOS_KERNEL_PATH}/portable/MemMang/heap_4.c
)

target_include_directories(benchmark_pico2w PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${FREERTOS_KERNEL_PATH}/portable/GCC/ARM_CM33_NTZ/non_secure
)

#Tell the C compiler this is the FreeRTOS build
target_compile_definitions(benchmark_pico2w PRIVATE FREERTOS_PICO)

#Include the directory containing project headers
target_include_directories(benchmark_pico2w PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

#Link all required libraries
target_link_libraries(benchmark_pico2w
    pico_stdlib
    pico_standard_link
    hardware_timer
    hardware_gpio
    hardware_powman
    hardware_watchdog
    hardware_uart
    pico_cyw43_arch_lwip_poll
    freertos_kernel
    FatFs_SPI
)

#Enable USB UART output so you can read the benchmark logs
pico_enable_stdio_usb(benchmark_pico2w 1)
pico_enable_stdio_uart(benchmark_pico2w 0)

#Generate the .uf2 file
pico_add_extra_outputs(benchmark_pico2w)