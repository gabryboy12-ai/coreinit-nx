#include "coreinit/condition.h"
#include "internal/mutex_internal.hpp"
#include "internal/handle_table.hpp"

#include <switch.h>

namespace {

struct HostCond {
    CondVar handle;
    void init() { condvarInit(&handle); }
};

coreinit_nx::HandleTable<HostCond> g_conds;

}

extern "C" {

void OSInitCond(OSCondition *cond)
{
    g_conds.get(cond);
}

void OSInitCondEx(OSCondition *cond, const char *name)
{
    (void)name;
    g_conds.get(cond);
}

void OSWaitCond(OSCondition *cond, OSMutex *mutex)
{
    auto *c = g_conds.get(cond);
    auto *m = coreinit_nx::getHostMutex(mutex);

    // Salva la profondita', azzerala, attendi. condvarWait rilascia la
    // Mutex e la riacquisisce prima di tornare. Poi ripristina.
    const uint32_t saved = m->count;
    m->count = 0;
    condvarWait(&c->handle, &m->lock);
    m->count = saved;
}

void OSSignalCond(OSCondition *cond)
{
    // Broadcast. Documentato in wut/include/coreinit/condition.h:
    // "Will wake up any threads waiting", equivalente a notify_all.
    condvarWakeAll(&g_conds.get(cond)->handle);
}

} // extern "C"