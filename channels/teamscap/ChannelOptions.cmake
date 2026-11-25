set(OPTION_DEFAULT ON)
set(OPTION_CLIENT_DEFAULT ON)
set(OPTION_SERVER_DEFAULT OFF)

define_channel_options(
  NAME
  "teamscap"
  TYPE
  "dynamic"
  DESCRIPTION
  "Teams VDI 1.0 wire-capture (passive sniffer for com.microsoft.rdc.dvc.webrtc.1)"
  SPECIFICATIONS
  ""
  DEFAULT
  ${OPTION_DEFAULT}
  CLIENT_DEFAULT
  ${OPTION_CLIENT_DEFAULT}
  SERVER_DEFAULT
  ${OPTION_SERVER_DEFAULT}
)
