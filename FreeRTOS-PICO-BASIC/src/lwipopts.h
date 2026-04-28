#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

/* Allow lwIP to run in the Pico's background without a dedicated RTOS thread */
#define NO_SYS 1

/* Prevent lwIP from redefining standard C time structures */
#define LWIP_TIMEVAL_PRIVATE 0

/* Disable OS-dependent LWIP APIs to prevent Sequential API errors */
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0

/* Enable IPv4 and standard protocols */
#define LWIP_IPV4 1
#define LWIP_DHCP 1
#define LWIP_UDP 1
#define LWIP_TCP 1

/* Standard Memory and Buffer Settings */
#define MEM_ALIGNMENT 4
#define MEM_SIZE 4000
#define MEMP_NUM_TCP_SEG 32
#define PBUF_POOL_SIZE 24

/* Pico W specific optimizations */
#define LWIP_NETIF_TX_SINGLE_PBUF 1

#endif /* _LWIPOPTS_H */