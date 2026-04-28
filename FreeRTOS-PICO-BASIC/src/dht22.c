#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"

bool dht22_read(uint pin, float *temp, float *humidity) {
    uint8_t data[5] = {0};
    uint last_state = 1;
    uint counter = 0;
    uint j = 0, i;

    // 1. Wake up the sensor by pulling the pin low
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(18)); // Must be held low for at least 18ms
    gpio_put(pin, 1);
    sleep_us(30);
    gpio_set_dir(pin, GPIO_IN);

    // 2. Suspend FreeRTOS scheduler for precise microsecond timing
    vTaskSuspendAll();

    // 3. Poll the 40 bits of incoming data
    for (i = 0; i < 85; i++) {
        counter = 0;
        while (gpio_get(pin) == last_state) {
            counter++;
            sleep_us(1);
            if (counter == 255) break;
        }
        last_state = gpio_get(pin);
        if (counter == 255) break;

        // Ignore the first 3 transitions (sensor ACK sequence)
        if ((i >= 4) && (i % 2 == 0)) {
            // Shift each bit into the 5 storage bytes
            data[j / 8] <<= 1;
            if (counter > 40) { // If the signal was HIGH for > 40us, it is a '1'
                data[j / 8] |= 1;
            }
            j++;
        }
    }

    // 4. Resume FreeRTOS scheduler immediately after the read
    xTaskResumeAll();

    // 5. Verify the checksum and calculate the final floats
    if ((j >= 40) && (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF))) {
        *humidity = (float)((data[0] << 8) | data[1]) / 10.0f;
        uint16_t temp_raw = (data[2] & 0x7F) << 8 | data[3];
        *temp = (float)temp_raw / 10.0f;
        
        // Handle negative temperatures
        if (data[2] & 0x80) *temp = -(*temp); 
        return true;
    }

    return false; // Checksum failed or sensor disconnected
}