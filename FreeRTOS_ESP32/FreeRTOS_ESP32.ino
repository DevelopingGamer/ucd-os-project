#include <Arduino.h>
#include "esp_timer.h"
#include "SPIFFS.h"
#include "esp_heap_caps.h"

//Used to record boot time
uint32_t boot_start = 0;
uint32_t boot_time = 0;

//Create list of 1000 samples to compute averages
#define NUM_SAMPLES 1000
volatile int64_t light_times[NUM_SAMPLES];
volatile int64_t heavy_times[NUM_SAMPLES];
volatile int64_t task_iot_times[NUM_SAMPLES];
volatile int64_t file_write_times[NUM_SAMPLES];
volatile int64_t ctx_switch_times[NUM_SAMPLES];
volatile int64_t isr_latency_samples[NUM_SAMPLES];

//Record current sample index for each list
volatile int sample_light = 0;
volatile int sample_heavy = 0;
volatile int sample_iot = 0;
volatile int sample_write = 0;
volatile int sample_ctx = 0;
volatile int sample_isr = 0;

//Record interrupt timestamp
volatile int64_t last_isr_time = 0;

//Initialize pointer to hardware timer
hw_timer_t *timer = NULL;

//Runs during timer interrupt, gets the time and adds that time to the interrupt time list
void IRAM_ATTR onTimer() {
  last_isr_time = esp_timer_get_time();

  if (sample_isr < NUM_SAMPLES) {
    isr_latency_samples[sample_isr++] = last_isr_time;
  }
}

//Records how long 1000 interations takes, records the time in light_times and then sleeps for 1us
void Light_Task(void *pvParameters) {
  while (1) {
    int64_t start = esp_timer_get_time();

    for (volatile int i = 0; i < 1000; i++);

    int64_t end = esp_timer_get_time();

    if (sample_light < NUM_SAMPLES)
      light_times[sample_light++] = end - start;

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

//Records how long 1000000 interations takes, records the time in heavy_times and then sleeps for 1us
void Heavy_Task(void *pvParameters) {
  while (1) {
    int64_t start = esp_timer_get_time();

    for (volatile int i = 0; i < 1000000; i++);

    int64_t end = esp_timer_get_time();

    if (sample_heavy < NUM_SAMPLES)
      heavy_times[sample_heavy++] = end - start;

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

//Simulates an IoT task by generating random numbers and sleeping for a random amount of time
void IoTTask(void *pvParameters) {
  while (1) {
    int64_t start = esp_timer_get_time();
    int sensor = random(0, 100);
    for (volatile int i = 0; i < sensor * 100; i++);
    int64_t end = esp_timer_get_time();

    if (sample_iot < NUM_SAMPLES)
      task_iot_times[sample_iot++] = end - start;

    vTaskDelay(pdMS_TO_TICKS(random(5, 50)));
  }
}

//Opens a test.txt to open and truncate in flash memory, starts a timer, writes 1000 lines to the file, closes the file, records the time elapsed, and then sleeps for 5 seconds
void FileIOTask(void *pvParameters) {
  while (1) {
    File file = SPIFFS.open("/test.txt", FILE_WRITE, true);
    if (!file) {
      Serial.println("File open failed");
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    int64_t start = esp_timer_get_time();
    for (int i = 0; i < 1000; i++) {
      file.println("benchmark data");
    }
    int64_t end = esp_timer_get_time();

    file.close();

    if (sample_write < NUM_SAMPLES)
      file_write_times[sample_write++] = end - start;

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

//Gets the time and then yields the cpu, gets the time again when it next runs, records the time elapsed, then sleeps for 1us
void ContextSwitchTask(void *pvParameters) {
  while (1) {
    int64_t t1 = esp_timer_get_time();
    taskYIELD();
    int64_t t2 = esp_timer_get_time();

    if (sample_ctx < NUM_SAMPLES)
      ctx_switch_times[sample_ctx++] = t2 - t1;

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

//Prints the boot time, heap stats, current ISR timestamp, average execution times of tasks, CPU usage stats, and then sleeps for 5 seconds
void MetricsTask(void *pvParameters) {
  while (1) {
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_8BIT);
    uint32_t total_heap = info.total_free_bytes + info.total_allocated_bytes;
    uint32_t free_heap = info.total_free_bytes;
    uint32_t used_heap = info.total_allocated_bytes;

    Serial.println("\n===== BENCHMARK RESULTS =====");
    Serial.printf("Boot time: %d us\n", boot_time);
    Serial.printf("Free heap: %d bytes (%.2f%%)\n", free_heap, (float)free_heap / total_heap * 100.0);
    Serial.printf("Heap in use: %d bytes (%.2f%%)\n", used_heap, (float)used_heap / total_heap * 100.0);
    Serial.printf("Last ISR time: %lld us\n", last_isr_time);

    auto avg = [](volatile int64_t *arr, int size) -> int64_t {
      int64_t sum = 0;
      for (int i = 0; i < size; i++) sum += arr[i];
      return (size > 0) ? sum / size : 0;
    };

    if (sample_light)
      Serial.printf("Light_Task avg: %lld us\n", avg(light_times, sample_light));

    if (sample_heavy)
      Serial.printf("Heavy_Task avg: %lld us\n", avg(heavy_times, sample_heavy));

    if (sample_iot)
      Serial.printf("IoT avg: %lld us\n", avg(task_iot_times, sample_iot));

    if (sample_write)
      Serial.printf("File write avg: %lld us\n", avg(file_write_times, sample_write));

    if (sample_ctx)
      Serial.printf("Context switch avg: %lld us\n", avg(ctx_switch_times, sample_ctx));

    Serial.println("=============================\n");

    Serial.println("\n========= CPU USAGE =========");
    char buffer[1024];
    vTaskGetRunTimeStats(buffer);
    Serial.println(buffer);
    Serial.println("=============================\n");

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void setup() {
  //Get the boot start time and initialize the serial port
  boot_start = esp_timer_get_time();
  Serial.begin(115200);

  //Print error message and terminate setup if SPIFFS initialization fails
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS failed");
    return;
  }

  //Create timer with 1MHz frequency (1 us per tick) to trigger every second
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, 1000000, true, 0);

  //Create RTOS tasks
  xTaskCreatePinnedToCore(Light_Task, "Light_Task", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(Heavy_Task, "Heavy_Task", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(ContextSwitchTask, "Ctx", 2048, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(MetricsTask, "Metrics", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(IoTTask, "IoT", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(FileIOTask, "FileIO", 4096, NULL, 1, NULL, 1);

  //Get the boot time by subtracting the time at the end of setup by the start time
  boot_time = esp_timer_get_time() - boot_start;
}

//Empty due to tasks being done by FreeRTOS, but still required for successful compilation in Arduino IDE
void loop() {

}
