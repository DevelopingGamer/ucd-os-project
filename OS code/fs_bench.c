/*
 * fs_bench.c
 *
 * KPIs covered:
 *   - Filesystem sequential write throughput (KB/s)
 *   - Filesystem sequential read  throughput (KB/s)
 *   - fsync / flush latency (us)
 *
 * Transfer size: 512 B blocks x 256 = 128 KB total.
 *
 * Build additions required:
 *   FreeRTOS: link FatFS (USEMODULE or Makefile)
 *   RIOT:     USEMODULE += littlefs2
 *   Zephyr:   CONFIG_FILE_SYSTEM_LITTLEFS=y
 *             CONFIG_FILE_SYSTEM=y
 */

#include "benchmark_common.h"
#include "fs_bench.h"

#define FS_TEST_FILE    "/bench/fstest.bin"
#define FS_BLOCK_SIZE   512U
#define FS_BLOCK_COUNT  256U
#define FS_TOTAL_BYTES  (FS_BLOCK_SIZE * FS_BLOCK_COUNT)

static uint8_t _fs_buf[FS_BLOCK_SIZE];

/* ════════════════════════════════════════════════════════════════════
 *  FreeRTOS — FatFS
 * ═══════════════════════════════════════════════════════════════════*/
#if defined(FREERTOS)
#include "ff.h"

static FATFS _fs;
static FIL   _fil;

fs_stats_t fs_bench_run(void) {
    fs_stats_t s = {0};
    for (uint32_t i = 0; i < FS_BLOCK_SIZE; i++) _fs_buf[i] = (uint8_t)i;
    f_mount(&_fs, "", 1);

    /* Write */
    if (f_open(&_fil, FS_TEST_FILE, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
        return s;

    uint32_t t0 = xTaskGetTickCount();
    for (uint32_t b = 0; b < FS_BLOCK_COUNT; b++) {
        UINT bw;
        f_write(&_fil, _fs_buf, FS_BLOCK_SIZE, &bw);
    }
    uint32_t tp = xTaskGetTickCount();
    f_sync(&_fil);
    s.fs_sync_us  = TICK_TO_US(xTaskGetTickCount() - tp);
    f_close(&_fil);
    uint32_t wms  = TICK_TO_MS(xTaskGetTickCount() - t0);
    s.fs_write_kbps = (wms > 0) ? (FS_TOTAL_BYTES / wms) : 0;

    /* Read */
    if (f_open(&_fil, FS_TEST_FILE, FA_READ) != FR_OK) return s;
    t0 = xTaskGetTickCount();
    for (uint32_t b = 0; b < FS_BLOCK_COUNT; b++) {
        UINT br;
        f_read(&_fil, _fs_buf, FS_BLOCK_SIZE, &br);
    }
    uint32_t rms = TICK_TO_MS(xTaskGetTickCount() - t0);
    f_close(&_fil);
    s.fs_read_kbps = (rms > 0) ? (FS_TOTAL_BYTES / rms) : 0;
    return s;
}

/* ════════════════════════════════════════════════════════════════════
 *  RIOT OS — littlefs2 via VFS
 * ═══════════════════════════════════════════════════════════════════*/
#elif defined(RIOT_OS)
#include "vfs.h"
#include "fs/littlefs2_fs.h"

static littlefs2_desc_t _lfs_desc = { .lock = MUTEX_INIT };
static vfs_mount_t _mnt = {
    .fs           = &littlefs2_file_system,
    .mount_point  = "/bench",
    .private_data = &_lfs_desc,
};

fs_stats_t fs_bench_run(void) {
    fs_stats_t s = {0};
    for (uint32_t i = 0; i < FS_BLOCK_SIZE; i++) _fs_buf[i] = (uint8_t)i;
    vfs_mount(&_mnt);

    /* Write */
    int fd = vfs_open(FS_TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0) return s;
    uint32_t t0 = ztimer_now(ZTIMER_MSEC);
    for (uint32_t b = 0; b < FS_BLOCK_COUNT; b++)
        vfs_write(fd, _fs_buf, FS_BLOCK_SIZE);
    uint32_t tp = ztimer_now(ZTIMER_USEC);
    vfs_fsync(fd);
    s.fs_sync_us    = ztimer_now(ZTIMER_USEC) - tp;
    vfs_close(fd);
    uint32_t wms    = ztimer_now(ZTIMER_MSEC) - t0;
    s.fs_write_kbps = (wms > 0) ? (FS_TOTAL_BYTES / wms) : 0;

    /* Read */
    fd = vfs_open(FS_TEST_FILE, O_RDONLY, 0);
    if (fd < 0) return s;
    t0 = ztimer_now(ZTIMER_MSEC);
    for (uint32_t b = 0; b < FS_BLOCK_COUNT; b++)
        vfs_read(fd, _fs_buf, FS_BLOCK_SIZE);
    uint32_t rms    = ztimer_now(ZTIMER_MSEC) - t0;
    vfs_close(fd);
    s.fs_read_kbps  = (rms > 0) ? (FS_TOTAL_BYTES / rms) : 0;
    return s;
}

/* ════════════════════════════════════════════════════════════════════
 *  Zephyr — LittleFS via fs subsystem
 * ═══════════════════════════════════════════════════════════════════*/
#elif defined(ZEPHYR)
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(cstorage);
static struct fs_mount_t _mnt = {
    .type        = FS_LITTLEFS,
    .fs_data     = &cstorage,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
    .mnt_point   = "/lfs",
};

fs_stats_t fs_bench_run(void) {
    fs_stats_t     s = {0};
    struct fs_file_t f;
    for (uint32_t i = 0; i < FS_BLOCK_SIZE; i++) _fs_buf[i] = (uint8_t)i;
    fs_mount(&_mnt);
    fs_file_t_init(&f);

    /* Write */
    fs_open(&f, "/lfs/bench.bin", FS_O_CREATE | FS_O_WRITE);
    uint32_t t0 = (uint32_t)k_uptime_get();
    for (uint32_t b = 0; b < FS_BLOCK_COUNT; b++)
        fs_write(&f, _fs_buf, FS_BLOCK_SIZE);
    uint32_t tp = (uint32_t)k_uptime_get() * 1000U;
    fs_sync(&f);
    s.fs_sync_us    = (uint32_t)k_uptime_get() * 1000U - tp;
    fs_close(&f);
    uint32_t wms    = (uint32_t)k_uptime_get() - t0;
    s.fs_write_kbps = (wms > 0) ? (FS_TOTAL_BYTES / wms) : 0;

    /* Read */
    fs_open(&f, "/lfs/bench.bin", FS_O_READ);
    t0 = (uint32_t)k_uptime_get();
    for (uint32_t b = 0; b < FS_BLOCK_COUNT; b++)
        fs_read(&f, _fs_buf, FS_BLOCK_SIZE);
    uint32_t rms   = (uint32_t)k_uptime_get() - t0;
    fs_close(&f);
    s.fs_read_kbps  = (rms > 0) ? (FS_TOTAL_BYTES / rms) : 0;
    return s;
}
#endif
