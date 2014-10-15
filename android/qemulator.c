/* Copyright (C) 2006-2010 The Android Open Source Project
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

#include "android/qemulator.h"

#include "android/android.h"
#include "android/framebuffer.h"
#include "android/globals.h"
#include "android/hw-control.h"
#include "android/hw-sensors.h"
#include "android/opengles.h"
#include "android/skin/keycode.h"
#include "android/skin/winsys.h"
#include "android/user-events.h"
#include "android/utils/debug.h"
#include "android/utils/bufprint.h"
#include "telephony/modem_driver.h"

#define  D(...)  do {  if (VERBOSE_CHECK(init)) dprint(__VA_ARGS__); } while (0)

static double get_default_scale( AndroidOptions*  opts );

/* EmulatorWindow structure instance. */
static EmulatorWindow   qemulator[1];

static void handle_key_command( void*  opaque, SkinKeyCommand  command, int  param );
static void emulator_window_refresh(EmulatorWindow* emulator);
extern void qemu_system_shutdown_request(void);

static void
emulator_window_light_brightness( void* opaque, const char*  light, int  value )
{
    EmulatorWindow*  emulator = opaque;

    VERBOSE_PRINT(hw_control,"%s: light='%s' value=%d window=%p", __FUNCTION__, light, value, emulator->window);
    if ( !strcmp(light, "lcd_backlight") ) {
        emulator->lcd_brightness = value;
        if (emulator->window)
            skin_window_set_lcd_brightness( emulator->window, value );
        return;
    }
}

static void emulator_window_trackball_event(int dx, int dy) {
    user_event_mouse(dx, dy, 1, 0);
}

static void emulator_window_window_key_event(unsigned keycode, int down) {
    user_event_key(keycode, down);
}

static void emulator_window_window_mouse_event(unsigned x,
                                         unsigned y,
                                         unsigned state) {
    /* NOTE: the 0 is used in hw/android/goldfish/events_device.c to
     * differentiate between a touch-screen and a trackball event
     */
    user_event_mouse(x, y, 0, state);
}

static void emulator_window_window_generic_event(int event_type,
                                           int event_code,
                                           int event_value) {
    user_event_generic(event_type, event_code, event_value);
    /* XXX: hack, replace by better code here */
    if (event_value != 0)
        android_sensors_set_coarse_orientation(ANDROID_COARSE_PORTRAIT);
    else
        android_sensors_set_coarse_orientation(ANDROID_COARSE_LANDSCAPE);
}

static void
emulator_window_setup( EmulatorWindow*  emulator )
{
    AndroidOptions*  opts = emulator->opts;

    if ( !emulator->window && !opts->no_window ) {
        SkinLayout*  layout = emulator->layout;
        double       scale  = get_default_scale(emulator->opts);

        static const SkinWindowFuncs skin_window_funcs = {
            .key_event = &emulator_window_window_key_event,
            .mouse_event = &emulator_window_window_mouse_event,
            .generic_event = &emulator_window_window_generic_event,
            .opengles_show = &android_showOpenglesWindow,
            .opengles_hide = &android_hideOpenglesWindow,
            .opengles_redraw = &android_redrawOpenglesWindow,
        };

        emulator->window = skin_window_create(layout,
                                              emulator->win_x,
                                              emulator->win_y,
                                              scale,
                                              0,
                                              &skin_window_funcs);
        if (emulator->window == NULL)
            return;

        {
            SkinTrackBall*           ball;
            SkinTrackBallParameters  params;

            params.diameter   = 30;
            params.ring       = 2;
            params.ball_color = 0xffe0e0e0;
            params.dot_color  = 0xff202020;
            params.ring_color = 0xff000000;
            params.event_func = &emulator_window_trackball_event;

            ball = skin_trackball_create( &params );
            emulator->trackball = ball;
            skin_window_set_trackball( emulator->window, ball );

            emulator->lcd_brightness = 128;  /* 50% */
            skin_window_set_lcd_brightness( emulator->window, emulator->lcd_brightness );
        }

        if ( emulator->onion != NULL )
            skin_window_set_onion( emulator->window,
                                   emulator->onion,
                                   emulator->onion_rotation,
                                   emulator->onion_alpha );

        emulator_window_set_title(emulator);

        skin_window_enable_touch ( emulator->window,
                                   !androidHwConfig_isScreenNoTouch(android_hw));
        skin_window_enable_dpad  ( emulator->window, android_hw->hw_dPad != 0 );
        skin_window_enable_qwerty( emulator->window, android_hw->hw_keyboard != 0 );
        skin_window_enable_trackball( emulator->window, android_hw->hw_trackBall != 0 );
    }

    /* initialize hardware control support */
    AndroidHwControlFuncs funcs;
    funcs.light_brightness = emulator_window_light_brightness;
    android_hw_control_set(emulator, &funcs);
}

static void
emulator_window_fb_update( void*   _emulator, int  x, int  y, int  w, int  h )
{
    EmulatorWindow*  emulator = _emulator;

    if (!emulator->window) {
        if (emulator->opts->no_window)
            return;
        emulator_window_setup( emulator );
    }
    skin_window_update_display( emulator->window, x, y, w, h );
}

static void
emulator_window_fb_rotate( void*  _emulator, int  rotation )
{
    EmulatorWindow*  emulator = _emulator;

    emulator_window_setup( emulator );
}

static void
emulator_window_fb_poll( void* _emulator )
{
    EmulatorWindow* emulator = _emulator;
    emulator_window_refresh(emulator);
}

EmulatorWindow*
emulator_window_get(void)
{
    return qemulator;
}

static void emulator_window_framebuffer_free(void* opaque) {
    QFrameBuffer* fb = opaque;

    qframebuffer_done(fb);
    free(fb);
}

static void* emulator_window_framebuffer_create(int width, int height, int bpp) {
    QFrameBuffer* fb = calloc(1, sizeof(*fb));

    qframebuffer_init(fb, width, height, 0,
                      bpp == 32 ? QFRAME_BUFFER_RGBX_8888
                                : QFRAME_BUFFER_RGB565 );

    qframebuffer_fifo_add(fb);
    return fb;
}

static void* emulator_window_framebuffer_get_pixels(void* opaque) {
    QFrameBuffer* fb = opaque;
    return fb->pixels;
}

static int emulator_window_framebuffer_get_depth(void* opaque) {
    QFrameBuffer* fb = opaque;
    return fb->bits_per_pixel;
}

int
emulator_window_init( EmulatorWindow*       emulator,
                AConfig*         aconfig,
                const char*      basepath,
                int              x,
                int              y,
                AndroidOptions*  opts )
{
    static const SkinFramebufferFuncs skin_fb_funcs = {
        .create_framebuffer = &emulator_window_framebuffer_create,
        .free_framebuffer = &emulator_window_framebuffer_free,
        .get_pixels = &emulator_window_framebuffer_get_pixels,
        .get_depth = &emulator_window_framebuffer_get_depth,
    };

    emulator->aconfig     = aconfig;
    emulator->layout_file =
            skin_file_create_from_aconfig(aconfig,
                                          basepath,
                                          &skin_fb_funcs);

    emulator->layout      = emulator->layout_file->layouts;
    emulator->keyboard    = skin_keyboard_create(opts->charmap,
                                                 opts->raw_keys,
                                                 &user_event_keycodes);
    emulator->window      = NULL;
    emulator->win_x       = x;
    emulator->win_y       = y;
    emulator->opts[0]     = opts[0];

    /* register as a framebuffer clients for all displays defined in the skin file */
    SKIN_FILE_LOOP_PARTS( emulator->layout_file, part )
        SkinDisplay*  disp = part->display;
        if (disp->valid) {
            qframebuffer_add_client( disp->framebuffer,
                                     emulator,
                                     emulator_window_fb_update,
                                     emulator_window_fb_rotate,
                                     emulator_window_fb_poll,
                                     NULL );
        }
    SKIN_FILE_LOOP_END_PARTS

    skin_keyboard_enable( emulator->keyboard, 1 );
    skin_keyboard_on_command( emulator->keyboard, handle_key_command, emulator );

    return 0;
}

void
emulator_window_done(EmulatorWindow* emulator)
{
    if (emulator->window) {
        skin_window_free(emulator->window);
        emulator->window = NULL;
    }
    if (emulator->trackball) {
        skin_trackball_destroy(emulator->trackball);
        emulator->trackball = NULL;
    }
    if (emulator->keyboard) {
        skin_keyboard_free(emulator->keyboard);
        emulator->keyboard = NULL;
    }
    emulator->layout = NULL;
    if (emulator->layout_file) {
        skin_file_free(emulator->layout_file);
        emulator->layout_file = NULL;
    }
}

SkinLayout*
emulator_window_get_layout(EmulatorWindow* emulator)
{
    return emulator->layout;
}

QFrameBuffer*
emulator_window_get_first_framebuffer(EmulatorWindow* emulator)
{
    /* register as a framebuffer clients for all displays defined in the skin file */
    SKIN_FILE_LOOP_PARTS( emulator->layout_file, part )
        SkinDisplay*  disp = part->display;
        if (disp->valid) {
            return disp->framebuffer;
        }
    SKIN_FILE_LOOP_END_PARTS
    return NULL;
}

void
emulator_window_set_title(EmulatorWindow* emulator)
{
    char  temp[128], *p=temp, *end=p+sizeof temp;;

    if (emulator->window == NULL)
        return;

    if (emulator->show_trackball) {
        SkinKeyBinding  bindings[ SKIN_KEY_COMMAND_MAX_BINDINGS ];
        int             count;

        count = skin_keyset_get_bindings( skin_keyset_get_default(),
                                          SKIN_KEY_COMMAND_TOGGLE_TRACKBALL,
                                          bindings );

        if (count > 0) {
            int  nn;
            p = bufprint( p, end, "Press " );
            for (nn = 0; nn < count; nn++) {
                if (nn > 0) {
                    if (nn < count-1)
                        p = bufprint(p, end, ", ");
                    else
                        p = bufprint(p, end, " or ");
                }
                p = bufprint(p, end, "%s",
                             skin_key_pair_to_string(bindings[nn].sym,
                                                     bindings[nn].mod) );
            }
            p = bufprint(p, end, " to leave trackball mode. ");
        }
    }

    p = bufprint(p, end, "%d:%s",
                 android_base_port,
                 avdInfo_getName( android_avdInfo ));

    skin_window_set_title( emulator->window, temp );
}

/*
 * Helper routines
 */

static int
get_device_dpi( AndroidOptions*  opts )
{
    int dpi_device = android_hw->hw_lcd_density;

    if (opts->dpi_device != NULL) {
        char*  end;
        dpi_device = strtol( opts->dpi_device, &end, 0 );
        if (end == NULL || *end != 0 || dpi_device <= 0) {
            fprintf(stderr, "argument for -dpi-device must be a positive integer. Aborting\n" );
            exit(1);
        }
    }
    return  dpi_device;
}

static double
get_default_scale( AndroidOptions*  opts )
{
    int     dpi_device  = get_device_dpi( opts );
    int     dpi_monitor = -1;
    double  scale       = 0.0;

    /* possible values for the 'scale' option are
     *   'auto'        : try to determine the scale automatically
     *   '<number>dpi' : indicates the host monitor dpi, compute scale accordingly
     *   '<fraction>'  : use direct scale coefficient
     */

    if (opts->scale) {
        if (!strcmp(opts->scale, "auto"))
        {
            /* we need to get the host dpi resolution ? */
            int   xdpi, ydpi;

            if (skin_winsys_get_monitor_dpi(&xdpi, &ydpi) < 0) {
                fprintf(stderr, "could not get monitor DPI resolution from system. please use -dpi-monitor to specify one\n" );
                exit(1);
            }
            D( "system reported monitor resolutions: xdpi=%d ydpi=%d\n", xdpi, ydpi);
            dpi_monitor = (xdpi + ydpi+1)/2;
        }
        else
        {
            char*   end;
            scale = strtod( opts->scale, &end );

            if (end && end[0] == 'd' && end[1] == 'p' && end[2] == 'i' && end[3] == 0) {
                if ( scale < 20 || scale > 1000 ) {
                    fprintf(stderr, "emulator: ignoring bad -scale argument '%s': %s\n", opts->scale,
                            "host dpi number must be between 20 and 1000" );
                    exit(1);
                }
                dpi_monitor = scale;
                scale       = 0.0;
            }
            else if (end == NULL || *end != 0) {
                fprintf(stderr, "emulator: ignoring bad -scale argument '%s': %s\n", opts->scale,
                        "not a number or the 'auto' keyword" );
                exit(1);
            }
            else if ( scale < 0.1 || scale > 3. ) {
                fprintf(stderr, "emulator: ignoring bad -window-scale argument '%s': %s\n", opts->scale,
                        "must be between 0.1 and 3.0" );
                exit(1);
            }
        }
    }

    if (scale == 0.0 && dpi_monitor > 0)
        scale = dpi_monitor*1.0/dpi_device;

    return scale;
}

/* used to respond to a given keyboard command shortcut
 */
static void
handle_key_command( void*  opaque, SkinKeyCommand  command, int  down )
{
    static const struct { SkinKeyCommand  cmd; SkinKeyCode  kcode; }  keycodes[] =
    {
        { SKIN_KEY_COMMAND_BUTTON_CALL,        kKeyCodeCall },
        { SKIN_KEY_COMMAND_BUTTON_HOME,        kKeyCodeHome },
        { SKIN_KEY_COMMAND_BUTTON_BACK,        kKeyCodeBack },
        { SKIN_KEY_COMMAND_BUTTON_HANGUP,      kKeyCodeEndCall },
        { SKIN_KEY_COMMAND_BUTTON_POWER,       kKeyCodePower },
        { SKIN_KEY_COMMAND_BUTTON_SEARCH,      kKeyCodeSearch },
        { SKIN_KEY_COMMAND_BUTTON_MENU,        kKeyCodeMenu },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_UP,     kKeyCodeDpadUp },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_LEFT,   kKeyCodeDpadLeft },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_RIGHT,  kKeyCodeDpadRight },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_DOWN,   kKeyCodeDpadDown },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_CENTER, kKeyCodeDpadCenter },
        { SKIN_KEY_COMMAND_BUTTON_VOLUME_UP,   kKeyCodeVolumeUp },
        { SKIN_KEY_COMMAND_BUTTON_VOLUME_DOWN, kKeyCodeVolumeDown },
        { SKIN_KEY_COMMAND_BUTTON_CAMERA,      kKeyCodeCamera },
        { SKIN_KEY_COMMAND_BUTTON_TV,          kKeyCodeTV },
        { SKIN_KEY_COMMAND_BUTTON_EPG,         kKeyCodeEPG },
        { SKIN_KEY_COMMAND_BUTTON_DVR,         kKeyCodeDVR },
        { SKIN_KEY_COMMAND_BUTTON_PREV,        kKeyCodePrevious },
        { SKIN_KEY_COMMAND_BUTTON_NEXT,        kKeyCodeNext },
        { SKIN_KEY_COMMAND_BUTTON_PLAY,        kKeyCodePlay },
        { SKIN_KEY_COMMAND_BUTTON_PAUSE,       kKeyCodePause },
        { SKIN_KEY_COMMAND_BUTTON_STOP,        kKeyCodeStop },
        { SKIN_KEY_COMMAND_BUTTON_REWIND,      kKeyCodeRewind },
        { SKIN_KEY_COMMAND_BUTTON_FFWD,        kKeyCodeFastForward },
        { SKIN_KEY_COMMAND_BUTTON_BOOKMARKS,   kKeyCodeBookmarks },
        { SKIN_KEY_COMMAND_BUTTON_WINDOW,      kKeyCodeCycleWindows },
        { SKIN_KEY_COMMAND_BUTTON_CHANNELUP,   kKeyCodeChannelUp },
        { SKIN_KEY_COMMAND_BUTTON_CHANNELDOWN, kKeyCodeChannelDown },
        { SKIN_KEY_COMMAND_NONE, 0 }
    };
    int          nn;
    EmulatorWindow*   emulator = opaque;


    for (nn = 0; keycodes[nn].kcode != 0; nn++) {
        if (command == keycodes[nn].cmd) {
            unsigned  code = keycodes[nn].kcode;
            if (down)
                code |= 0x200;
            user_event_keycode( code );
            return;
        }
    }

    // for the show-trackball command, handle down events to enable, and
    // up events to disable
    if (command == SKIN_KEY_COMMAND_SHOW_TRACKBALL) {
        emulator->show_trackball = (down != 0);
        skin_window_show_trackball( emulator->window, emulator->show_trackball );
        //emulator_window_set_title( emulator );
        return;
    }

    // only handle down events for the rest
    if (down == 0)
        return;

    switch (command)
    {
    case SKIN_KEY_COMMAND_TOGGLE_NETWORK:
        {
            qemu_net_disable = !qemu_net_disable;
            if (android_modem) {
                amodem_set_data_registration(
                        android_modem,
                qemu_net_disable ? A_REGISTRATION_UNREGISTERED
                    : A_REGISTRATION_HOME);
            }
            D( "network is now %s", qemu_net_disable ?
                                    "disconnected" : "connected" );
        }
        break;

    case SKIN_KEY_COMMAND_TOGGLE_FULLSCREEN:
        if (emulator->window) {
            skin_window_toggle_fullscreen(emulator->window);
        }
        break;

    case SKIN_KEY_COMMAND_TOGGLE_TRACKBALL:
        emulator->show_trackball = !emulator->show_trackball;
        skin_window_show_trackball( emulator->window, emulator->show_trackball );
        emulator_window_set_title(emulator);
        break;

    case SKIN_KEY_COMMAND_ONION_ALPHA_UP:
    case SKIN_KEY_COMMAND_ONION_ALPHA_DOWN:
        if (emulator->onion)
        {
            int  alpha = emulator->onion_alpha;

            if (command == SKIN_KEY_COMMAND_ONION_ALPHA_UP)
                alpha += 16;
            else
                alpha -= 16;

            if (alpha > 256)
                alpha = 256;
            else if (alpha < 0)
                alpha = 0;

            emulator->onion_alpha = alpha;
            skin_window_set_onion( emulator->window, emulator->onion, emulator->onion_rotation, alpha );
            skin_window_redraw( emulator->window, NULL );
            //dprint( "onion alpha set to %d (%.f %%)", alpha, alpha/2.56 );
        }
        break;

    case SKIN_KEY_COMMAND_CHANGE_LAYOUT_PREV:
    case SKIN_KEY_COMMAND_CHANGE_LAYOUT_NEXT:
        {
            SkinLayout*  layout = NULL;

            if (command == SKIN_KEY_COMMAND_CHANGE_LAYOUT_NEXT) {
                layout = emulator->layout->next;
                if (layout == NULL)
                    layout = emulator->layout_file->layouts;
            }
            else if (command == SKIN_KEY_COMMAND_CHANGE_LAYOUT_PREV) {
                layout = emulator->layout_file->layouts;
                while (layout->next && layout->next != emulator->layout)
                    layout = layout->next;
            }
            if (layout != NULL) {
                SkinRotation  rotation;

                emulator->layout = layout;
                skin_window_reset( emulator->window, layout );

                rotation = skin_layout_get_dpad_rotation( layout );

                if (emulator->keyboard)
                    skin_keyboard_set_rotation( emulator->keyboard, rotation );

                if (emulator->trackball) {
                    skin_trackball_set_rotation( emulator->trackball, rotation );
                    skin_window_set_trackball( emulator->window, emulator->trackball );
                    skin_window_show_trackball( emulator->window, emulator->show_trackball );
                }

                skin_window_set_lcd_brightness( emulator->window, emulator->lcd_brightness );

                qframebuffer_invalidate_all();
                qframebuffer_check_updates();
            }
        }
        break;

    default:
        /* XXX: TODO ? */
        ;
    }
}

/* called periodically to poll for user input events */
static void emulator_window_refresh(EmulatorWindow* emulator)
{
    SkinEvent ev;
    SkinWindow*    window   = emulator->window;
    SkinKeyboard*  keyboard = emulator->keyboard;

   /* this will eventually call sdl_update if the content of the VGA framebuffer
    * has changed */
    qframebuffer_check_updates();

    if (window == NULL)
        return;

    while(skin_event_poll(&ev)) {
        switch(ev.type){
        case kEventVideoExpose:
            skin_window_redraw( window, NULL );
            break;

        case kEventKeyDown:
            skin_keyboard_process_event( keyboard, &ev, 1 );
            break;

        case kEventKeyUp:
            skin_keyboard_process_event( keyboard, &ev, 0 );
            break;

        case kEventMouseMotion:
            skin_window_process_event( window, &ev );
            break;

        case kEventMouseButtonDown:
        case kEventMouseButtonUp:
            {
                int  down = (ev.type == kEventMouseButtonDown);
                if (ev.u.mouse.button == kMouseButtonScrollUp)
                {
                    /* scroll-wheel simulates DPad up */
                    SkinKeyCode  kcode;

                    kcode = // emulator_window_rotate_keycode(kKeyCodeDpadUp);
                        skin_keycode_rotate(kKeyCodeDpadUp,
                            skin_layout_get_dpad_rotation(emulator_window_get_layout(emulator_window_get())));
                    user_event_key( kcode, down );
                }
                else if (ev.u.mouse.button == kMouseButtonScrollDown)
                {
                    /* scroll-wheel simulates DPad down */
                    SkinKeyCode  kcode;

                    kcode = // emulator_window_rotate_keycode(kKeyCodeDpadDown);
                        skin_keycode_rotate(kKeyCodeDpadDown,
                            skin_layout_get_dpad_rotation(emulator_window_get_layout(emulator_window_get())));
                    user_event_key( kcode, down );
                }
                else if (ev.u.mouse.button == kMouseButtonLeft) {
                    skin_window_process_event( window, &ev );
                }
            }
            break;

        case kEventQuit:
            /* only save emulator config through clean exit */
            emulator_window_done(emulator_window_get());
            qemu_system_shutdown_request();
            return;
        }
    }

    skin_keyboard_flush( keyboard );
}

/*
 * android/console.c helper routines.
 */

SkinKeyboard*
android_emulator_get_keyboard(void)
{
    return qemulator->keyboard;
}

void
android_emulator_set_window_scale(double  scale, int  is_dpi)
{
    EmulatorWindow*  emulator = qemulator;

    if (is_dpi)
        scale /= get_device_dpi( emulator->opts );

    if (emulator->window)
        skin_window_set_scale( emulator->window, scale );
}


void
android_emulator_set_base_port( int  port )
{
    /* Base port is already set in the emulator's core. */
    emulator_window_set_title(qemulator);
}
