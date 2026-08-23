#include "coreinit/mcp.h"

#include <switch.h>
#include <cstring>
#include <cstdio>

namespace {

bool g_setInited = false;

void ensureSettings() {
    if (!g_setInited && R_SUCCEEDED(setInitialize())) g_setInited = true;
}

MCPRegion cafeRegion()
{
    ensureSettings();
    SetRegion region = SetRegion_USA;
    setGetRegionCode(&region);
    switch (region) {
        case SetRegion_JPN: return MCP_REGION_JAPAN;
        case SetRegion_EUR: return MCP_REGION_EUROPE;
        case SetRegion_AUS: return MCP_REGION_EUROPE;   // PAL
        case SetRegion_HTK: return MCP_REGION_TAIWAN;
        default:            return MCP_REGION_USA;
        
    }
}

} // namespace

extern "C" {

// No IOSU here: the handle is a token. Cafe OS treats any non-negative
// value as valid.
MCPError MCP_Open(void)  { return 1; }

MCPError MCP_Close(int handle)
{
    (void)handle;
    return 0;
}

// Region comes from Horizon and is real. The rest describes Wii U hardware
// that does not exist here -- serial number, model, EEPROM version -- so it
// is filled with plausible synthetic values. Games read these for region
// gating and display, not for anything that can be checked.
MCPError MCP_GetSysProdSettings(int32_t handle, MCPSysProdSettings *settings)
{
    (void)handle;
    if (!settings) return -1;

    memset(settings, 0, sizeof(*settings));
    const MCPRegion region = cafeRegion();

    settings->product_area   = region;
    settings->game_region    = region;
    settings->eeprom_version = 1;
    settings->version        = 1;

    snprintf(settings->ntsc_pal, sizeof(settings->ntsc_pal), "%s",
             region == MCP_REGION_EUROPE ? "PAL" : "NTSC");
    snprintf(settings->model_number, sizeof(settings->model_number),
             "%s", "coreinit-nx");
    memcpy(settings->serial_id, "000000000000", sizeof(settings->serial_id));

    return 0;
}

} // extern "C"