#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t MCPError;

typedef enum MCPRegion {
    MCP_REGION_JAPAN  = 0x01,
    MCP_REGION_USA    = 0x02,
    MCP_REGION_EUROPE = 0x04,
    MCP_REGION_CHINA  = 0x10,
    MCP_REGION_KOREA  = 0x20,
    MCP_REGION_TAIWAN = 0x40,
} MCPRegion;

// Da wut: WUT_CHECK_SIZE(MCPSysProdSettings, 0x46)
#pragma pack(push, 1)
typedef struct MCPSysProdSettings {
    MCPRegion product_area;
    uint16_t  eeprom_version;
    uint8_t   _pad0[2];
    MCPRegion game_region;
    uint8_t   _unk0[4];
    char      ntsc_pal[5];
    char      wifi_5ghz_country_code[4];
    uint8_t   wifi_5ghz_country_code_revision;
    char      code_id[8];
    char      serial_id[12];
    uint8_t   _unk1[4];
    char      model_number[16];
    uint32_t  version;
} MCPSysProdSettings;
#pragma pack(pop)

MCPError MCP_Open(void);
MCPError MCP_Close(int handle);
MCPError MCP_GetSysProdSettings(int32_t handle, MCPSysProdSettings *settings);

#ifdef __cplusplus
}
#endif