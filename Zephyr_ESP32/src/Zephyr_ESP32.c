#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <stdio.h>
#include <zephyr/storage/flash_map.h>

//Used to record boot time
uint32_t boot_time = 0;

//Create list of 1000 samples to compute averages
#define NUM_SAMPLES 1000
volatile int64_t light_times[NUM_SAMPLES];
volatile int64_t heavy_times[NUM_SAMPLES];
volatile int64_t task_iot_times[NUM_SAMPLES];
volatile int64_t file_write_times[NUM_SAMPLES];
volatile int64_t ctx_switch_times[NUM_SAMPLES];

//Record current sample index for each list
volatile int sample_light = 0, sample_heavy = 0, sample_iot = 0, sample_write = 0, sample_ctx = 0;

//Record interrupt timestamp
volatile int64_t last_isr_time = 0;

//Gets uptime in milliseconds 
static inline int64_t get_time_us(void) {
    return k_ticks_to_us_floor64(k_uptime_ticks());
}

//Runs during timer interrupt, gets the time
void timer_handler(struct k_timer *dummy) {
    last_isr_time = get_time_us();
}
K_TIMER_DEFINE(benchmark_timer, timer_handler, NULL);

//Records how long 1000 interations takes, records the time in light_times and then sleeps for 1us
void light_task(void *, void *, void *) {
    while (1) {
        int64_t start = get_time_us();
        for (volatile int i = 0; i < 1000; i++);
        int64_t end = get_time_us();

        if (sample_light < NUM_SAMPLES)
            light_times[sample_light++] = end - start;

        k_msleep(1);
    }
}
K_THREAD_DEFINE(light_id, 2048, light_task, NULL, NULL, NULL, 5, 0, 0);

//Records how long 1000000 interations takes, records the time in heavy_times and then sleeps for 1us
void heavy_task(void *, void *, void *) {
    while (1) {
        int64_t start = get_time_us();
        for (volatile int i = 0; i < 1000000; i++);
        int64_t end = get_time_us();

        if (sample_heavy < NUM_SAMPLES)
            heavy_times[sample_heavy++] = end - start;

        k_msleep(1);
    }
}
K_THREAD_DEFINE(heavy_id, 2048, heavy_task, NULL, NULL, NULL, 5, 0, 0);

//Simulates an IoT task by generating random numbers and sleeping for a random amount of time
void iot_task(void *, void *, void *) {
    while (1) {
        int64_t start = get_time_us();
        uint32_t sensor = sys_rand32_get() % 100;
        for (volatile int i = 0; i < sensor * 100; i++);
        int64_t end = get_time_us();

        if (sample_iot < NUM_SAMPLES) {
            task_iot_times[sample_iot++] = end - start;
        }

        uint32_t delay = 5 + (sys_rand32_get() % 46);
        k_msleep(delay);
    }
}
K_THREAD_DEFINE(iot_id, 2048, iot_task, NULL, NULL, NULL, 5, 0, 0);

void ctx_task_a(void *, void *, void *) {
    while (1) {
        int64_t t1 = get_time_us();
        k_yield();
        int64_t t2 = get_time_us();

        if (sample_ctx < NUM_SAMPLES) {
            ctx_switch_times[sample_ctx++] = (t2 - t1) / 2; 
        }

        k_msleep(1);
    }
}
K_THREAD_DEFINE(ctx_id_a, 2048, ctx_task_a, NULL, NULL, NULL, 4, 0, 0);

void ctx_task_b(void *, void *, void *) {
    while (1) {
        k_yield();
        k_msleep(1);
    }
}
K_THREAD_DEFINE(ctx_id_b, 2048, ctx_task_b, NULL, NULL, NULL, 4, 0, 0);

//Configures Zephyr's Virtual File System to work with the ESP32's flash memory, 
// use default config to simplify implementation
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t lfs_storage = {
    .type = FS_LITTLEFS,
    .fs_data = &storage,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
    .mnt_point = "/lfs",
};

//Opens a test.txt to open and truncate in flash memory, starts a timer, writes 1000 lines to the file, 
// closes the file, records the time elapsed, and then sleeps for 5 seconds
void file_io_task(void *, void *, void *) {
    if (fs_mount(&lfs_storage) != 0) {
        printk("Error occurred while attempting to mount LittleFS.\n");
        return;
    }

    while (1) {
        struct fs_file_t file;
        fs_file_t_init(&file);
        
        if (fs_open(&file, "/lfs/test.txt", FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC) == 0) {
            int64_t start = get_time_us();
            for (int i = 0; i < 1000; i++) {
                fs_write(&file, "benchmark data\n", 15);
            }
            int64_t end = get_time_us();
            fs_close(&file);

            if (sample_write < NUM_SAMPLES) {
                file_write_times[sample_write++] = end - start;
            }
        } else {
            printk("File open failed\n");
        }

        k_msleep(5000);
    }
}
K_THREAD_DEFINE(fileio_id, 4096, file_io_task, NULL, NULL, NULL, 5, 0, 0);

//Helper function to get averages
int64_t calc_avg(volatile int64_t *arr, int size) {
    if (size == 0) return 0;
    int64_t sum = 0;
    for (int i = 0; i < size; i++) sum += arr[i];
    return sum / size;
}

//Prints the boot time, current ISR timestamp, average execution times of tasks, and then sleeps for 5 seconds
void metrics_task(void *, void *, void *) {
    while (1) {
        printk("\n===== BENCHMARK RESULTS =====\n");
        printk("Boot time: %u us\n", boot_time);
        printk("Last ISR time: %lld us\n", last_isr_time);

        if (sample_light) printk("Light_Task avg: %lld us\n", calc_avg(light_times, sample_light));
        if (sample_heavy) printk("Heavy_Task avg: %lld us\n", calc_avg(heavy_times, sample_heavy));
        if (sample_iot)   printk("IoT avg: %lld us\n", calc_avg(task_iot_times, sample_iot));
        if (sample_write) printk("File write avg: %lld us\n", calc_avg(file_write_times, sample_write));
        if (sample_ctx)   printk("Context switch avg: %lld us\n", calc_avg(ctx_switch_times, sample_ctx));
        printk("=============================\n");

        k_msleep(5000);
    }
}
K_THREAD_DEFINE(metrics_id, 4096, metrics_task, NULL, NULL, NULL, 5, 0, 0);

// Gets boot time and starts timer
int main(void) {
    boot_time = get_time_us();     
    k_timer_start(&benchmark_timer, K_SECONDS(1), K_SECONDS(1));
    return 0;
}
