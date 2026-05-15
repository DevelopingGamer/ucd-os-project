# name of your application
APPLICATION = esp32-riot

# If no BOARD is found in the environment, use this default:
BOARD ?= native

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../../RIOT

# Include the ztimer module for standard application timing
USEMODULE += ztimer
USEMODULE += ztimer_msec
USEMODULE += malloc_thread_safe

# Enable RIOT scheduler statistics tracking
USEMODULE += schedstatistics

# Force the kernel scheduler to compile with statistics-gathering hooks enabled
CFLAGS += -DSCHED_STATISTICS

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP ?= 1

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

include $(RIOTBASE)/Makefile.include