#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void OSSavesDone_ReadyToRelease(void);
void OSReleaseForeground(void);
void OSEnableForegroundExit(void);

#ifdef __cplusplus
}
#endif