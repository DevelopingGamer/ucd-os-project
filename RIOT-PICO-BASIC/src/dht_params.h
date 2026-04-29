#ifndef DHT_PARAMS_H
#define DHT_PARAMS_H

#include "board.h"
#include "dht.h"

#ifdef __cplusplus
extern "C" {
#endif

// Override the default RIOT DHT parameters
// to accommodate having the DHT's data line
// connected to GP20
static const dht_params_t dht_params[] = {
    {
        .pin     = GPIO_PIN(0, 20),
        .type    = DHT22,
        .in_mode = GPIO_IN_PU
    }
};

#ifdef __cplusplus
}
#endif

#endif