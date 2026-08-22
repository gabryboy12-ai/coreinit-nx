#include "coreinit/messagequeue.h"
#include "internal/handle_table.hpp"

#include <switch.h>
#include <vector>

namespace {

// The guest supplies the message buffer and its size. We keep our own copy
// of the ring rather than writing into that buffer: it lives in guest
// memory with guest byte order, and OSMessage holds a pointer plus three
// words that we would have to byte-swap on every access.
//
// The cost is a duplicated buffer of at most a few hundred bytes; the
// benefit is that endianness stays out of the hot path.
struct HostQueue {
    ::Mutex  lock;
    CondVar  notEmpty;
    CondVar  notFull;
    std::vector<OSMessage> ring;
    uint32_t head;
    uint32_t count;
    uint32_t capacity;

    void init() {
        mutexInit(&lock); condvarInit(&notEmpty); condvarInit(&notFull);
        ring.clear(); head = 0; count = 0; capacity = 0;
    }
};

coreinit_nx::HandleTable<HostQueue> g_queues;

OSMessageQueue g_systemQueue;
bool           g_systemQueueReady = false;

} // namespace

extern "C" {

void OSInitMessageQueue(OSMessageQueue *queue, OSMessage *messages,
                        int32_t size)
{
    if (!queue || size <= 0) return;
    (void)messages;   // vedi nota sopra: non scriviamo nel buffer del guest
    auto *q = g_queues.get(queue);
    mutexLock(&q->lock);
    q->capacity = (uint32_t)size;
    q->ring.assign((size_t)size, OSMessage{});
    q->head = 0; q->count = 0;
    mutexUnlock(&q->lock);
}

void OSInitMessageQueueEx(OSMessageQueue *queue, OSMessage *messages,
                          int32_t size, const char *name)
{
    (void)name;
    OSInitMessageQueue(queue, messages, size);
}

int32_t OSSendMessage(OSMessageQueue *queue, OSMessage *message,
                      OSMessageFlags flags)
{
    if (!queue || !message) return 0;
    auto *q = g_queues.get(queue);
    mutexLock(&q->lock);

    while (q->count >= q->capacity) {
        if (!(flags & OS_MESSAGE_FLAGS_BLOCKING)) {
            mutexUnlock(&q->lock);
            return 0;
        }
        condvarWait(&q->notFull, &q->lock);
    }

    if (flags & OS_MESSAGE_FLAGS_HIGH_PRIORITY) {
        // In testa: il prossimo a essere ricevuto.
        q->head = (q->head + q->capacity - 1) % q->capacity;
        q->ring[q->head] = *message;
    } else {
        q->ring[(q->head + q->count) % q->capacity] = *message;
    }
    q->count++;
    condvarWakeOne(&q->notEmpty);
    mutexUnlock(&q->lock);
    return 1;
}

int32_t OSReceiveMessage(OSMessageQueue *queue, OSMessage *message,
                         OSMessageFlags flags)
{
    if (!queue || !message) return 0;
    auto *q = g_queues.get(queue);
    mutexLock(&q->lock);

    while (q->count == 0) {
        if (!(flags & OS_MESSAGE_FLAGS_BLOCKING)) {
            mutexUnlock(&q->lock);
            return 0;
        }
        condvarWait(&q->notEmpty, &q->lock);
    }

    *message = q->ring[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    condvarWakeOne(&q->notFull);
    mutexUnlock(&q->lock);
    return 1;
}

// Guarda senza consumare, e non blocca mai.
int32_t OSPeekMessage(OSMessageQueue *queue, OSMessage *message)
{
    if (!queue || !message) return 0;
    auto *q = g_queues.get(queue);
    mutexLock(&q->lock);
    const bool has = q->count > 0;
    if (has) *message = q->ring[q->head];
    mutexUnlock(&q->lock);
    return has ? 1 : 0;
}

// Su Cafe OS il sistema pubblica qui eventi di ciclo di vita
// dell'applicazione. Qui nessuno la alimenta: la creiamo vuota perche' i
// giochi la interrogano comunque, e una coda vuota li lascia proseguire.
OSMessageQueue *OSGetSystemMessageQueue(void)
{
    if (!g_systemQueueReady) {
        OSInitMessageQueue(&g_systemQueue, nullptr, 16);
        g_systemQueueReady = true;
    }
    return &g_systemQueue;
}

} // extern "C"