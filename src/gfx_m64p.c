/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-video-angrylionplus - plugin.c                            *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2014 Bobby Smiles                                       *
 *   Copyright (C) 2009 Richard Goedeken                                   *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define M64P_PLUGIN_PROTOTYPES 1

#define KEY_FULLSCREEN "Fullscreen"
#define KEY_SCREEN_WIDTH "ScreenWidth"
#define KEY_SCREEN_HEIGHT "ScreenHeight"
#define KEY_UPSCALING "Upscaling"
#define KEY_SSDITHER "SuperscaledDither"
#define KEY_SSREADBACKS "SuperscaledReads"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "gfx_m64p.h"
#include "glguts.h"
#include "parallel_imp.h"

#include "api/m64p_types.h"
#include "api/m64p_config.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static ptr_ConfigOpenSection ConfigOpenSection = NULL;
static ptr_ConfigSaveSection ConfigSaveSection = NULL;
static ptr_ConfigSetDefaultInt ConfigSetDefaultInt = NULL;
static ptr_ConfigSetDefaultBool ConfigSetDefaultBool = NULL;
static ptr_ConfigGetParamInt ConfigGetParamInt = NULL;
static ptr_ConfigGetParamBool ConfigGetParamBool = NULL;

static bool warn_hle;
static bool plugin_initialized;
void (*debug_callback)(void *, int, const char *);
void *debug_call_context;

extern int32_t window_width;
extern int32_t window_height;
extern int32_t window_fullscreen;

m64p_dynlib_handle CoreLibHandle;
GFX_INFO gfx;
void (*render_callback)(int);

static m64p_handle configVideoGeneral = NULL;
static m64p_handle configVideoParallel = NULL;

#define PLUGIN_VERSION 0x000001
#define VIDEO_PLUGIN_API_VERSION 0x020200
#define DP_INTERRUPT 0x20

uint32_t rdram_size;
static ptr_PluginGetVersion CoreGetVersion = NULL;

void plugin_init(void)
{
    CoreGetVersion = (ptr_PluginGetVersion)DLSYM(CoreLibHandle, "PluginGetVersion");

    int core_version;
    CoreGetVersion(NULL, &core_version, NULL, NULL, NULL);
    if (core_version >= 0x020501)
    {
        rdram_size = *gfx.RDRAM_SIZE;
    }
    else
    {
        rdram_size = 0x800000;
    }
}

void plugin_close(void)
{
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle _CoreLibHandle, void *Context,
                                     void (*DebugCallback)(void *, int, const char *))
{
    if (plugin_initialized)
    {
        return M64ERR_ALREADY_INIT;
    }

    /* first thing is to set the callback function for debug info */
    debug_callback = DebugCallback;
    debug_call_context = Context;

    CoreLibHandle = _CoreLibHandle;

    ConfigOpenSection = (ptr_ConfigOpenSection)DLSYM(CoreLibHandle, "ConfigOpenSection");
    ConfigSaveSection = (ptr_ConfigSaveSection)DLSYM(CoreLibHandle, "ConfigSaveSection");
    ConfigSetDefaultInt = (ptr_ConfigSetDefaultInt)DLSYM(CoreLibHandle, "ConfigSetDefaultInt");
    ConfigSetDefaultBool = (ptr_ConfigSetDefaultBool)DLSYM(CoreLibHandle, "ConfigSetDefaultBool");
    ConfigGetParamInt = (ptr_ConfigGetParamInt)DLSYM(CoreLibHandle, "ConfigGetParamInt");
    ConfigGetParamBool = (ptr_ConfigGetParamBool)DLSYM(CoreLibHandle, "ConfigGetParamBool");

    ConfigOpenSection("Video-General", &configVideoGeneral);
    ConfigOpenSection("Video-Parallel", &configVideoParallel);
    ConfigSetDefaultBool(configVideoGeneral, KEY_FULLSCREEN, 0, "Use fullscreen mode if True, or windowed mode if False");
    ConfigSetDefaultInt(configVideoGeneral, KEY_SCREEN_WIDTH, 640, "Width of output window or fullscreen width");
    ConfigSetDefaultInt(configVideoGeneral, KEY_SCREEN_HEIGHT, 480, "Height of output window or fullscreen height");
    ConfigSetDefaultInt(configVideoParallel, KEY_UPSCALING, 0, "Amount of rescaling: 0=None, 2=2x, 4=4x, 8=8x");
    ConfigSetDefaultBool(configVideoParallel, KEY_SSREADBACKS, 0, "Enable superscaling of readbacks when upsampling");
    ConfigSetDefaultBool(configVideoParallel, KEY_SSDITHER, 0, "Enable superscaling of dithering when upsampling");
    ConfigSaveSection("Video-General");

    plugin_initialized = true;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{

    if (!plugin_initialized)
    {
        return M64ERR_NOT_INIT;
    }

    /* reset some local variable */
    debug_callback = NULL;
    debug_call_context = NULL;

    plugin_initialized = false;

    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    /* set version info */
    if (PluginType != NULL)
    {
        *PluginType = M64PLUGIN_GFX;
    }

    if (PluginVersion != NULL)
    {
        *PluginVersion = PLUGIN_VERSION;
    }

    if (APIVersion != NULL)
    {
        *APIVersion = VIDEO_PLUGIN_API_VERSION;
    }

    if (PluginNamePtr != NULL)
    {
        *PluginNamePtr = "parallel";
    }

    if (Capabilities != NULL)
    {
        *Capabilities = 0;
    }

    return M64ERR_SUCCESS;
}

EXPORT int CALL InitiateGFX(GFX_INFO Gfx_Info)
{
    gfx = Gfx_Info;

    return 1;
}

EXPORT void CALL MoveScreen(int xpos, int ypos)
{
}

EXPORT void CALL ProcessDList(void)
{
}

EXPORT void CALL ProcessRDPList(void)
{
    vk_process_commands();
}

EXPORT int CALL RomOpen(void)
{
    window_fullscreen = ConfigGetParamBool(configVideoGeneral, KEY_FULLSCREEN);
    window_width = ConfigGetParamInt(configVideoGeneral, KEY_SCREEN_WIDTH);
    window_height = ConfigGetParamInt(configVideoGeneral, KEY_SCREEN_HEIGHT);
    vk_rescaling = ConfigGetParamInt(configVideoParallel, KEY_UPSCALING);
    vk_ssreadbacks = ConfigGetParamBool(configVideoParallel, KEY_SSREADBACKS);
    vk_ssdither = ConfigGetParamBool(configVideoParallel, KEY_SSDITHER);
    plugin_init();
    vk_init();

    return 1;
}

EXPORT void CALL RomClosed(void)
{
    vk_destroy();
}

EXPORT void CALL ShowCFB(void)
{
    vk_rasterize();
}

EXPORT void CALL UpdateScreen(void)
{
    vk_rasterize();
}

EXPORT void CALL ViStatusChanged(void)
{
}

EXPORT void CALL ViWidthChanged(void)
{
}

EXPORT void CALL ChangeWindow(void)
{
    screen_toggle_fullscreen();
}

EXPORT void CALL ReadScreen2(void *dest, int *width, int *height, int front)
{
    struct frame_buffer fb = {0};
    screen_read(&fb, false);

    *width = fb.width;
    *height = fb.height;

    if (dest)
    {
        fb.pixels = dest;
        screen_read(&fb, false);
    }
}

EXPORT void CALL SetRenderingCallback(void (*callback)(int))
{
    render_callback = callback;
}

EXPORT void CALL ResizeVideoOutput(int width, int height)
{
    window_width = width;
    window_height = height;
}

EXPORT void CALL FBWrite(unsigned int addr, unsigned int size)
{
}

EXPORT void CALL FBRead(unsigned int addr)
{
}

EXPORT void CALL FBGetFrameBufferInfo(void *pinfo)
{
}
