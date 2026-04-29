cmake_minimum_required(VERSION 3.20.0)
set(BOARD rpi_pico2/rp2350a/m33/w)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pico_benchmark_zephyr)

#Compiler flag to trigger Zephyr code blocks
zephyr_compile_definitions(ZEPHYR)

#Include source files
target_sources(app PRIVATE 
    main.c 
    bench_log.c 
    boot_recovery.c
    cpu_monitor.c 
    fs_bench.c 
    io_bench.c 
    latency_meas.c 
    mem_profile.c 
    workload_sim.c
)