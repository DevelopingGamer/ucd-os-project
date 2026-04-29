# Name of application
APPLICATION = pico_benchmark_riot

# target board
BOARD ?= rpi-pico-w

# Path to the RIOT base directory
RIOTBASE ?= $(HOME)/os/RIOT-PICO/RIOT

#Have C compiler use the RIOT_OS code
CFLAGS += -DRIOT_OS

#Required RIOT OS modules
#Timers
USEMODULE += ztimer
USEMODULE += ztimer_msec
USEMODULE += ztimer_usec

#CPU Monitoring 
USEMODULE += schedstatistics

#File System
USEMODULE += vfs
USEMODULE += fatfs_vfs

#Networking
USEMODULE += gnrc_sock_udp
#No Ipv4 implementation that'll even let the code compile, eventhough the IPv6 version
# is still incapable of using the Infineon CYW 43439 wireless chip
USEMODULE += gnrc_ipv6
USEMODULE += auto_init_gnrc_netif

#As per the RIOT documentation (https://api.riot-os.org/group__boards__rpi__pico__w.html)
# gnrc_netdev_default or something like it has not actually been implmented for the RP 2040,
# due to the fact there there is no support for the Infineon CYW 43439 wireless chip, as such
# the data can be processed but there is no way for it to be sent out to the wireless chip
#USEMODULE += gnrc_netdev_default

#Sensors
USEMODULE += dht

#Hardware Peripherals
USEMODULE += periph_uart
USEMODULE += periph_timer
USEMODULE += periph_gpio_irq

#As per the RIOT documentation (https://api.riot-os.org/group__boards__rpi__pico__w.html)
# periph_wdt or something like it has not actually been implmented for the RP 2040, due to the
# fact there there is no support for the Hardware Watchdog, as such boot_recovery.c's
# _trigger_watchdog_reset function simulates a hardware reboot with a timer and then a software
# reboot 
#USEMODULE += periph_wdt

#SD Card Block Device Driver
USEMODULE += sdcard_spi

#Wi-Fi Credentials
CFLAGS += -DWIFI_SSID=\"PicoTest\"
CFLAGS += -DWIFI_PASS=\"7i59e9j6gusk825\"

#Include main RIOT build system
include $(RIOTBASE)/Makefile.include
