/* Copyright (C) 2006-2015 The Android Open Source Project
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

#include "android/console.h"
#include "android/constants.h"
#include "android/adb-qemud.h"
#include "android/adb-server.h"
#include "android/android.h"
#include "android/cmdline-option.h"
#include "android/console.h"
#include "android/globals.h"
#include "android/hw-fingerprint.h"
#include "android/hw-sensors.h"
#include "android/proxy/proxy_http.h"
#include "android/utils/debug.h"
#include "android/utils/ipaddr.h"
#include "android/utils/path.h"
#include "android/utils/system.h"
#include "android/utils/bufprint.h"
#include "android/version.h"

#include <stdbool.h>

#define  D(...)  do {  if (VERBOSE_CHECK(init)) dprint(__VA_ARGS__); } while (0)

/* Contains arguments for -android-ports option. */
char* android_op_ports = NULL;
/* Contains the parsed numbers from android_op_ports */
int android_op_ports_numbers[2] = {-1, -1};
/* Contains arguments for -android-port option. */
char* android_op_port = NULL;
/* Contains the parsed number from android_op_port */
int android_op_port_number = -1;
/* Contains arguments for -android-report-console option. */
char* android_op_report_console = NULL;
/* Contains arguments for -http-proxy option. */
char* op_http_proxy = NULL;
/* Base port for the emulated system. */
int    android_base_port;
/* ADB port */
int    android_adb_port = 5037; // Default

// Global configuration flags, 'true' to indicate that the AndroidEmu console
// code should be used, 'false' otherwise, which is the default used by QEMU2
// which currently provides its own console code.
static bool s_support_android_emu_console = false;

// Global configuration flag, 'true' to indicate that configurable ADB and
// console ports are supported (e.g. -ports <port1>,<port2> option). Note that
// this requires s_support_android_emu_console == true too to work properly.
static bool s_support_configurable_ports = false;

// The following code is used to support the -report-console option,
// which takes a parameter in one of the following formats:
//
//    tcp:<port>[,<options>]
//    unix:<path>[,<options>]
//
// Where <options> is a comma-separated list of options which can be
//    server        - Enable server mode (waits for client connection).
//    max=<count>   - Set maximum connection attempts (client mode only).
//    ipv6          - Use IPv6 localhost (::1) instead of (127.0.0.1)
//

// bit flags returned by get_report_console_options() below.
enum {
    REPORT_CONSOLE_SERVER = (1 << 0),
    REPORT_CONSOLE_MAX    = (1 << 1),
    REPORT_CONSOLE_IPV6   = (1 << 2),
};

// Look at |end| for a comma-separated list of -report-console options
// and return a set of corresponding bit flags. Return -1 on failure.
// On success, if REPORT_CONSOLE_MAX is set in the result, |*maxtries|
// will be updated with the <count> parameter of the max= option.
static int get_report_console_options(char* end, int* maxtries) {
    int flags = 0;

    if (end == NULL || *end == 0) {
        return 0;
    }

    if (end[0] != ',') {
        derror("socket port/path can be followed by [,<option>]+ only");
        return -1;
    }
    end += 1;
    while (*end) {
        char*  p = strchr(end, ',');
        if (p == NULL)
            p = end + strlen(end);

        if ((p - end) == strlen("server") && !memcmp(end, "server", p - end)) {
            flags |= REPORT_CONSOLE_SERVER;
        } else if (memcmp( end, "max=", 4) == 0) {
            end  += 4;
            *maxtries = strtol( end, NULL, 10 );
            flags |= REPORT_CONSOLE_MAX;
        } else if ((p - end) == strlen("ipv6") &&
                !memcmp(end, "ipv6", p - end)) {
            flags |= REPORT_CONSOLE_IPV6;
        } else {
            derror("socket port/path can be followed by "
                   "[,server][,max=<count>][,ipv6] only");
            return -1;
        }

        end = p;
        if (*end)
            end += 1;
    }
    return flags;
}

// Implement -report-console option. |proto_port| is the option's parameter
// as described above (e.g. 'tcp:<port>,server'). And |console_port| is
// the emulator's console port to report. Return 0 on success, -1 on failure.
static int report_console(const char* proto_port, int console_port) {
    int   s = -1, s2;
    int   maxtries = 10;
    int   flags = 0;
    signal_state_t  sigstate;

    disable_sigalrm( &sigstate );

    if ( !strncmp( proto_port, "tcp:", 4) ) {
        char*  end;
        long   port = strtol(proto_port + 4, &end, 10);

        flags = get_report_console_options( end, &maxtries );
        if (flags < 0) {
            return -1;
        }

        if (flags & REPORT_CONSOLE_SERVER) {
            // TODO: Listen on both IPv6 and IPv4 interfaces at the same time?
            if (flags & REPORT_CONSOLE_IPV6) {
                s = socket_loopback6_server( port, SOCKET_STREAM );
            } else {
                s = socket_loopback4_server( port, SOCKET_STREAM );
            }
            if (s < 0) {
                derror("could not create server socket on TCP:%ld: %s", port,
                       errno_str);
                return -1;
            }
        } else {
            for ( ; maxtries > 0; maxtries-- ) {
                D("trying to find console-report client on tcp:%d", port);
                if (flags & REPORT_CONSOLE_IPV6) {
                    s = socket_loopback6_client( port, SOCKET_STREAM );
                } else {
                    s = socket_loopback4_client( port, SOCKET_STREAM );
                }
                if (s >= 0)
                    break;

                sleep_ms(1000);
            }
            if (s < 0) {
                derror("could not connect to server on TCP:%ld: %s", port,
                       errno_str);
                return -1;
            }
        }
    } else if ( !strncmp( proto_port, "unix:", 5) ) {
#ifdef _WIN32
        derror("sorry, the unix: protocol is not supported on Win32");
        return -1;
#else
        char*  path = strdup(proto_port+5);
        char*  end  = strchr(path, ',');
        if (end != NULL) {
            flags = get_report_console_options( end, &maxtries );
            if (flags < 0) {
                free(path);
                return -1;
            }
            *end  = 0;
        }
        if (flags & REPORT_CONSOLE_SERVER) {
            s = socket_unix_server( path, SOCKET_STREAM );
            if (s < 0) {
                derror("could not bind unix socket on '%s': %s", proto_port + 5,
                       errno_str);
                return -1;
            }
        } else {
            for ( ; maxtries > 0; maxtries-- ) {
                s = socket_unix_client( path, SOCKET_STREAM );
                if (s >= 0)
                    break;

                sleep_ms(1000);
            }
            if (s < 0) {
                derror("could not connect to unix socket on '%s': %s", path,
                       errno_str);
                return -1;
            }
        }
        free(path);
#endif
    } else {
        derror("-report-console must be followed by a 'tcp:<port>' or "
               "'unix:<path>'");
        return -1;
    }

    if (flags & REPORT_CONSOLE_SERVER) {
        int  tries = 3;
        D( "waiting for console-reporting client" );
        do {
            s2 = socket_accept(s, NULL);
        } while (s2 < 0 && --tries > 0);

        if (s2 < 0) {
            derror("could not accept console-reporting client connection: %s",
                   errno_str);
            return -1;
        }

        socket_close(s);
        s = s2;
    }

    /* simply send the console port in text */
    {
        char  temp[12];
        snprintf( temp, sizeof(temp), "%d", console_port );

        if (socket_send(s, temp, strlen(temp)) < 0) {
            derror("could not send console number report: %d: %s", errno,
                   errno_str);
            return -1;
        }
        socket_close(s);
    }
    D( "console port number sent to remote. resuming boot" );

    restore_sigalrm (&sigstate);
    return 0;
}

static int qemu_android_console_start(int port,
                                      const AndroidConsoleAgents* agents) {
    if (!s_support_android_emu_console) {
        return 0;
    }

    return android_console_start(port, agents);
}

void android_emulation_setup_use_android_emu_console(bool enabled) {
    s_support_android_emu_console = enabled;
}

void android_emulation_setup_use_configurable_ports(bool enabled) {
    s_support_configurable_ports = enabled;
}

// Try to bind to specific |console_port| and |adb_port| on the localhost
// interface. |legacy_adb| is true iff the legacy ADB network redirection
// through guest:tcp:5555 must also be setup.
//
// Returns true on success, false otherwise. Note that failure is clean, i.e.
// it won't leave ports bound by mistake.
static bool setup_console_and_adb_ports(int console_port,
                                        int adb_port,
                                        bool legacy_adb,
                                        const AndroidConsoleAgents* agents) {
    bool register_adb_service = false;
    // The guest IP that ADB listens to in legacy mode.
    uint32_t guest_ip;
    inet_strtoip("10.0.2.15", &guest_ip);

    if (legacy_adb) {
        agents->net->slirpRedir(false, adb_port, guest_ip, 5555);
    } else {
        if (adb_server_init(adb_port) < 0) {
            return false;
        }
        register_adb_service = true;
    }
    if (qemu_android_console_start(console_port, agents) < 0) {
        if (legacy_adb) {
            agents->net->slirpUnredir(false, adb_port);
        } else {
            register_adb_service = false;
            adb_server_undo_init();
        }
        return false;
    }
    if (register_adb_service) {
        android_adb_service_init();
    }
    return true;
}

/* this function is called from qemu_main() once all arguments have been parsed
 * it should be used to setup any Android-specific items in the emulation before the
 * main loop runs
 */
bool android_emulation_setup(const AndroidConsoleAgents* agents) {
    /* Set the port where the emulator expects adb to run on the host
     * machine */
    char* adb_host_port_str = getenv( "ANDROID_ADB_SERVER_PORT" );
    if ( adb_host_port_str && strlen( adb_host_port_str ) > 0 ) {
        android_adb_port = (int) strtol( adb_host_port_str, NULL, 0 );
        if ( android_adb_port <= 0 ) {
            derror("env var ANDROID_ADB_SERVER_PORT must be a number > 0. Got "
                   "\"%s\"",
                   adb_host_port_str);
            return false;
        }
    }

    if (android_op_port && android_op_ports) {
        derror("options -port and -ports cannot be used together.");
        return false;
    }

    if (s_support_configurable_ports) {
        int tries = MAX_ANDROID_EMULATORS;
        int success   = 0;
        int adb_port = -1;
        int base_port = ANDROID_CONSOLE_BASEPORT;
        int legacy_adb = avdInfo_getAdbdCommunicationMode(android_avdInfo) ? 0 : 1;

        if (android_op_ports) {
            int console_port = -1;
            if (!android_parse_ports_option(android_op_ports,
                                            &console_port,
                                            &adb_port)) {
                return false;
            }

            setup_console_and_adb_ports(console_port, adb_port, legacy_adb,
                                        agents);

            base_port = console_port;
        } else {
            if (android_op_port) {
                if (!android_parse_port_option(android_op_port, &base_port)) {
                    return false;
                }
                tries     = 1;
            }

            for ( ; tries > 0; tries--, base_port += 2 ) {

                /* setup first redirection for ADB, the Android Debug Bridge */
                adb_port = base_port + 1;

                if (!setup_console_and_adb_ports(base_port, adb_port,
                                                 legacy_adb, agents)) {
                    continue;
                }

                D( "control console listening on port %d, ADB on port %d", base_port, adb_port );
                success = 1;
                break;
            }

            if (!success) {
                derror("It seems too many emulator instances are running on "
                       "this machine. Aborting.");
                return false;
            }
        }

        if (android_op_report_console) {
            if (report_console(android_op_report_console, base_port) < 0) {
                return false;
            }
        }

        /* Save base port. */
        android_base_port = base_port;

        /* send a simple message to the ADB host server to tell it we just started.
        * it should be listening on port 5037. if we can't reach it, don't bother
        */
        int s = socket_loopback6_client(android_adb_port, SOCKET_STREAM);
        if (s < 0) {
            s = socket_loopback4_client(android_adb_port, SOCKET_STREAM);
        }
        if (s < 0) {
            D("can't connect to ADB server: %s (errno = %d)", errno_str, errno );
        } else {
            char tmp[32];
            char header[5];

            // Expected format: <hex4>host:emulator:<port>
            // Where <port> is the decimal adb port number, and <hex4> is the length
            // of the payload that follows it in hex.
            int len = snprintf(tmp, sizeof tmp, "0000host:emulator:%d", adb_port);
            snprintf(header, sizeof header, "%04x", len - 4);
            memcpy(tmp, header, 4);
            socket_send(s, tmp, len);
            D("sent '%s' to ADB server", tmp);

            socket_close(s);
        }
    }

    agents->telephony->initModem(android_base_port);

    /* setup the http proxy, if any */
    if (VERBOSE_CHECK(proxy))
        proxy_set_verbose(1);

    if (!op_http_proxy) {
        op_http_proxy = getenv("http_proxy");
    }

    do
    {
        const char*  env = op_http_proxy;
        int          envlen;
        ProxyOption  option_tab[4];
        ProxyOption* option = option_tab;
        char*        p;
        char*        q;
        const char*  proxy_name;
        int          proxy_name_len;
        int          proxy_port;

        if (!env)
            break;

        envlen = strlen(env);

        /* skip the 'http://' header, if present */
        if (envlen >= 7 && !memcmp(env, "http://", 7)) {
            env    += 7;
            envlen -= 7;
        }

        /* do we have a username:password pair ? */
        p = strchr(env, '@');
        if (p != 0) {
            q = strchr(env, ':');
            if (q == NULL) {
            BadHttpProxyFormat:
                dprint("http_proxy format unsupported, try 'proxy:port' or 'username:password@proxy:port'");
                break;
            }

            option->type       = PROXY_OPTION_AUTH_USERNAME;
            option->string     = env;
            option->string_len = q - env;
            option++;

            option->type       = PROXY_OPTION_AUTH_PASSWORD;
            option->string     = q+1;
            option->string_len = p - (q+1);
            option++;

            env = p+1;
        }

        p = strchr(env,':');
        if (p == NULL)
            goto BadHttpProxyFormat;

        proxy_name     = env;
        proxy_name_len = p - env;
        proxy_port     = atoi(p+1);

        D( "setting up http proxy:  server=%.*s port=%d",
                proxy_name_len, proxy_name, proxy_port );

        /* Check that we can connect to the proxy in the next second.
         * If not, the proxy setting is probably garbage !!
         */
        if ( proxy_check_connection( proxy_name, proxy_name_len, proxy_port, 1000 ) < 0) {
            dprint("Could not connect to proxy at %.*s:%d: %s !",
                   proxy_name_len, proxy_name, proxy_port, errno_str);
            dprint("Proxy will be ignored !");
            break;
        }

        if ( proxy_http_setup( proxy_name, proxy_name_len, proxy_port,
                               option - option_tab, option_tab ) < 0 )
        {
            dprint( "Http proxy setup failed for '%.*s:%d': %s",
                    proxy_name_len, proxy_name, proxy_port, errno_str);
            dprint( "Proxy will be ignored !");
        }
    }
    while (0);

    /* initialize sensors, this must be done here due to timer issues */
    android_hw_sensors_init();

    /* initilize fingperprint here */
    android_hw_fingerprint_init();

    return true;
}
