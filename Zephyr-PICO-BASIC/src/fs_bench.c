/*
 * fs_bench.c
 *
 * KPIs covered:
 *   - Filesystem sequential write throughput (KB/s)
 *   - Filesystem sequential read  throughput (KB/s)
 *   - fsync / flush latency (us)
 *
 * Transfer size: 512 B blocks x 100 = 51.2 KB total.
 *
 * Build additions required:
 *   FreeRTOS: link FatFS (USEMODULE or Makefile)
 *   RIOT:     USEMODULE += littlefs2
 *   Zephyr:   CONFIG_FILE_SYSTEM_LITTLEFS=y
 *             CONFIG_FILE_SYSTEM=y
 */

#include "benchmark_common.h"
#include "fs_bench.h"
#include <stdio.h>
//Setup file to use for benchmark testing
#define FS_TEST_FILE    "fstest.bin"
//Set sector size to 512 bytes for FAT32 format and
// perform the operation 100 times for 512*100 total bytes
#define FS_BLOCK_SIZE   512U
#define FS_BLOCK_COUNT  100U
#define FS_TOTAL_BYTES  (FS_BLOCK_SIZE * FS_BLOCK_COUNT)

//Initialize a buffer to store file system blocks
static uint8_t _fs_buf[FS_BLOCK_SIZE];

#if defined(FREERTOS_PICO)
#include "ff.h"
#include "sd_card.h"

//Initialize the file system object and the
// file object
static FATFS _fs;
static FIL   _fil;

//Used to have file system only initialize the first
// time fs_bench_run is called
static bool _fs_initialized = false;

fs_stats_t fs_bench_run(void) {
    //Initialize struct values to 0
    fs_stats_t s = {0};
    //Fill the buffer with numbers
    for (uint32_t i = 0; i < FS_BLOCK_SIZE; i++) _fs_buf[i] = (uint8_t)i;
    if (!_fs_initialized) {
        //Initialize the SD card
        if (!sd_init_driver()) {
            printf("Error occured while initalizing SD Card\r\n");
            return s; 
        }

        //Mount the file system, pass 1 to have the mounting function verify
        // that the file system is mounted correctly
        if (f_mount(&_fs, "", 1) != FR_OK) {
            printf("Error occured while mounting file system\r\n");
            return s;
        }
        //Set to true so that the above code doesn't run again
        _fs_initialized = true;
    }

    //Delete and recreate FS_TEST_FILE if its already present, set the file to write,
    // and use _fil as the file object
    //If FR_OK is false meaning the file open failed, return s as it was initialized
    // with all struct values set to 0
    if (f_open(&_fil, FS_TEST_FILE, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
        return s;

    //Get the current time before writing
    uint32_t t0 = xTaskGetTickCount();
    //write 256 blocks to the file object
    for (uint32_t b = 0; b < FS_BLOCK_COUNT; b++) {
        //bw writes the number of bytes f_write writes from
        // the file
        UINT bw;
        f_write(&_fil, _fs_buf, FS_BLOCK_SIZE, &bw);
    }
    //Get the time after writing and before syncing
    uint32_t tp = xTaskGetTickCount();
    //Sync the data from the file object to the storage device
    f_sync(&_fil);
    //Get the time in ticks after sync operation
    uint32_t post_sync_time = xTaskGetTickCount();
    //Get the time the sync took by subtracting the post-sync time
    // by the pre-sync time in microseconds
    s.fs_sync_us  = TICK_TO_US(post_sync_time - tp);
    //Get the writing and syncing time by subtracting the post-sync
    // time by the pre-write time in milliseconds
    uint32_t wms  = TICK_TO_MS(post_sync_time - t0);
    //Close the file
    f_close(&_fil);
    //Get the write kbps by dividing the total bytes by the
    // writing and syncing time
    s.fs_write_kbps = (wms > 0) ? (FS_TOTAL_BYTES / wms) : 0;

    //Open file for reading, if file open fails return struct
    if (f_open(&_fil, FS_TEST_FILE, FA_READ) != FR_OK) return s;
    //Get current time before reading
    t0 = xTaskGetTickCount();
    //Iterate over each block in the file and read it
    for (uint32_t b = 0; b < FS_BLOCK_COUNT; b++) {
        //br stores the number of bytes f_read reads from
        // the file
        UINT br;
        f_read(&_fil, _fs_buf, FS_BLOCK_SIZE, &br);
    }
    //Get the time after reading and before closing
    uint32_t rms = TICK_TO_MS(xTaskGetTickCount() - t0);
    f_close(&_fil);
    //Get the read kbps by dividing the total bytes by the
    // read time
    s.fs_read_kbps = (rms > 0) ? (FS_TOTAL_BYTES / rms) : 0;
    //Return the struct
    return s;
}

#elif defined(RIOT_OS)
#include "vfs.h"
#include "fs/fatfs.h"
#include <fcntl.h>
//Initialize the fatfs descriptor with vol_idx set to 0
// to have the operating system use the first logical drive (0:)
// which in this case will point to the SD Card module since that
// is the only module available in our hardware set up 
static fatfs_desc_t _fatfs_desc = {
    .vol_idx = 0
};
//Initialize the virtual file system mount structure with
// the FatFS driver, route operations relating to the bench
// directory to this Little FS partition, and link the 
// VFS structure to _fatfs_desc
static vfs_mount_t _mnt = {
    .fs           = &fatfs_file_system,
    .mount_point  = "/bench",
    .private_data = &_fatfs_desc,
};

fs_stats_t fs_bench_run(void) {
    //Initialize struct values to 0
    fs_stats_t s = {0};
    //Populate the buffer
    for (uint32_t i = 0; i < FS_BLOCK_SIZE; i++) _fs_buf[i] = (uint8_t)i;
    //Add the mount structure to the kernels Internal Mount Table
    vfs_mount(&_mnt);

    //Open the test file as write only, if it doesn't exist create it,
    // if it does exist clear its contents, and set permissions to 0
    // since this code is the only user on the system 
    int fd = vfs_open(FS_TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0);
    //If fd is less than 0 (indicating an error) return the struct
    // that contains only 0
    if (fd < 0) return s;
    //Get the time before writing to the file
    uint32_t t0 = ztimer_now(ZTIMER_MSEC);
    //Write FS_BLOCK_COUNT blocks to the file
    for (uint32_t b = 0; b < FS_BLOCK_COUNT; b++)
        vfs_write(fd, _fs_buf, FS_BLOCK_SIZE);
    //Record the time after writing but before syncing
    uint32_t tp = ztimer_now(ZTIMER_USEC);
    //Sync the data in the file object to the storage device
    vfs_fsync(fd);
    //Get the time after syncing subtracted by the time
    // after writing in microseconds
    s.fs_sync_us    = ztimer_now(ZTIMER_USEC) - tp;
    //Get the time after syncing subtracted by the time
    // before writing in milliseconds
    uint32_t wms = ztimer_now(ZTIMER_MSEC) - t0;
    //Close the file object
    vfs_close(fd);
    //Calculate write kbps by dividing the total bytes by the
    // write, sync, and close time
    s.fs_write_kbps = (wms > 0) ? (FS_TOTAL_BYTES / wms) : 0;

    //Open the file in read only mode and return the struct
    // if there was an error
    fd = vfs_open(FS_TEST_FILE, O_RDONLY, 0);
    if (fd < 0) return s;
    //Get the time before reading
    t0 = ztimer_now(ZTIMER_MSEC);
    //Read each block
    for (uint32_t b = 0; b < FS_BLOCK_COUNT; b++)
        vfs_read(fd, _fs_buf, FS_BLOCK_SIZE);
    //Record the time after reading subtracted by the time before
    // reading
    uint32_t rms    = ztimer_now(ZTIMER_MSEC) - t0;
    //Close the file
    vfs_close(fd);
    //Get read kbps by dividing total bytes by read time and return the 
    // metrics struct
    s.fs_read_kbps  = (rms > 0) ? (FS_TOTAL_BYTES / rms) : 0;
    return s;
}

#elif defined(ZEPHYR)
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>

//Initialize the Zephyr mount struct,
// set the driver to the FS_FATFS driver,
// set the mount point to the SD Card,
// have the configuration data set
// dynamically by FS_FATFS by reading
// the boot sector of the SD card, and
//have the VFS find the physical device
// registered with the name "SD"
static struct fs_mount_t _mnt = {
    .type        = FS_FATFS,
    .mnt_point   = "/SD:",
    .fs_data     = NULL,
    .storage_dev = (void *)"SD",
};

fs_stats_t fs_bench_run(void) {
    //Initialize struct values to 0
    fs_stats_t     s = {0};
    //Initialize the file object
    struct fs_file_t f;
    //Populate the buffer
    for (uint32_t i = 0; i < FS_BLOCK_SIZE; i++) _fs_buf[i] = (uint8_t)i;
    //Initialize SD Card, if it fails return the struct
    if (disk_access_init("SD") != 0) {
        return s; 
    }
    //Set the mount structure to active
    fs_mount(&_mnt);
    //Set the variable f so Zephyr can use it as a file object that can
    //be used in fs_open
    fs_file_t_init(&f);
    //Open the file in the SD card, create it if it doesn't exist yet,
    // and open it in write mode
    fs_open(&f, "/SD:/bench.bin", FS_O_CREATE | FS_O_WRITE);
    //Get the time before writing
    uint32_t t0 = (uint32_t)k_uptime_get();
    //Write block count blocks to the file
    for (uint32_t b = 0; b < FS_BLOCK_COUNT; b++)
        fs_write(&f, _fs_buf, FS_BLOCK_SIZE);
    //Get the time after writing
    uint32_t tp = (uint32_t)k_uptime_get() * 1000U;
    //Sync the file object to the storage device
    fs_sync(&f);
    //Get the time after syncing in milliseconds
    uint32_t post_sync_time = (uint32_t)k_uptime_get();
    //Get the difference in time between the time after
    // syncing and the time after writing
    s.fs_sync_us    = post_sync_time * 1000U - tp;
    //Get the difference between the time after closing and the
    // time before writing
    uint32_t wms    = post_sync_time - t0;
    //Close the file
    fs_close(&f);
    //Get the write kbps by dividing total bytes by wms
    s.fs_write_kbps = (wms > 0) ? (FS_TOTAL_BYTES / wms) : 0;

    //Open the file in read mode
    fs_open(&f, "/SD:/bench.bin", FS_O_READ);
    //Get the time before reading
    t0 = (uint32_t)k_uptime_get();
    //Get read the file block by block
    for (uint32_t b = 0; b < FS_BLOCK_COUNT; b++)
        fs_read(&f, _fs_buf, FS_BLOCK_SIZE);
    //Get the time after reading
    uint32_t rms   = (uint32_t)k_uptime_get() - t0;
    //Close the file
    fs_close(&f);
    //Get the read kbps by dividing the total bytes by rms 
    // and return the struct
    s.fs_read_kbps  = (rms > 0) ? (FS_TOTAL_BYTES / rms) : 0;
    return s;
}
#endif
