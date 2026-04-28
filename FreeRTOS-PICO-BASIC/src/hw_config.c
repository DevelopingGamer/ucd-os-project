#include "hw_config.h"

/* Hardware Configuration for the SPI Bus */
static spi_t spis[] = {
    {
        .hw_inst = spi0,
        .miso_gpio = 4, /* RX */
        .mosi_gpio = 3, /* TX */
        .sck_gpio  = 2, /* SCK */
        .baud_rate = 12500 * 1000,
    }
};

/* Hardware Configuration for the SD Card Chip Select */
static sd_card_t sd_cards[] = {
    {
        .pcName = "0:",
        .spi = &spis[0],
        .ss_gpio = 5, /* CS */
        .use_card_detect = false,
        .card_detect_gpio = 0,
        .card_detected_true = 1
    }
};

/* Mandatory library hooks */
size_t sd_get_num() { return count_of(sd_cards); }
sd_card_t *sd_get_by_num(size_t num) {
    if (num <= sd_get_num()) return &sd_cards[num];
    return NULL;
}
size_t spi_get_num() { return count_of(spis); }
spi_t *spi_get_by_num(size_t num) {
    if (num <= spi_get_num()) return &spis[num];
    return NULL;
}