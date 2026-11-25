/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Protocol constants for the Microsoft Dev Box / Windows 365 client pipeline.
 *
 * Sources: live-validated on 2026-11-24 (see PROTOCOL-CONNECTION.md in the
 * WindowsApp research project). The RD Web client identity (a85cf173-...) is
 * the one tenant Conditional Access allows from unmanaged Linux endpoints;
 * the Windows 365 Client identity (4fb5cc57-...) is used by the Windows App
 * with the WAM broker and may be blocked for browser flows.
 */
#pragma once

#include <QString>

namespace MsDevBox::Constants {

/* OAuth client identities */
inline constexpr const char* ClientIdRdWeb = "a85cf173-4192-42f8-81fa-777a763e6e2c";
inline constexpr const char* ClientIdWindows365 = "4fb5cc57-dbbc-4cdc-9595-748adff5f414";

/* Interactive auth-code + PKCE endpoints */
inline constexpr const char* Authority = "https://login.microsoftonline.com/common";
inline constexpr const char* AuthorizePath = "/oauth2/v2.0/authorize";
inline constexpr const char* TokenPath = "/oauth2/v2.0/token";
inline constexpr const char* RedirectUri = "https://login.microsoftonline.com/common/oauth2/nativeclient";

/* Scopes */
inline constexpr const char* ScopeRdWeb = "https://www.wvd.microsoft.com/.default openid profile offline_access";
inline constexpr const char* ScopeEndUser = "7c0a6aea-533c-458c-9f81-15568f10f6e4/EndUser.Access openid profile offline_access";

/* Windows 365 EndUser API ("Deschutes") — serves Dev Boxes as CloudPC-shaped entities */
inline constexpr const char* CloudPcsUrl =
    "https://windows365.microsoft.com/u/api/v2/cloudPCs?extend=IsDefault&licenseCategories=Dedicated,Flex,FrontlineShared";

/* RDWeb ARM feeddiscovery — needs the RdWeb token AND x-ms-user-agent */
inline constexpr const char* FeedDiscoveryUrlTemplate =
    "https://rdweb.wvd.microsoft.com/api/arm/feeddiscovery/tenants/%1/app/%2";

/* REQUIRED on all RDWeb endpoints; without it: 400 INCOMPATIBLE_CLIENT_VERSION */
inline constexpr const char* RdWebUserAgentHeader = "x-ms-user-agent";
inline constexpr const char* RdWebUserAgent = "win365.nxt/3.33.1";

/* EndUser API client headers */
inline constexpr const char* EndUserSourceClient = "NxtClient";
inline constexpr const char* EndUserClientVersion = "2.0.1601.0";

/* Partner GUIDs (CloudPC.partnerId): legacy DevBox live-confirmed; DevBoxV2 per RE */
inline constexpr const char* PartnerDevBox = "e3171dd9-9a5f-e5be-b36c-cc7c4f3f3bcf";
inline constexpr const char* PartnerDevBoxV2 = "10099236-61c3-44e0-b9a8-c1036643ae26";

/* Default .rdp output name */
inline const QString defaultRdpFileName()
{
    return QStringLiteral("devbox.rdp");
}

} // namespace
