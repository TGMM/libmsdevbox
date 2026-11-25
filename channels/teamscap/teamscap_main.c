/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Teams VDI 1.0 protocol dispatcher (teamscap, Phase 1)
 *
 * Listens on com.microsoft.rdc.dvc.webrtc.1, parses incoming JSON-RPC,
 * responds to the handshake, and captures all verbs for protocol discovery.
 *
 * Phase 0: passive sniffer — DONE, wire format captured
 * Phase 1: respond to setVersionInfo — THIS VERSION
 *
 * Set TEAMSCAP_NORESPOND=1 to fall back to passive-only (for comparison captures).
 *
 * License: Apache License 2.0
 */

#include <freerdp/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include <winpr/crt.h>
#include <winpr/assert.h>
#include <winpr/stream.h>

#include "teamscap_main.h"

#include <freerdp/freerdp.h>
#include <freerdp/client/channels.h>
#include <freerdp/channels/log.h>

#define TAG TEAMSCAP_TAG

#define TEAMS_VDI10_CHANNEL_NAME "com.microsoft.rdc.dvc.webrtc.1"

typedef struct
{
	IWTSListenerCallback iface;

	IWTSPlugin* plugin;
	IWTSVirtualChannelManager* channel_mgr;

	rdpContext* rdpcontext;
} TEAMSCAP_LISTENER_CALLBACK;

typedef struct
{
	GENERIC_CHANNEL_CALLBACK generic;

	rdpContext* rdpcontext;
	FILE* rawFile;
	FILE* metaFile;
	UINT32 chunkCount;
	UINT32 responseCount;
	UINT64 totalBytes;
} TEAMSCAP_CHANNEL_CALLBACK;

typedef struct
{
	IWTSPlugin iface;

	TEAMSCAP_LISTENER_CALLBACK* listener_callback;

	rdpContext* rdpcontext;
} TEAMSCAP_PLUGIN;

/* Bounded substring search (safe with embedded nulls) */
static BOOL teamscap_contains(const char* data, size_t len, const char* needle)
{
	size_t needleLen = strlen(needle);
	if (needleLen > len)
		return FALSE;
	for (size_t i = 0; i <= len - needleLen; i++)
	{
		if (memcmp(data + i, needle, needleLen) == 0)
			return TRUE;
	}
	return FALSE;
}

/* WLog-based hex dump (for logging to terminal, not file) */
static void teamscap_log_hex(const char* tag, const BYTE* data, size_t length)
{
	char buf[256] = { 0 };
	size_t pos = 0;
	for (size_t i = 0; i < length && pos < sizeof(buf) - 4; i++)
	{
		pos += snprintf(buf + pos, sizeof(buf) - pos, "%02x ", data[i]);
		if (i == 31)
			break; /* first 32 bytes only */
	}
	WLog_INFO(TAG, "%s [%zu bytes]: %s", tag, length, buf);
}

static void teamscap_hexdump(FILE* fp, const BYTE* data, size_t length)
{
	char ascii[17] = { 0 };

	for (size_t off = 0; off < length; off += 16)
	{
		size_t n = (length - off < 16) ? (length - off) : 16;
		fprintf(fp, "  %04zx  ", off);
		for (size_t i = 0; i < 16; i++)
		{
			if (i < n)
				fprintf(fp, "%02x ", data[off + i]);
			else
				fprintf(fp, "   ");
			if (i == 7)
				fprintf(fp, " ");
		}
		for (size_t i = 0; i < n; i++)
		{
			BYTE b = data[off + i];
			ascii[i] = (b >= 0x20 && b < 0x7f) ? (char)b : '.';
		}
		ascii[n] = '\0';
		fprintf(fp, " |%s|\n", ascii);
	}
}

/**
 * Send a JSON response back through the DVC channel.
 * Phase 6: bare JSON (no length prefix — trailing bytes on incoming are buffer garbage,
 * not framing). Include "id" and "status" fields per SendRtcRemoteProcedureCallResponse signature.
 */
static UINT teamscap_send(TEAMSCAP_CHANNEL_CALLBACK* callback, const char* json)
{
	WINPR_ASSERT(callback);
	WINPR_ASSERT(json);

	IWTSVirtualChannel* channel = callback->generic.channel;
	if (!channel)
	{
		WLog_ERR(TAG, "no channel to write to");
		return CHANNEL_RC_OK;
	}

	size_t len = strlen(json);
	UINT status = channel->Write(channel, (ULONG)len, (const BYTE*)json, nullptr);
	if (status != CHANNEL_RC_OK)
	{
		WLog_ERR(TAG, "channel Write failed: %" PRIu32, status);
		return status;
	}

	callback->responseCount++;
	WLog_INFO(TAG, ">>> SENT [%zu bytes bare] %s", len, json);

	if (callback->metaFile)
	{
		fprintf(callback->metaFile, ">>> RESPONSE #%u [%zu bytes bare]: %s\n",
		        callback->responseCount, len, json);
		fflush(callback->metaFile);
	}

	return CHANNEL_RC_OK;
}

/**
 * Phase 1 verb router: parse the rpcName and respond accordingly.
 */
static UINT teamscap_route(TEAMSCAP_CHANNEL_CALLBACK* callback, const BYTE* data, size_t size)
{
	const char* json = (const char*)data;
	const BOOL respond = (getenv("TEAMSCAP_NORESPOND") == NULL);

	if (!respond)
		return CHANNEL_RC_OK;

	/* --- RDWebRTCRedirector side-channel verbs --- */

	if (teamscap_contains(json, size, "\"setVersionInfo\""))
	{
		WLog_INFO(TAG, "<<< setVersionInfo — replying sessioninfo EVENT (byte-exact template)");

		/* CONFIRMED via decompilation of both sides (2026-11-24):
		 * RDCoreSdk.dll FUN_18035e7b0 (endpoint construction) + MsRdcWebRTCSvc.exe
		 * FUN_140047650 (VM-side parser, RtcWebSocketWorker.cpp:0x1887):
		 *
		 * The VM-side service gates on this EXACT byte substring (strstr):
		 *   "rpcEventTarget":{"rpcObjectType":"RDWebRTCRedirector"},
		 *   "rpcEventName":"sessioninfo"
		 * so the key order below must not change.
		 *
		 * The service parses rpcEventArgs{display{width,height},
		 * version{clientOS,webRTC,webRTCRedirector,clientAppVersion,...},
		 * features{clipRectMode,givecontrol,givecontrolv2,appshare,
		 * appshareClientSupport,screenshare,multimonitorscreenshare,...}}
		 * and normalizes appshare=enabled when appshareClientSupport!=disabled. */
		const char announce[] =
		    "{\"rpcEventTarget\":{\"rpcObjectType\":\"RDWebRTCRedirector\"},"
		    "\"rpcEventName\":\"sessioninfo\","
		    "\"rpcEventArgs\":{"
		    "\"version\":{"
		    "\"clientOS\":\"Version 26100 (OS Build 26100.2454)\","
		    "\"clientAppName\":\"msrdc\","
		    "\"clientPlatform\":\"Windows\","
		    "\"clientAppVersion\":\"2.0.1601.0\","
		    "\"clientFlavor\":\"msrdc\","
		    "\"webRTC\":\"M133 (02.10.25) (cd3e295)\","
		    "\"webRTCRedirector\":\"3.1.2609.30001\"},"
		    "\"display\":{\"width\":1920,\"height\":1080},"
		    "\"session\":{\"vdiMode\":true,"
		    "\"endpointId\":\"9f2d8f6e-1234-4000-8000-525400c0ffee\"},"
		    "\"endpointId\":\"9f2d8f6e-1234-4000-8000-525400c0ffee\","
		    "\"features\":{"
		    "\"clipRectMode\":\"clipRegion\","
		    "\"screenshare\":\"enabled\","
		    "\"multimonitorscreenshare\":\"enabled\","
		    "\"camera\":\"enabled\","
		    "\"microphone\":\"enabled\","
		    "\"unifiedplan\":\"enabled\","
		    "\"datachannel\":\"enabled\","
		    "\"backgroundeffects\":\"enabled\","
		    "\"e911info\":\"enabled\","
		    "\"givecontrolv2\":\"enabled\","
		    "\"givecontrol\":\"enabled\","
		    "\"appshare\":\"enabled\","
		    "\"appshareClientSupport\":\"enabled\","
		    "\"mirrormyselfvideo\":\"enabled\","
		    "\"secondaryringer\":\"enabled\","
		    "\"maxSimulcastLayers\":2}}}";

		return teamscap_send(callback, announce);
	}

	if (teamscap_contains(json, size, "\"setTeamsAppVolume\""))
	{
		WLog_INFO(TAG, "<<< setTeamsAppVolume — SILENT");
		return CHANNEL_RC_OK; /* no response */
	}

	/* --- Log unknown verbs for discovery (don't respond yet) --- */

	WLog_INFO(TAG, "<<< UNKNOWN VERB (captured, not responded): %.*s", (int)(size > 80 ? 80 : size),
	          json);

	return CHANNEL_RC_OK;
}

/**
 * Data received from the VM-side Teams agent: dump it and route it.
 */
static UINT teamscap_on_data_received(IWTSVirtualChannelCallback* pChannelCallback, wStream* data)
{
	TEAMSCAP_CHANNEL_CALLBACK* callback = (TEAMSCAP_CHANNEL_CALLBACK*)pChannelCallback;
	WINPR_ASSERT(callback);

	BYTE* pBuffer = Stream_Pointer(data);
	size_t cbSize = Stream_GetRemainingLength(data);

	/* Always capture */
	if (callback->rawFile && (cbSize > 0))
	{
		fwrite(pBuffer, 1, cbSize, callback->rawFile);
		fflush(callback->rawFile);
	}

	if (callback->metaFile)
	{
		struct timeval tv = { 0 };
		gettimeofday(&tv, NULL);
		callback->chunkCount++;
		callback->totalBytes += cbSize;
		fprintf(callback->metaFile,
		        "=== chunk #%" PRIu32 " t=%lld.%06lld size=%" PRIuz " total=%" PRIu64 " ===\n",
		        callback->chunkCount, (long long)tv.tv_sec, (long long)tv.tv_usec, cbSize,
		        callback->totalBytes);
		if (cbSize > 0)
			teamscap_hexdump(callback->metaFile, pBuffer, cbSize);
		fflush(callback->metaFile);
	}

	/* Phase 1: route and respond */
	UINT status = teamscap_route(callback, pBuffer, cbSize);

	Stream_Seek(data, cbSize);
	return status;
}

/**
 * Channel closed: flush and close capture files.
 */
static UINT teamscap_on_close(IWTSVirtualChannelCallback* pChannelCallback)
{
	TEAMSCAP_CHANNEL_CALLBACK* callback = (TEAMSCAP_CHANNEL_CALLBACK*)pChannelCallback;
	WINPR_ASSERT(callback);

	WLog_INFO(TAG, "Teams channel closed after %" PRIu32 " chunks, %" PRIu64 " bytes, %" PRIu32
	              " responses sent",
	          callback->chunkCount, callback->totalBytes, callback->responseCount);

	if (callback->metaFile)
	{
		fprintf(callback->metaFile,
		        "=== closed: %" PRIu32 " chunks, %" PRIu64 " bytes, %" PRIu32 " responses ===\n",
		        callback->chunkCount, callback->totalBytes, callback->responseCount);
		fclose(callback->metaFile);
	}
	if (callback->rawFile)
		fclose(callback->rawFile);

	free(callback);
	return CHANNEL_RC_OK;
}

/**
 * The VM-side Teams agent opened the channel: accept and start capturing.
 */
// NOLINTBEGIN(readability-non-const-parameter)
static UINT teamscap_on_new_channel_connection(IWTSListenerCallback* pListenerCallback,
                                               IWTSVirtualChannel* pChannel, BYTE* Data,
                                               BOOL* pbAccept,
                                               IWTSVirtualChannelCallback** ppCallback)
// NOLINTEND(readability-non-const-parameter)
{
	TEAMSCAP_LISTENER_CALLBACK* listener_callback = (TEAMSCAP_LISTENER_CALLBACK*)pListenerCallback;
	WINPR_ASSERT(listener_callback);
	/* Log the DVC open data (server's channel open payload) */
	if (Data)
		teamscap_log_hex("DVC open data", Data, 64);

	TEAMSCAP_CHANNEL_CALLBACK* callback =
	    (TEAMSCAP_CHANNEL_CALLBACK*)calloc(1, sizeof(TEAMSCAP_CHANNEL_CALLBACK));
	if (!callback)
	{
		WLog_ERR(TAG, "calloc failed!");
		return CHANNEL_RC_NO_MEMORY;
	}

	char ts[64] = { 0 };
	struct timeval tv = { 0 };
	gettimeofday(&tv, NULL);
	struct tm tmv = { 0 };
	localtime_r(&tv.tv_sec, &tmv);
	strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tmv);

	const char* base = getenv("TEAMSCAP_OUT");
	if (!base || (strlen(base) == 0))
		base = "/tmp/teamscap";

	char rawPath[1024] = { 0 };
	char metaPath[1024] = { 0 };
	snprintf(rawPath, sizeof(rawPath), "%s-%s.bin", base, ts);
	snprintf(metaPath, sizeof(metaPath), "%s-%s.log", base, ts);

	callback->rawFile = fopen(rawPath, "wb");
	callback->metaFile = fopen(metaPath, "w");

	WLog_INFO(TAG, "Teams channel '%s' OPENED - capture: %s (+ %s)", TEAMS_VDI10_CHANNEL_NAME,
	          rawPath, metaPath);

	if (callback->metaFile)
	{
		fprintf(callback->metaFile, "teamscap Phase 1 (dispatcher) capture %s\nchannel: %s\n", ts,
		        TEAMS_VDI10_CHANNEL_NAME);
		fflush(callback->metaFile);
	}

	if (pbAccept)
		*pbAccept = TRUE;

	GENERIC_CHANNEL_CALLBACK* generic = &callback->generic;
	generic->iface.OnDataReceived = teamscap_on_data_received;
	generic->iface.OnClose = teamscap_on_close;
	generic->plugin = listener_callback->plugin;
	generic->channel_mgr = listener_callback->channel_mgr;
	generic->channel = pChannel;
	callback->rdpcontext = listener_callback->rdpcontext;

	*ppCallback = &callback->generic.iface;
	return CHANNEL_RC_OK;
}

static UINT teamscap_plugin_initialize(IWTSPlugin* pPlugin, IWTSVirtualChannelManager* pChannelMgr)
{
	TEAMSCAP_PLUGIN* teamscap = (TEAMSCAP_PLUGIN*)pPlugin;
	WINPR_ASSERT(teamscap);
	WINPR_ASSERT(pChannelMgr);

	teamscap->listener_callback =
	    (TEAMSCAP_LISTENER_CALLBACK*)calloc(1, sizeof(TEAMSCAP_LISTENER_CALLBACK));
	if (!teamscap->listener_callback)
	{
		WLog_ERR(TAG, "calloc failed!");
		return CHANNEL_RC_NO_MEMORY;
	}

	teamscap->listener_callback->rdpcontext = teamscap->rdpcontext;
	teamscap->listener_callback->iface.OnNewChannelConnection = teamscap_on_new_channel_connection;
	teamscap->listener_callback->plugin = pPlugin;
	teamscap->listener_callback->channel_mgr = pChannelMgr;

	/* Primary: Teams VDI 1.0 WebRTC channel */
	WLog_INFO(TAG, "listening for Teams channel '%s' (Phase 1: dispatcher mode)",
	          TEAMS_VDI10_CHANNEL_NAME);
	UINT status = pChannelMgr->CreateListener(pChannelMgr, TEAMS_VDI10_CHANNEL_NAME, 0,
	                                   (IWTSListenerCallback*)teamscap->listener_callback, nullptr);
	if (status != CHANNEL_RC_OK)
		return status;

	/* Multi-channel hypothesis: the shim may check for ALL Windows App DVC channels.
	 * Register dummy listeners for MMR and WVR so the shim sees a "complete" endpoint. */
	TEAMSCAP_LISTENER_CALLBACK* mmr_callback =
	    (TEAMSCAP_LISTENER_CALLBACK*)calloc(1, sizeof(TEAMSCAP_LISTENER_CALLBACK));
	if (mmr_callback)
	{
		mmr_callback->rdpcontext = teamscap->rdpcontext;
		mmr_callback->iface.OnNewChannelConnection = teamscap_on_new_channel_connection;
		mmr_callback->plugin = pPlugin;
		mmr_callback->channel_mgr = pChannelMgr;
		status = pChannelMgr->CreateListener(pChannelMgr, "com.microsoft.rdc.dvc.mmr.1", 0,
		                                    (IWTSListenerCallback*)mmr_callback, nullptr);
		WLog_INFO(TAG, "listening for MMR channel 'com.microsoft.rdc.dvc.mmr.1' (status=%u)", status);
	}

	TEAMSCAP_LISTENER_CALLBACK* wvr_callback =
	    (TEAMSCAP_LISTENER_CALLBACK*)calloc(1, sizeof(TEAMSCAP_LISTENER_CALLBACK));
	if (wvr_callback)
	{
		wvr_callback->rdpcontext = teamscap->rdpcontext;
		wvr_callback->iface.OnNewChannelConnection = teamscap_on_new_channel_connection;
		wvr_callback->plugin = pPlugin;
		wvr_callback->channel_mgr = pChannelMgr;
		status = pChannelMgr->CreateListener(pChannelMgr, "com.microsoft.rdc.dvc.wvr.1", 0,
		                                    (IWTSListenerCallback*)wvr_callback, nullptr);
		WLog_INFO(TAG, "listening for WVR channel 'com.microsoft.rdc.dvc.wvr.1' (status=%u)", status);
	}

	return CHANNEL_RC_OK;
}

static UINT teamscap_plugin_terminated(IWTSPlugin* pPlugin)
{
	TEAMSCAP_PLUGIN* teamscap = (TEAMSCAP_PLUGIN*)pPlugin;
	free(teamscap->listener_callback);
	free(teamscap);
	return CHANNEL_RC_OK;
}

/**
 * Main entry point for teamscap DVC plugin
 */
FREERDP_ENTRY_POINT(UINT VCAPITYPE teamscap_DVCPluginEntry(IDRDYNVC_ENTRY_POINTS* pEntryPoints))
{
	UINT status = CHANNEL_RC_OK;
	WINPR_ASSERT(pEntryPoints);

	TEAMSCAP_PLUGIN* teamscap = (TEAMSCAP_PLUGIN*)pEntryPoints->GetPlugin(pEntryPoints, "teamscap");

	if (!teamscap)
	{
		teamscap = (TEAMSCAP_PLUGIN*)calloc(1, sizeof(TEAMSCAP_PLUGIN));
		if (!teamscap)
		{
			WLog_ERR(TAG, "calloc failed!");
			return CHANNEL_RC_NO_MEMORY;
		}

		teamscap->iface.Initialize = teamscap_plugin_initialize;
		teamscap->iface.Connected = nullptr;
		teamscap->iface.Disconnected = nullptr;
		teamscap->iface.Terminated = teamscap_plugin_terminated;
		teamscap->rdpcontext = pEntryPoints->GetRdpContext(pEntryPoints);
		status = pEntryPoints->RegisterPlugin(pEntryPoints, "teamscap", &teamscap->iface);
	}

	return status;
}
