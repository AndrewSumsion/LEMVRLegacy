/* Copyright (C) 2015 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#include "dns.h"

#include "android/utils/debug.h"
#include "android/utils/ipaddr.h"
#include "android/utils/sockets.h"

#ifdef _WIN32
#  include "android/base/sockets/Winsock.h"
#  include <iphlpapi.h>
#  include <vector>
#else
#  include <arpa/inet.h>
#  include <fstream>
#endif

#include <algorithm>
#include <string>

#ifdef _WIN32
int android_dns_get_system_servers(uint32_t* buffer, size_t bufferSize) {
    std::vector<char> fixedInfoBuffer(sizeof(FIXED_INFO));
    FIXED_INFO* fixedInfo = reinterpret_cast<FIXED_INFO*>(&fixedInfoBuffer[0]);
    ULONG bufLen = fixedInfoBuffer.size();

    if (ERROR_BUFFER_OVERFLOW == GetNetworkParams(fixedInfo, &bufLen)) {
        fixedInfoBuffer.resize(bufLen);
        fixedInfo = reinterpret_cast<FIXED_INFO*>(&fixedInfoBuffer[0]);
    }

    if (GetNetworkParams(fixedInfo, &bufLen) != ERROR_SUCCESS) {
        derror("Failed to get network parameters, cannot retrieve DNS servers");
        return kAndroidDnsErrorBadServer;
    }

    size_t dnsAddrCount = 0;
    for (IP_ADDR_STRING* ipAddr = &fixedInfo->DnsServerList;
            ipAddr && dnsAddrCount < bufferSize;
            ipAddr = ipAddr->Next) {
        uint32_t ip;
        if (inet_strtoip(ipAddr->IpAddress.String, &ip) == 0) {
            buffer[dnsAddrCount++] = ip;
        }
    }

    if (dnsAddrCount == 0)
        return kAndroidDnsErrorBadServer;

    return dnsAddrCount;
}

#else

int android_dns_get_system_servers(uint32_t* buffer, size_t bufferSize)
{
#ifdef CONFIG_DARWIN
    /* on Darwin /etc/resolv.conf is a symlink to /private/var/run/resolv.conf
     * in some siutations, the symlink can be destroyed and the system will not
     * re-create it. Darwin-aware applications will continue to run, but "legacy"
     * Unix ones will not.
     */
    std::ifstream fin("/private/var/run/resolv.conf");
    if (!fin.good()) {
        fin.open("/etc/resolv.conf");  /* desperate attempt to sanity */
    }
#else
    std::ifstream fin("/etc/resolv.conf");
#endif
    if (!fin.good()) {
        derror("Failed to open /etc/resolv.conf, cannot retrieve DNS servers");
        return kAndroidDnsErrorBadServer;
    }

    std::string line;
    size_t dnsAddrCount = 0;
    while (dnsAddrCount < bufferSize && std::getline(fin, line)) {
        char nameserver[257];
        if (sscanf(line.c_str(), "nameserver%*[ \t]%256s", nameserver) == 1) {
            uint32_t ip;
            if (inet_strtoip(nameserver, &ip) == 0) {
                buffer[dnsAddrCount++] = ip;
            }
        }
    }

    if (dnsAddrCount == 0)
        return kAndroidDnsErrorBadServer;

    return dnsAddrCount;
}

#endif

int android_dns_parse_servers(const char* input,
                              uint32_t* buffer,
                              size_t bufferSize) {
    std::string servers(input);

    std::replace(servers.begin(), servers.end(), ',', '\0');

    size_t dnsAddrCount = 0;
    for (size_t pos = 0; pos < servers.size();) {
        SockAddress addr;
        const char* server = &servers[pos];
        if (sock_address_init_resolve(&addr, server, 53, 0) < 0) {
            return kAndroidDnsErrorBadServer;
        }

        if (dnsAddrCount >= bufferSize) {
            return kAndroidDnsErrorTooManyServers;
        }

        int ip = sock_address_get_ip(&addr);
        if (ip == -1) {
            return kAndroidDnsErrorBadServer;
        }

        buffer[dnsAddrCount++] = static_cast<uint32_t>(ip);
        pos = servers.find('\0', pos);
        if (pos == std::string::npos) {
            break;
        }
        ++pos; // Skip the actual null terminator
    }
    return dnsAddrCount;
}

int android_dns_get_servers(const char* dnsServerOption,
                            uint32_t* dnsServerIps) {
    const int kMaxDnsServers = ANDROID_MAX_DNS_SERVERS;
    int dnsCount = 0;
    if (dnsServerOption && dnsServerOption[0]) {
        dnsCount = android_dns_parse_servers(dnsServerOption, dnsServerIps,
                                             kMaxDnsServers);
        if (dnsCount == kAndroidDnsErrorTooManyServers) {
            derror("Too may DNS servers listed in -dns-server option, a "
                   "maximum of %d values is supported\n",
                   ANDROID_MAX_DNS_SERVERS);
            return kAndroidDnsErrorTooManyServers;
        }
        if (dnsCount < 0) {  // Bad format in the option.
            derror("Malformed or invalid -dns-server parameter: %s",
                   dnsServerOption);
            return kAndroidDnsErrorBadServer;
        }
    }
    if (!dnsCount) {
        dnsCount = android_dns_get_system_servers(dnsServerIps, kMaxDnsServers);
        if (dnsCount < 0) {
            dnsCount = 0;
            dwarning(
                    "Cannot find system DNS servers! Name resolution will "
                    "be disabled.");
        }
    }
    if (VERBOSE_CHECK(init)) {
        dprintn("emulator: Found %d DNS servers:", dnsCount);
        for (int n = 0; n < dnsCount; ++n) {
            uint32_t ip = dnsServerIps[n];
            dprintn(" %d.%d.%d.%d", (uint8_t)(ip >> 24), (uint8_t)(ip >> 16),
                    (uint8_t)(ip >> 8), (uint8_t)(ip));
        }
        dprintn("\n");
    }

    return dnsCount;
}
