/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Teams VDI 1.0 wire-capture plugin (teamscap)
 *
 * Passive dynamic-channel sniffer for com.microsoft.rdc.dvc.webrtc.1.
 * Registers a listener; when the server-side Teams redirector agent opens the
 * channel, all received DVC chunks are dumped to a timestamped .bin (raw bytes)
 * and .log (chunk metadata + hexdump). Nothing is ever sent back.
 *
 * License: Apache License 2.0
 */

#ifndef TEAMSCAP_MAIN_H
#define TEAMSCAP_MAIN_H

#include <freerdp/config.h>

#include <winpr/stream.h>

#include <freerdp/svc.h>
#include <freerdp/addin.h>
#include <freerdp/channels/log.h>

#define TEAMSCAP_TAG CHANNELS_TAG("teamscap.client")

#endif /* TEAMSCAP_MAIN_H */
