#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Da wut: OSMessage 0x10, OSMessageQueue 0x3c
typedef struct OSMessage {
    void    *message;
    uint32_t args[3];
} OSMessage;

typedef struct OSMessageQueue { uint8_t _opaque[0x3c]; } OSMessageQueue;

typedef enum OSMessageFlags {
    OS_MESSAGE_FLAGS_NONE          = 0,
    OS_MESSAGE_FLAGS_BLOCKING      = 1 << 0,
    OS_MESSAGE_FLAGS_HIGH_PRIORITY = 1 << 1,
} OSMessageFlags;

void OSInitMessageQueue(OSMessageQueue *queue, OSMessage *messages,
                        int32_t size);
void OSInitMessageQueueEx(OSMessageQueue *queue, OSMessage *messages,
                          int32_t size, const char *name);
int32_t OSSendMessage(OSMessageQueue *queue, OSMessage *message,
                      OSMessageFlags flags);
int32_t OSReceiveMessage(OSMessageQueue *queue, OSMessage *message,
                         OSMessageFlags flags);
int32_t OSPeekMessage(OSMessageQueue *queue, OSMessage *message);
OSMessageQueue *OSGetSystemMessageQueue(void);

#ifdef __cplusplus
}
#endif