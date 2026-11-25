# FreeRDP: A Remote Desktop Protocol Implementation
# Teams VDI 1.0 wire-capture plugin (teamscap)

# License: Apache License 2.0

define_channel_client("teamscap")

set(${MODULE_PREFIX}_SRCS teamscap_main.c teamscap_main.h)

set(${MODULE_PREFIX}_LIBS winpr)

include_directories(..)

add_channel_client_library(${MODULE_PREFIX} ${MODULE_NAME} ${CHANNEL_NAME} TRUE "DVCPluginEntry")
