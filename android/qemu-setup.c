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
#include "android/adb-qemud.h"
#include "android/adb-server.h"
#include "android/android.h"
#include "android/globals.h"
#include "android/hw-fingerprint.h"
#include "android/hw-sensors.h"
#include "android/proxy/proxy_http.h"
#include "android/utils/debug.h"
#include "android/utils/ipaddr.h"
#include "android/utils/path.h"
#include "android/utils/system.h"
#include "android/utils/bufprint.h"

#define  D(...)  do {  if (VERBOSE_CHECK(init)) dprint(__VA_ARGS__); } while (0)

#ifdef ANDROID_SDK_TOOLS_REVISION
#  define  VERSION_STRING  STRINGIFY(ANDROID_SDK_TOOLS_REVISION)".0"
#else
#  define  VERSION_STRING  "standalone"
#endif

/* Contains arguments for -android-ports option. */
char* android_op_ports = NULL;
/* Contains arguments for -android-port option. */
char* android_op_port = NULL;
/* Contains arguments for -android-report-console option. */
char* android_op_report_console = NULL;
/* Contains arguments for -http-proxy option. */
char* op_http_proxy = NULL;
/* Base port for the emulated system. */
int    android_base_port;
/* ADB port */
int    android_adb_port = 5037; // Default

enum {
    REPORT_CONSOLE_SERVER = (1 << 0),
    REPORT_CONSOLE_MAX    = (1 << 1)
};

static int
get_report_console_options( char*  end, int  *maxtries )
{
    int    flags = 0;

    if (end == NULL || *end == 0)
        return 0;

    if (end[0] != ',') {
        derror( "socket port/path can be followed by [,<option>]+ only\n");
        exit(3);
    }
    end += 1;
    while (*end) {
        char*  p = strchr(end, ',');
        if (p == NULL)
            p = end + strlen(end);

        if (memcmp( end, "server", p-end ) == 0)
            flags |= REPORT_CONSOLE_SERVER;
        else if (memcmp( end, "max=", 4) == 0) {
            end  += 4;
            *maxtries = strtol( end, NULL, 10 );
            flags |= REPORT_CONSOLE_MAX;
        } else {
            derror( "socket port/path can be followed by [,server][,max=<count>] only\n");
            exit(3);
        }

        end = p;
        if (*end)
            end += 1;
    }
    return flags;
}

static void
report_console( const char*  proto_port, int  console_port )
{
    int   s = -1, s2;
    int   maxtries = 10;
    int   flags = 0;
    signal_state_t  sigstate;

    disable_sigalrm( &sigstate );

    if ( !strncmp( proto_port, "tcp:", 4) ) {
        char*  end;
        long   port = strtol(proto_port + 4, &end, 10);

        flags = get_report_console_options( end, &maxtries );

        if (flags & REPORT_CONSOLE_SERVER) {
            s = socket_loopback_server( port, SOCKET_STREAM );
            if (s < 0) {
                fprintf(stderr, "could not create server socket on TCP:%ld: %s\n",
                        port, errno_str);
                exit(3);
            }
        } else {
            for ( ; maxtries > 0; maxtries-- ) {
                D("trying to find console-report client on tcp:%d", port);
                s = socket_loopback_client( port, SOCKET_STREAM );
                if (s >= 0)
                    break;

                sleep_ms(1000);
            }
            if (s < 0) {
                fprintf(stderr, "could not connect to server on TCP:%ld: %s\n",
                        port, errno_str);
                exit(3);
            }
        }
    } else if ( !strncmp( proto_port, "unix:", 5) ) {
#ifdef _WIN32
        fprintf(stderr, "sorry, the unix: protocol is not supported on Win32\n");
        exit(3);
#else
        char*  path = strdup(proto_port+5);
        char*  end  = strchr(path, ',');
        if (end != NULL) {
            flags = get_report_console_options( end, &maxtries );
            *end  = 0;
        }
        if (flags & REPORT_CONSOLE_SERVER) {
            s = socket_unix_server( path, SOCKET_STREAM );
            if (s < 0) {
                fprintf(stderr, "could not bind unix socket on '%s': %s\n",
                        proto_port+5, errno_str);
                exit(3);
            }
        } else {
            for ( ; maxtries > 0; maxtries-- ) {
                s = socket_unix_client( path, SOCKET_STREAM );
                if (s >= 0)
                    break;

                sleep_ms(1000);
            }
            if (s < 0) {
                fprintf(stderr, "could not connect to unix socket on '%s': %s\n",
                        path, errno_str);
                exit(3);
            }
        }
        free(path);
#endif
    } else {
        fprintf(stderr, "-report-console must be followed by a 'tcp:<port>' or 'unix:<path>'\n");
        exit(3);
    }

    if (flags & REPORT_CONSOLE_SERVER) {
        int  tries = 3;
        D( "waiting for console-reporting client" );
        do {
            s2 = socket_accept(s, NULL);
        } while (s2 < 0 && --tries > 0);

        if (s2 < 0) {
            fprintf(stderr, "could not accept console-reporting client connection: %s\n",
                   errno_str);
            exit(3);
        }

        socket_close(s);
        s = s2;
    }

    /* simply send the console port in text */
    {
        char  temp[12];
        snprintf( temp, sizeof(temp), "%d", console_port );

        if (socket_send(s, temp, strlen(temp)) < 0) {
            fprintf(stderr, "could not send console number report: %d: %s\n",
                    errno, errno_str );
            exit(3);
        }
        socket_close(s);
    }
    D( "console port number sent to remote. resuming boot" );

    restore_sigalrm (&sigstate);
}

static int qemu_control_console_start(int port, const QAndroidBatteryAgent* batteryAgent,
                                      const QAndroidFingerAgent* fingerAgent,
                                      const QAndroidLocationAgent* locationAgent,
                                      const QAndroidUserEventAgent* userEventAgent,
                                      const QAndroidVmOperations* vmOperations,
                                      const QAndroidNetAgent* netAgent) {
    return control_console_start(port, batteryAgent,
                                 fingerAgent, locationAgent,
                                 userEventAgent,
                                 vmOperations,
                                 netAgent);
}

/* this function is called from qemu_main() once all arguments have been parsed
 * it should be used to setup any Android-specific items in the emulation before the
 * main loop runs
 */
void android_emulation_setup(const QAndroidBatteryAgent* batteryAgent,
                             const QAndroidCellularAgent* cellularAgent,
                             const QAndroidFingerAgent* fingerAgent,
                             const QAndroidLocationAgent* locationAgent,
                             const QAndroidSensorsAgent* sensorsAgent,
                             const QAndroidTelephonyAgent* telephonyAgent,
                             const QAndroidUserEventAgent* userEventAgent,
                             const QAndroidVmOperations* vmOperations,
                             const QAndroidNetAgent* netAgent)
{
    int   tries     = MAX_ANDROID_EMULATORS;
    int   base_port = 5554;
    int   success   = 0;
    int   adb_port = -1;
    uint32_t  guest_ip;

        /* Set the port where the emulator expects adb to run on the host
         * machine */
    char* adb_host_port_str = getenv( "ANDROID_ADB_SERVER_PORT" );
    if ( adb_host_port_str && strlen( adb_host_port_str ) > 0 ) {
        android_adb_port = (int) strtol( adb_host_port_str, NULL, 0 );
        if ( android_adb_port <= 0 ) {
            derror( "env var ANDROID_ADB_SERVER_PORT must be a number > 0. Got \"%s\"\n",
                    adb_host_port_str );
            exit(1);
        }
    }

    inet_strtoip("10.0.2.15", &guest_ip);

#if 0
    if (opts->adb_port) {
        fprintf( stderr, "option -adb-port is obsolete, use -port instead\n" );
        exit(1);
    }
#endif

    if (android_op_port && android_op_ports) {
        fprintf( stderr, "options -port and -ports cannot be used together.\n");
        exit(1);
    }

#ifndef ANDROID_QEMU2_SPECIFIC
    int legacy_adb = avdInfo_getAdbdCommunicationMode(android_avdInfo) ? 0 : 1;

    if (android_op_ports) {
        char* comma_location;
        char* end;
        int console_port = strtol( android_op_ports, &comma_location, 0 );

        if ( comma_location == NULL || *comma_location != ',' ) {
            derror( "option -ports must be followed by two comma separated positive integer numbers" );
            exit(1);
        }

        adb_port = strtol( comma_location+1, &end, 0 );

        if ( end == NULL || *end ) {
            derror( "option -ports must be followed by two comma separated positive integer numbers" );
            exit(1);
        }

        if ( console_port == adb_port ) {
            derror( "option -ports must be followed by two different integer numbers" );
            exit(1);
        }

        // Set up redirect from host to guest system. adbd on the guest listens
        // on 5555.
        if (legacy_adb) {
            netAgent->slirpRedir(false, adb_port, guest_ip, 5555);
        } else {
            adb_server_init(adb_port);
            android_adb_service_init();
        }
        if (qemu_control_console_start(console_port, batteryAgent, fingerAgent,
                                       locationAgent, userEventAgent,
                                       vmOperations, netAgent) < 0) {
            if (legacy_adb) {
                netAgent->slirpUnredir(false, adb_port);
            }
        }

        base_port = console_port;
    } else {
        if (android_op_port) {
            char*  end;
            int    port = strtol( android_op_port, &end, 0 );
            if ( end == NULL || *end ||
                (unsigned)((port - base_port) >> 1) >= (unsigned)tries ) {
                derror( "option -port must be followed by an even integer number between %d and %d\n",
                        base_port, base_port + (tries-1)*2 );
                exit(1);
            }
            if ( (port & 1) != 0 ) {
                port &= ~1;
                dwarning( "option -port must be followed by an even integer, using  port number %d\n",
                          port );
            }
            base_port = port;
            tries     = 1;
        }

        for ( ; tries > 0; tries--, base_port += 2 ) {

            /* setup first redirection for ADB, the Android Debug Bridge */
            adb_port = base_port + 1;
            if (legacy_adb) {
                if (!netAgent->slirpRedir(false, adb_port, guest_ip, 5555))
                    continue;
            } else {
                if (adb_server_init(adb_port))
                    continue;
                android_adb_service_init();
            }

            /* setup second redirection for the emulator console */
            if (qemu_control_console_start(base_port, batteryAgent, fingerAgent,
                                           locationAgent, userEventAgent,
                                           vmOperations, netAgent) < 0) {
                if (legacy_adb) {
                    netAgent->slirpUnredir(false, adb_port);
                }
                continue;
            }

            D( "control console listening on port %d, ADB on port %d", base_port, adb_port );
            success = 1;
            break;
        }

        if (!success) {
            fprintf(stderr, "it seems too many emulator instances are running on this machine. Aborting\n" );
            exit(1);
        }
    }

    if (android_op_report_console) {
        report_console(android_op_report_console, base_port);
    }

    /* Save base port. */
    android_base_port = base_port;

    /* send a simple message to the ADB host server to tell it we just started.
     * it should be listening on port 5037. if we can't reach it, don't bother
     */
    int s = socket_loopback_client(android_adb_port, SOCKET_STREAM);
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
#endif

    telephonyAgent->initModem(base_port);

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
}
