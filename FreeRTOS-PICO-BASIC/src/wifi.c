#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"

#define TARGET_IP "192.168.1.100"
#define TARGET_PORT 8080

bool wifi_send_udp_packet(uint8_t *data, size_t len) {
    ip_addr_t dest_addr;
    ipaddr_aton(TARGET_IP, &dest_addr);

    // 1. Lock the lwIP core to prevent FreeRTOS background thread collisions
    cyw43_arch_lwip_begin();

    // 2. Create a new UDP Protocol Control Block (PCB)
    struct udp_pcb *pcb = udp_new();
    if (!pcb) {
        cyw43_arch_lwip_end();
        return false;
    }

    // 3. Connect the PCB to the destination IP and port
    if (udp_connect(pcb, &dest_addr, TARGET_PORT) != ERR_OK) {
        udp_remove(pcb);
        cyw43_arch_lwip_end();
        return false;
    }

    // 4. Allocate a RAM buffer for the packet payload
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (!p) {
        udp_remove(pcb);
        cyw43_arch_lwip_end();
        return false;
    }

    // 5. Copy the benchmark data into the packet buffer
    pbuf_take(p, data, len);

    // 6. Send the packet over the Wi-Fi chip
    err_t err = udp_send(pcb, p);

    // 7. Free the memory, remove the PCB, and unlock the lwIP core
    pbuf_free(p);
    udp_remove(pcb);
    cyw43_arch_lwip_end();

    cyw43_arch_poll(); // <--- ADD THIS TO FORCE TRANSMISSION

    return (err == ERR_OK);
}