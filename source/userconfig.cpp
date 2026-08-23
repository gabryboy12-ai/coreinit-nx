#include "coreinit/userconfig.h"
#include "coreinit/debug.h"

#include <switch.h>
#include <cstring>

namespace {

bool g_setInited = false;

void ensureSettings() {
    if (!g_setInited && R_SUCCEEDED(setInitialize())) g_setInited = true;
}

// Cafe OS language codes. Horizon uses a different, larger enum, so the
// regional English/French/Spanish variants collapse onto their base
// language -- Cafe OS has no separate code for them.
uint32_t cafeLanguage()
{
    ensureSettings();
    u64 code = 0;
    SetLanguage lang = SetLanguage_ENUS;
    if (R_SUCCEEDED(setGetSystemLanguage(&code))) {
        setMakeLanguage(code, &lang);
    }
    switch (lang) {
        case SetLanguage_JA:                            return 0;
        case SetLanguage_ENUS: case SetLanguage_ENGB:   return 1;
        case SetLanguage_FR:   case SetLanguage_FRCA:   return 2;
        case SetLanguage_DE:                            return 3;
        case SetLanguage_IT:                            return 4;
        case SetLanguage_ES:   case SetLanguage_ES419:  return 5;
        case SetLanguage_ZHCN: case SetLanguage_ZHHANS: return 6;
        case SetLanguage_KO:                            return 7;
        case SetLanguage_NL:                            return 8;
        case SetLanguage_PT:   case SetLanguage_PTBR:   return 9;
        case SetLanguage_RU:                            return 10;
        case SetLanguage_ZHTW: case SetLanguage_ZHHANT: return 11;
        default:                                        return 1;
    }
}

// Wii U country codes follow the Nintendo numbering: 1 Japan, 49 USA,
// 110 UK... We report a representative country per Horizon region rather
// than pretending to know the exact one.
uint32_t cafeCountry()
{
    ensureSettings();
    SetRegion region = SetRegion_USA;
    setGetRegionCode(&region);
    switch (region) {
        case SetRegion_JPN: return 1;    // Japan
        case SetRegion_USA: return 49;   // United States
        case SetRegion_EUR: return 110;  // United Kingdom
        case SetRegion_AUS: return 65;   // Australia
        case SetRegion_HTK: return 128;  // Taiwan
        default:            return 49;
    }
}

// Writes a value of the requested width into the caller's buffer.
bool store(UCSysConfig *s, uint32_t value)
{
    if (!s->data || s->dataSize == 0) return false;
    switch (s->dataType) {
        case UC_DATATYPE_UNSIGNED_BYTE:
            *(uint8_t *)s->data = (uint8_t)value;  return true;
        case UC_DATATYPE_UNSIGNED_SHORT:
            *(uint16_t *)s->data = (uint16_t)value; return true;
        case UC_DATATYPE_UNSIGNED_INT:
        case UC_DATATYPE_SIGNED_INT:
            *(uint32_t *)s->data = value;           return true;
        default:
            return false;
    }
}

bool resolve(UCSysConfig *s)
{
    const char *n = s->name;

    if (strstr(n, "language"))     return store(s, cafeLanguage());
    if (strstr(n, "cntry_reg"))    return store(s, cafeCountry());
    if (strstr(n, "eula_agree"))   return store(s, 1);
    if (strstr(n, "eco"))          return store(s, 0);
    if (strstr(n, "initial_launch")) return store(s, 0);
    if (strstr(n, "parent"))       return store(s, 0);   // no parental controls

    return false;
}

} // namespace

extern "C" {

// There is no IOSU to talk to, so the handle is a token. Anything positive
// is valid on Cafe OS.
UCHandle UCOpen(void) { return 1; }

UCError UCClose(UCHandle handle)
{
    (void)handle;
    return UC_ERROR_OK;
}

// Reads resolve against Horizon's real settings where a Cafe OS key has an
// equivalent -- console language and region are genuine information, and
// using them means a game picks the right text instead of guessing.
//
// Unknown keys get UC_ERROR_KEY_NOT_FOUND in their own entry and are logged
// once, so a port developer discovers which keys a title actually wants
// rather than guessing at the surface.
UCError UCReadSysConfig(UCHandle handle, uint32_t count, UCSysConfig *settings)
{
    (void)handle;
    if (!settings) return UC_ERROR_INVALID_PARAM;

    UCError overall = UC_ERROR_OK;
    for (uint32_t i = 0; i < count; i++) {
        if (resolve(&settings[i])) {
            settings[i].error = UC_ERROR_OK;
        } else {
            settings[i].error = UC_ERROR_KEY_NOT_FOUND;
            overall = UC_ERROR_KEY_NOT_FOUND;
            OSReport("coreinit-nx: UCReadSysConfig unknown key '%s'\n",
                     settings[i].name);
        }
    }
    return overall;
}

// Accepted and discarded. A game changing system configuration should not
// fail -- an error might send it down a recovery path nobody tested -- but
// it has no business altering the Switch's actual settings.
UCError UCWriteSysConfig(UCHandle handle, uint32_t count,
                         UCSysConfig *settings)
{
    (void)handle;
    if (!settings) return UC_ERROR_INVALID_PARAM;
    for (uint32_t i = 0; i < count; i++) settings[i].error = UC_ERROR_OK;
    return UC_ERROR_OK;
}

UCError UCDeleteSysConfig(UCHandle handle, uint32_t count,
                          UCSysConfig *settings)
{
    return UCWriteSysConfig(handle, count, settings);
}

} // extern "C"