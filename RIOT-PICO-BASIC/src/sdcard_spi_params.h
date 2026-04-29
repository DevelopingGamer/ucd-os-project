#ifndef SDCARD_SPI_PARAMS_H
#define SDCARD_SPI_PARAMS_H

#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

// Override default SD Card SPI mapping
static const sdcard_spi_params_t sdcard_spi_params[] = {
    {
        .spi_dev        = SPI_DEV(0),
        .cs             = GPIO_PIN(0, 5), /* CS */
        .clk            = GPIO_PIN(0, 2), /* SCK */
        .mosi           = GPIO_PIN(0, 3), /* TX  */
        .miso           = GPIO_PIN(0, 4), /* RX  */
        .power          = GPIO_UNDEF,          /* No dedicated power pin */
    }
};

#ifdef __cplusplus
}
#endif

#endif