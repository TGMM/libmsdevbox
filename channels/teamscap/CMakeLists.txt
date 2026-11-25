# FreeRDP: A Remote Desktop Protocol Implementation
# Teams VDI 1.0 wire-capture plugin (teamscap)

# License: Apache License 2.0

define_channel("teamscap")

if(WITH_CLIENT_CHANNELS)
  add_channel_client(${MODULE_PREFIX} ${CHANNEL_NAME})
endif()
