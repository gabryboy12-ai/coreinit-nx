# coreinit-nx

An implementation of the Wii U **Cafe OS `coreinit`** API on top of
[libnx](https://github.com/switchbrew/libnx), so that statically
recompiled Wii U code can run natively on Nintendo Switch homebrew.

=======

> **Status: early but usable as a base.** Twenty-four modules, 73 tests
> passing on real hardware.
>
> | Corpus | coreinit symbols | Title requirements covered |
> |---|---|---|
> | Black Ops 2 (Wii U) | 131 / 162 | **80.9%** |
> | Homebrew (4 titles) | 161 / 347 | **57.6%** |
>
> Coverage is measured, not estimated — see [the import census](#the-import-census).

---

## Why this exists

Every existing Wii U reimplementation — Cemu, decaf-emu — targets **PC**.
Nobody has implemented Cafe OS on **Horizon**.

That matters, because Cafe OS and Horizon are both Nintendo RTOS designs
built a few years apart, and their primitives correspond almost one to one.
Implementing Cafe OS on libnx is closer to writing an *adapter between two
related operating systems* than to writing an emulator.

Static recompilation tools such as
[DolRecomp](https://github.com/ExpansionPak/DolRecomp) can translate Wii U
PowerPC code to C — but they are explicitly CPU-code-only and ship no
runtime. This project aims to be part of that missing runtime.

---

## Architecture

### Guest structures are used as keys, not as storage

When recompiled game code calls `OSInitMutex(&mutex)`, it passes a pointer
to a structure **it allocated itself**, in its own memory, with the Wii U
layout and in **big-endian** byte order.

Two approaches were possible:

| | Store state inside the guest struct | Use the struct address as a key |
|---|---|---|
| Endianness | byte swap on every field access | never reads the struct at all |
| ABI constraint | full field-by-field layout | size only |
| Locking logic | reimplemented from scratch | delegated to the Horizon kernel |

**This project takes the second approach.** A host-side hash table maps the
guest structure's address to a real libnx primitive. The cost is one lookup
per call; the benefit is that endianness disappears from the problem and we
inherit tested, fast kernel primitives.

The only remaining ABI constraint is **size**, because the guest allocates
the structure and may embed it inside its own structures. Sizes are taken
from [wut](https://github.com/devkitPro/wut), the Wii U homebrew toolchain,
which is the authoritative public source.

`OSSystemInfo` is the first exception: its **contents** matter, not just its
size, because the `OSTimerClockSpeed` macro expands into game code as
`OSGetSystemInfo()->busClockSpeed / 4`. Heaps are expected to be the next
place this approach strains, since games inspect heap structures directly.

### Lazy creation

`HandleTable::get()` creates the host object on first use rather than
requiring explicit initialisation. Game code may use a primitive that was
statically initialised and never passed to an `OSInit*` call. Degrading
cleanly beats crashing.

---

## Implemented

### `coreinit/mutex.h`

`OSInitMutex`, `OSInitMutexEx`, `OSLockMutex`, `OSUnlockMutex`,
`OSTryLockMutex` — `OSMutex` is `0x2c` bytes.

**Verified on hardware:** Cafe OS mutexes are recursive; the same thread may
acquire one repeatedly. Getting this wrong does not fail loudly — it
surfaces later as intermittent deadlocks.

### `coreinit/condition.h`

`OSInitCond`, `OSInitCondEx`, `OSWaitCond`, `OSSignalCond` —
`OSCondition` is `0x1c` bytes.

`OSSignalCond` is a **broadcast**, mapped to `condvarWakeAll`. Not an
assumption: wut documents it as equivalent to `notify_all`.

**Verified on hardware:** a thread holding the mutex at recursion depth 2
can wait on a condition, be woken by another thread, and resume with its
depth intact. Verified by a *third* thread attempting to acquire the mutex
after a single unlock — it must fail. Testing this from the waiting thread
would always succeed, since the mutex is recursive.

### `coreinit/thread.h`

`OSCreateThread`, `OSResumeThread`, `OSSuspendThread`, `OSJoinThread`,
`OSExitThread`, `OSGetCurrentThread`, `OSGetCoreId`,
`OSSetThreadCleanupCallback` — `OSThread` is `0x6a0` bytes.

**Verified on hardware:** `OSCreateThread` creates a thread *suspended*, as
Cafe OS does — it does not run until `OSResumeThread`. Argument passing and
the exit value survive the entry point trampoline.

Mapping decisions, all arbitrary and open to revision:

- **Stack.** The guest-supplied stack pointer is stored but unused; libnx
  allocates the host stack, sized from the guest's `stackSize` request.
  Recompiled code will need the guest stack as its emulated PPC stack, but
  how that separates from the ARM64 host stack depends on how the
  recompiler emits code — unknown at time of writing. **Input welcome.**
- **Priority.** Cafe OS 0–31 (lower is higher) is mapped linearly onto the
  window between the process priority and `0x3F`. Relative ordering is
  preserved, absolute values are not.
- **Affinity.** Cafe OS affinity is a 3-core bitmask; libnx wants a single
  core id. First set bit wins, `-2` for "any", and a failed create retries
  on `-2` rather than giving up. `OSGetCoreId` clamps Horizon's core 3 to 2,
  since it does not exist on Cafe OS.
- **Entry point.** Cafe OS uses `int(int, const char**)`, libnx uses
  `void(void*)`. A trampoline bridges them and captures the return value.
- **Main thread.** It was never created through `OSCreateThread`, so it has
  no guest `OSThread`. A synthetic one is registered lazily on first
  `OSGetCurrentThread` call. Game code uses the pointer as an identity, not
  as a structure to inspect.

**Not implemented:** suspending an already-running thread.
`OSSuspendThread` maintains the counter but does not stop execution.

### `coreinit/time.h`, `coreinit/systeminfo.h`, `coreinit/title.h`

`OSGetTime`, `OSGetSystemTime`, `OSGetTick`, `OSGetSystemTick`,
`OSSleepTicks`, `OSCalendarTimeToTicks`, `OSTicksToCalendarTime`,
`__OSSetAbsoluteSystemTime`, `OSGetSystemInfo`, `OSGetTitleID` —
`OSCalendarTime` is `0x28`, `OSSystemInfo` is `0x20`.

Horizon counts at 19.2 MHz, the Wii U timer at `busClockSpeed / 4`. The
ratio reduces exactly to **3315/1024** — integer conversion, no floating
point, no overflow before roughly nine years of uptime. Tick-to-nanosecond
conversion reduces exactly to **32000/1989**, matching wut's own macro.

Calendar conversion uses the days-from-civil algorithm rather than
`gmtime`/`mktime`, to avoid timezone handling and libc global state. Note
that `OSCalendarTime::tm_year` is the **full AD year**, not `year - 1900`.

`OSGetTitleID` returns 0. There is no real Wii U title ID here; it should
become configurable, set by the port at runtime.

**Verified on hardware:** the clock is monotonic and advances at the
intended rate; calendar conversion round-trips correctly across leap years,
weekdays and day-of-year.

**Assumed, NOT verified:** the Cafe OS epoch (2000-01-01) and the bus clock
constant (248.625 MHz). Both are internally consistent with the tests, which
means the tests would keep passing even if the constants were wrong. Anyone
able to confirm these against decaf-emu or real hardware is very welcome to.

### `coreinit/cache.h`

`DCFlushRange`, `DCStoreRange` — mapped to `armDCacheFlush` and
`armDCacheClean`.

**NOT verified.** From userspace there is no way to observe the cache; the
test only shows that the data is not corrupted. On the Wii U these matter
because the GPU reads main memory directly and the CPU cache is not coherent
with it. Under Horizon the situation differs, and for recompiled code these
may end up closer to no-ops than to real cache operations.

### `coreinit/memfrmheap.h`

`MEMCreateFrmHeapEx`, `MEMDestroyFrmHeap`, `MEMAllocFromFrmHeapEx`,
`MEMFreeToFrmHeap`, `MEMRecordStateForFrmHeap`, `MEMFreeByStateToFrmHeap`,
`MEMAdjustFrmHeap`, `MEMGetAllocatableSizeForFrmHeapEx` —
`MEMFrmHeap` is `0x4c` bytes, `MEMHeapHeader` `0x40`.

The first module that **implements** rather than maps. A frame heap is a
two-ended stack: allocation moves `head` up from the start or `tail` down
from the end, freeing resets either to its limit or back to a recorded
state. No free list, no coalescing.

`malloc` could not be delegated to: `MEMHeapHandle` is a pointer to the
guest-supplied region, and returned pointers must fall inside it because
games do arithmetic on them and compare them.

**Verified on hardware:** allocations land inside the guest region;
alignment up to 256 bytes is honoured even from a deliberately misaligned
head; `MEMFreeByStateToFrmHeap` restores free space to exactly its prior
value.

**Assumed, NOT verified:** that a *negative* alignment means "allocate from
the tail". This is the Revolution SDK convention and the signature takes a
signed `int`, but it has not been confirmed against decaf-emu.

**Deviations from Cafe OS:**
- The `MEMFrmHeap` structure at the start of the region is **reserved but
  not populated**. Recompiled code would read it big-endian while we write
  little-endian, so writing it would be worse than leaving it alone.
  Anything that inspects `dataStart`/`dataEnd` directly will see garbage.
- State records are kept host-side. Cafe OS spends `0x10` bytes of heap per
  recorded state; we spend none, so reported free space is slightly larger
  after `MEMRecordStateForFrmHeap`.

### `coreinit/memexpheap.h`, base heaps

`MEMCreateExpHeapEx`, `MEMDestroyExpHeap`, `MEMAllocFromExpHeapEx`,
`MEMFreeToExpHeap`, `MEMGetTotalFreeSizeForExpHeap`,
`MEMGetAllocatableSizeForExpHeapEx`, `MEMSetAllocModeForExpHeap`,
`MEMGetAllocModeForExpHeap`, `MEMGetBaseHeapHandle`,
`MEMSetBaseHeapHandle` — `MEMExpHeap` is `0x54`, `MEMExpHeapBlock` `0x14`.

A general allocator over a guest-supplied region: block list, coalescing on
free, first-fit and nearest-fit modes, allocation from either end.

**Cafe OS puts a `0x14` block header inline before every allocation. We keep
the bookkeeping host-side but charge the same `0x14` anyway.** Without it
`MEMGetTotalFreeSizeForExpHeap` would report more free space than Cafe OS
does, and a game allocating until failure would behave differently.
Over-reporting available memory fails far from its cause.

**Verified on hardware:** freed blocks are reused; freeing three adjacent
blocks coalesces back to exactly the initial free size; alignment up to 256
is honoured; a 100-byte request consumes exactly 120 bytes of region.

**Scope is deliberate.** The full ExpHeap API includes groups, visitors,
debug modes, block resizing and integrity checks. The census shows games
use six functions. The rest is not implemented — `MEMGetSizeForMBlockExpHeap`
notably, since it takes only a pointer with no heap handle and would need a
global block registry.

**Base heaps** do not exist here as they do on Cafe OS, where the system
creates them before the game starts. `MEMGetBaseHeapHandle` lazily builds an
8 MB expanded heap on host memory. The size is arbitrary and should become
configurable, set by the port.

### `coreinit/debug.h` and the libc question

`OSReport`, `OSReportVerbose`, `OSReportInfo`, `OSReportWarn`, `OSVReport`,
`OSConsoleWrite`, `OSFatal`, `__os_snprintf`, `OSSavesDone_ReadyToRelease`

Output goes through a settable sink (`coreinitNxSetLogSink`), defaulting to
stdout. `OSFatal` never returns on Cafe OS — it shows an error screen the
user powers off from — so the default handler logs and blocks. A port will
want `coreinitNxSetFatalHandler`.

`OSSavesDone_ReadyToRelease` is a deliberate no-op: nothing here owns the
foreground. It is present because every title calls it.

**On symbol collisions with the C library.** `coreinit` exports `exit`,
`_Exit`, `memcpy`, `memmove` and `memset`, because on the Wii U it *was*
the system C library. These are not Nintendo APIs and this project does not
implement them: the linker resolves them against newlib, whose semantics
match and whose ARM64 implementations are better than anything written here.

That is the correct answer rather than a workaround, and it means those
symbols genuinely count as covered. `implemented.txt` lists them in a
separate section so the distinction stays visible.

Only Cafe OS-specific names need writing — `__os_snprintf`, `memclr` — plus
the Green Hills compiler runtime (`__ghsLock`, `__ghs_flock_file`,
`__cpp_exception_init_ptr`), which no host library provides. Those remain
unimplemented.

Note `__os_snprintf` takes `size_t`, not `uint32_t`. On aarch64 those are 64
and 32 bits respectively — unlike `int` and `int32_t`, they are genuinely
different types.

### Thread introspection and thread-specific storage

`OSGetThreadPriority`, `OSSetThreadPriority`, `OSGetThreadAffinity`,
`OSSetThreadAffinity`, `OSGetThreadName`, `OSSetThreadName`,
`OSGetThreadSpecific`, `OSSetThreadSpecific`, `OSSetThreadDeallocator`

**Getters return what the game set, not what Horizon granted.** The priority
mapping is lossy — 32 Cafe levels compressed into the available Horizon
window — so querying the kernel and inverting the formula would return a
different number than the one passed to `OSCreateThread`. Read-modify-write
patterns ("raise yourself one above where you are") would drift on every
iteration. Same reasoning for affinity: the game passes a 3-core mask, we
pick a single core, and the original mask is the only sensible answer.

Thread-specific storage uses a 16-slot `thread_local` array rather than
hanging off `HostThread`, so it works for threads we did not create — the
main thread, or anything started by the C library. `OSGetThreadSpecific`
and `OSSetThreadSpecific` take only an ID and act on the current thread.

**Verified on hardware:** slots are genuinely per-thread — a child thread
starts with them empty while the parent holds a value.

**Not implemented:** changing priority or affinity of an already-running
thread. Both are recorded and reported back, neither is applied.

### `coreinit/filesystem.h` — synchronous read-only subset

`FSInit`, `FSShutdown`, `FSAddClient`, `FSDelClient`, `FSGetClientNum`,
`FSInitCmdBlock`, `FSOpenFile`, `FSCloseFile`, `FSReadFile`,
`FSReadFileWithPos`, `FSGetPosFile`, `FSSetPosFile`, `FSGetStatFile`

Cafe OS's file API carries three objects most POSIX programmers do not
expect: an `FSClient` session (0x1700 bytes), an `FSCmdBlock` per-command
context (0xA80 bytes) — the API was designed asynchronous — and an
`FSErrorFlag` mask on nearly every call.

Both structures are treated as opaque and never populated. `FSFileHandle`
is a `uint32_t` rather than a pointer, so handles are minted here and map
to host `FILE*` in a side table.

**Path mapping is configurable, not hardcoded.** Wii U paths look like
`/vol/content/...`; where that content lives on a Switch SD card is the
port's decision. `coreinitNxAddVolumeMapping("/vol/content", "/wiiu/bo2")`
registers a prefix translation, applied in one place.

**Scope.** Only the synchronous read path. The `*Async` variants do not
appear in the filtered census — games use this API synchronously despite
its asynchronous design. Writing, directories and mounting are not
implemented yet.

**Verified on hardware, end to end:** a real file written to the SD card,
opened through a `/vol/content/...` path, read back with correct bytes,
seeked and re-read.

**Assumed, NOT verified:** that `FSReadFile` returns the *number of items
read* rather than `FS_STATUS_OK`. This is the Cafe OS convention — positive
is a count, negative is an error — but it has not been confirmed against
decaf-emu.

### Filesystem — writing, directories, per-client state

`FSWriteFile`, `FSChangeDir`, `FSGetCwd`, `FSOpenDir`, `FSReadDir`,
`FSCloseDir`, `FSMakeDir`, `FSRemove`, `FSRename`, `FSGetLastError`,
`FSGetLastErrorCodeForViewer`

`FSChangeDir` forced the first real per-client state in the project: a
relative path must resolve against *that client's* current directory, not a
global one. `HostClient` now carries a working directory and the last error,
keyed by the guest `FSClient` pointer.

Note that `FS_STATUS_*` and `FS_ERROR_*` are distinct enums — the first is
the call's outcome, the second the diagnostic code retrievable afterwards.
`FSGetLastError` returns the latter, so operations map their status into it
as they complete.

`FSRemove` deletes both files and empty directories, matching Cafe OS.
`FSReadDir` returns `FS_STATUS_END` at the end of a directory rather than an
error.

**Verified on hardware, end to end:** write a file, read it back byte for
byte, change directory, open the same file by relative path, list the
directory and find it, rename it, delete it, and confirm that opening a
missing file leaves `FS_ERROR_NOT_FOUND` in the client's error state.

**Still not implemented:** mounting and volume enumeration (`FSMount`,
`FSGetMountSource`, `FSGetVolumeState`) and change notifications
(`FSSetStateChangeNotification`). These concern removable media, which has
no meaningful equivalent here; they will be documented stubs rather than
fake implementations.

### `coreinit/atomic64.h`

`OSGetAtomic64`, `OSSetAtomic64`, `OSSwapAtomic64`,
`OSCompareAndSwapAtomic64`, `OSAddAtomic64`, `OSAndAtomic64`,
`OSOrAtomic64`, `OSXorAtomic64`, `OSTestAndSetAtomic64`,
`OSTestAndClearAtomic64`

Implemented with compiler intrinsics (`__atomic_*`, `__ATOMIC_SEQ_CST`),
not locks: GCC emits native ARM64 atomics directly. Wrapping these in a
mutex would be an order of magnitude slower and would deadlock if a game
used them from an interrupt-like context. Sequential consistency matches
the PowerPC `sync`/`lwarx`-`stwcx` pairs Cafe OS builds these from, which
are full barriers.

**Verified on hardware:** two threads performing 10,000 increments each on
a shared counter end at exactly 20,000 — a non-atomic implementation would
lose increments.

**Assumed, NOT verified:** which functions return the *previous* value and
which return the *new* one. `OSAddAtomic64` is implemented as returning the
new value, `OSSetAtomic64` and the test-and-* pair as returning the old,
following the Revolution SDK convention. Getting these backwards would not
fail any test here — it would silently corrupt a game's counters.

### Filesystem — mounting

`FSMount`, `FSGetMountSource`, `FSGetVolumeState`,
`FSSetStateChangeNotification`

`FSMount` is **not a stub**: it registers a volume mapping. Mounting an SD
source on `/vol/external01` makes paths under that prefix resolve on the
host filesystem, so paths the game constructs afterwards actually work.
This costs no more code than a stub and is considerably more useful.

`FSGetVolumeState` always reports `READY`, and state-change notifications
are never fired. There is no removable media here, so nothing can change —
that is the correct behaviour for this environment, not a pretence.

`FSOpenFile` now distinguishes `FS_STATUS_NOT_FILE` from
`FS_STATUS_NOT_FOUND` by stat-ing the path on failure. Games can branch on
the difference.

### `coreinit/alarm.h`

`OSCreateAlarm`, `OSCreateAlarmEx`, `OSSetAlarm`, `OSSetPeriodicAlarm`,
`OSCancelAlarm`, `OSCancelAlarms`, `OSSetAlarmTag`, `OSSetAlarmUserData`,
`OSGetAlarmUserData`, `OSWaitAlarm` — `OSAlarm` is `0x58` bytes.

**One scheduler thread for all alarms**, not one thread per alarm: a game
may create dozens, and dozens of sleeping threads waste memory and
scheduling. It sleeps on a condition variable with a timeout until the
nearest due alarm, so there is no polling loop, and setting a sooner alarm
signals it to recompute. The thread starts lazily on the first
`OSSetAlarm`, so a game that never uses alarms pays nothing.

Callbacks are invoked **without holding the internal lock**. A callback that
sets another alarm would otherwise deadlock — the classic failure mode for
this kind of code.

The `OSContext *` passed to callbacks is always `nullptr`: there is no
PowerPC thread context to hand over, and inventing one would be worse than
admitting its absence.

**Verified on hardware:** one-shot alarms fire once and not early;
cancelled alarms never fire; periodic alarms repeat and stop on
`OSCancelAlarms` by tag.

#### A lesson worth recording

The first version of this module passed every test while being **wrong**.
Implementation and tests shared the same assumption — that `OSSetAlarm`
takes an absolute deadline — so they agreed with each other and proved
nothing. wut documents `OSSetPeriodicAlarm`'s `start` as *"the duration
until the alarm should first be triggered"*: the time is **relative**.

What caught it was a test designed so it could not pass by coincidence:
pass a value that only makes sense as a delta and would be long expired as
an absolute instant. It failed, the assumption was wrong, and the code was
corrected.

Three assumptions in this repository are still in that state —
self-consistent but unverified: the Cafe OS epoch, the bus clock constant,
and which atomics return the old value versus the new. Treat them
accordingly.

### Teardown

`coreinitNxAlarmShutdown` stops and joins the scheduler thread. Without it
the process terminates while that thread still sleeps on static state being
destroyed, and Horizon reports an abnormal exit.

This is the project's first explicit teardown. Modules owning resources —
threads, open handles — must expose one, and the port is responsible for
calling it. `FSShutdown` already plays the same role.

### `coreinit/lockedcache.h`

`LCAlloc`, `LCDealloc`, `LCGetMaxSize`, `LCGetAllocatableSize`,
`LCGetUnallocated`, `LCHardwareIsAvailable`, `LCEnableDMA`, `LCDisableDMA`,
`LCIsDMAEnabled`, `LCLoadDMABlocks`, `LCStoreDMABlocks`, `LCWaitDMAQueue`,
`LCGetDMAQueueLength`

The Espresso can lock 16 KB of its L1 cache and use it as very low latency
scratch memory, with a DMA engine moving data to and from main RAM. ARM64
has no equivalent — L1 cannot be locked from userspace and there is no
program-controlled DMA.

Real memory is allocated instead, aligned to 64 bytes (the A57 cache line).
A stub allocating nothing would make games read garbage: they write data
here and read it back. The latency advantage is lost, but it never existed
on this hardware.

**The 16 KB budget is enforced.** `LCAlloc` fails beyond it, exactly as the
Espresso would. Offering unlimited memory would hide a real constraint until
it surfaced somewhere unfixable.

DMA is synchronous — the copy completes before the call returns, so
`LCGetDMAQueueLength` is always 0 and `LCWaitDMAQueue` has nothing to wait
for. An asynchronous queue would add a thread and latency for no benefit.

### `coreinit/semaphore.h`, `coreinit/spinlock.h`

`OSInitSemaphore`, `OSInitSemaphoreEx`, `OSGetSemaphoreCount`,
`OSSignalSemaphore`, `OSWaitSemaphore`, `OSTryWaitSemaphore`,
`OSInitSpinLock`, `OSAcquireSpinLock`, `OSTryAcquireSpinLock`,
`OSTryAcquireSpinLockWithTimeout`, `OSReleaseSpinLock`, and the four
`OSUninterruptibleSpinLock_*` variants — `OSSemaphore` `0x20`,
`OSSpinLock` `0x10`.

Semaphores are built on mutex + condvar rather than libnx's `Semaphore`,
because `OSGetSemaphoreCount` must read the count **without** modifying it
and libnx does not expose the value. Ten extra lines buys the counter in
plain sight.

Note that `OSSignalSemaphore` and `OSTryWaitSemaphore` return the
**previous** count, not a boolean. wut documents the latter as "if the value
is >0 then it means the call was successful".

**Spinlocks are mapped to ordinary mutexes.** On Cafe OS they disable
interrupts and busy-wait, which suits a system with few cores and
cooperative scheduling. Under Horizon interrupts cannot be disabled from
userspace, and busy-waiting would be *worse* than a mutex: the Tegra has
fewer usable cores than the Wii U, so a spinning thread steals time from the
very thread holding the lock. On preemptive scheduling, spinning is a
pessimisation.

The `_Uninterruptible` variants are therefore identical to the plain ones —
the only difference on Cafe OS was interrupt masking, which does not exist
here.

**Verified on hardware:** a worker blocks on a zero-count semaphore and
resumes only after `OSSignalSemaphore`; two threads performing 5,000
guarded increments each end at exactly 10,000.

### Cache operations, block move, thread cancellation

`DCInvalidateRange`, `DCZeroRange`, `DCTouchRange`, `ICInvalidateRange`,
`OSBlockMove`, `OSBlockSet`, `OSCancelThread`, `OSTestThreadCancel`,
`OSSetThreadCancelState`, `OSDetachThread`, `OSGetStackPointer`,
`OSBlockThreadsOnExit`

Cache ranges are **rounded up to 32 bytes**, the Espresso line size, as wut
documents. A game zeroing one byte expects the whole line zeroed, and may
rely on it.

`DCTouchRange` is a deliberate no-op. On PowerPC it prefetched lines into
cache; faking that with dummy reads would waste bandwidth for a hint the
hardware is free to ignore anyway.

`OSBlockMove` honours its `flush` flag — wut notes the function "makes use
of the cache to speed up the copy, so a flush is recommended", so games pass
it expecting the synchronisation. It uses `memmove`, not `memcpy`: the
regions may overlap.

**Thread cancellation is cooperative, and implemented faithfully.** wut
documents that a cancelled thread "will be terminated next time
`OSTestThreadCancel` is called" — there is no asynchronous kill to emulate.
Which is fortunate: killing a thread mid-flight leaves locks held and memory
unfreed, and libnx rightly offers no way to do it.

**Verified on hardware:** a worker looping with cancellation points stops
after `OSCancelThread` instead of running to completion; `DCZeroRange` on a
single byte clears exactly 32.

`OSGetStackPointer` returns the host stack pointer. The value is not
comparable to anything on Cafe OS, but games use it for depth estimates and
logging rather than as a meaningful address.

### `coreinit/event.h`, `coreinit/messagequeue.h`

`OSInitEvent`, `OSInitEventEx`, `OSSignalEvent`, `OSSignalEventAll`,
`OSWaitEvent`, `OSWaitEventWithTimeout`, `OSResetEvent`,
`OSInitMessageQueue`, `OSInitMessageQueueEx`, `OSSendMessage`,
`OSReceiveMessage`, `OSPeekMessage`, `OSGetSystemMessageQueue` —
`OSEvent` `0x24`, `OSMessage` `0x10`, `OSMessageQueue` `0x3c`.

Cafe OS events are Win32-style, and wut's documentation links each function
to its Windows counterpart. Manual-reset events stay signalled until
`OSResetEvent`; auto-reset events release one waiter and clear themselves.

**The message ring is kept host-side** rather than written into the buffer
the guest supplies. That buffer lives in guest memory with guest byte order,
and `OSMessage` holds a pointer plus three words that would need swapping on
every access. Duplicating a few hundred bytes keeps endianness out of the
hot path.

`OSGetSystemMessageQueue` returns an empty queue. On Cafe OS the system
publishes application lifecycle events there; nothing feeds it here, and an
empty queue lets games proceed rather than blocking on it.

**Verified on hardware:** two threads waiting on a manual-reset event are
both released by a single `OSSignalEvent` — with auto-reset semantics only
one would pass; a high-priority message jumps the queue; a non-blocking
receive on an empty queue fails instead of hanging.

Also added: `OSIsThreadTerminated`, `OSIsDebuggerPresent` and
`OSIsDebuggerInitialized` (both false — there is no Cafe OS debugger here,
and games use them only to enable extra logging), `OSPanic` (routed through
the same handler as `OSFatal`, with file and line prepended), and
`OSMemoryBarrier` (a full fence, matching PowerPC `sync`).

### `coreinit/dynload.h` — dynamic loading without a loader

`OSDynLoad_Acquire`, `OSDynLoad_FindExport`, `OSDynLoad_Release`,
`OSDynLoad_SetAllocator`, `OSDynLoad_GetAllocator`,
`OSDynLoad_GetNumberOfRPLs`, `OSDynLoad_GetRPLInfo`

This looked like the hardest problem in the project: resolving symbols by
name at runtime sits awkwardly with static recompilation, which assumes
every call target is known ahead of time.

The tension dissolves on inspection. Games do not load arbitrary plugins
through OSDynLoad — they acquire the **system RPLs**, `coreinit`, `gx2`,
`nsysnet`, which are exactly what this library implements. Nothing needs
loading; the code is already linked in. `Acquire` becomes a lookup and
`FindExport` a table search.

`source/symbol_registry.cpp` holds that table, mapping
`("coreinit", "OSGetTime")` to the address of our implementation. It doubles
as the authoritative list of the library's public surface — more reliable
than the hand-maintained `implemented.txt`. It currently holds a
representative subset; completing it is mechanical work, well suited to
outside contributions.

Module names carrying a `.rpl` suffix are accepted. Acquiring a library we
do not implement returns `OS_DYNLOAD_MODULE_NOT_FOUND` rather than a handle
that would resolve nothing later — an honest failure beats a broken pointer.

`OSDynLoad_GetNumberOfRPLs` and `OSDynLoad_GetRPLInfo` return 0 and FALSE.
That is not a stub: wut documents them as *"always returns 0 on release
versions of CafeOS, requires `OSGetSecurityLevel() > 0`"*. They never worked
on a retail console, so any game calling them already has a fallback path.

`SetAllocator` stores the callbacks and never calls them, since nothing is
ever allocated for loading.

**Verified on hardware:** a function resolved by name through
`FindExport` is then *called through the returned pointer* and returns a
valid result — a wrong address would crash rather than answer.

### Data exports

`MEMAllocFromDefaultHeap`, `MEMAllocFromDefaultHeapEx`,
`MEMFreeToDefaultHeap`, `__gh_errno_ptr`, `__cpp_exception_init_ptr`,
`__cpp_exception_cleanup_ptr`, `__atexit_cleanup`

Some coreinit exports are **data, not functions**. The default heap
allocators are global variables holding function pointers: a game reads the
variable and calls through it, and may *replace* it to hook allocation — a
common pattern in commercial engines that track memory usage.

They are therefore exported as real variables, initialised to
implementations backed by the MEM2 base heap. The symbol registry now
distinguishes `Function` from `Data`, so `OSDynLoad_FindExport` finally
honours its `exportType` parameter, which it previously ignored.

**Verified on hardware:** replacing `MEMAllocFromDefaultHeap` with a test
function redirects subsequent allocations through it. A disguised function
could not do that.

The Green Hills C++ exception hooks (`__cpp_exception_init_ptr` and
friends) are null. We do not know what they do, and inventing behaviour
would be worse than admitting absence — they are there so linking succeeds,
documented as unimplemented for anyone who can find out.

`__gh_errno_ptr` maps to newlib's `__errno()`. `_iob`, `environ` and
`__gh_FOPEN_MAX` are **not defined here**: newlib already provides them, and
they belong in the host-provided section alongside `memcpy` and `exit`.

---

## The import census

`tools/rpx-imports/` extracts the named imports from Wii U `.rpx` files and
aggregates them across a corpus, so that implementation order follows
measured demand rather than intuition.

This is possible because RPX files carry **named import tables**: Cafe OS
uses dynamic linking, so a title declares exactly which functions it needs
from `coreinit`, `gx2`, `nsysnet` and the rest. No recompilation required —
it is an ELF walk.

```sh
python3 tools/rpx-imports/extract_imports.py <file.rpx>
python3 tools/rpx-imports/census.py <corpus-dir> --label homebrew
```

Frequency is counted as **number of titles importing a symbol**, not total
call sites. A function every title needs matters more than one called a
thousand times inside a single game. Coverage is weighted the same way,
which is why implementing seven well-chosen functions moved it from 9.6% to
14.2%.

Current results are in `census/`.

### Linked stubs are not usage

An RPX import table lists every stub the linker pulled in, not the
functions the code actually calls. Black Ops 2 declares **959** coreinit
imports — including kernel internals like `PPCMfhid0` and
`__KernelSetUserModeExHandler` that no game calls. That is essentially
coreinit's entire export table.

Relocations are the ground truth: if nothing relocates against a symbol,
nothing calls it. Filtering the symbol table against `.rela.*` entries
brings Black Ops 2 down to **157** — an 83% reduction — while leaving the
wut homebrew figures untouched, because wut's linker already garbage-
collects unused stubs and Treyarch's did not.

Anyone analysing Wii U binaries should apply this filter. Without it the
numbers are an upper bound roughly five times the real demand.

### What the two corpora disagree about

The homebrew corpus put the 25 `FSA*` functions at the top of the
filesystem priority list. Black Ops 2 calls **none of them** — it uses the
high-level `FS*` API (`FSInit`, `FSAddClient`, `FSInitCmdBlock`,
`FSOpenFile`). `FSA*` are the low-level primitives wut's own runtime uses
internally.

Implementing the filesystem from homebrew data alone would have meant
writing 25 functions the game never calls. This is the clearest argument
for measuring against real titles.

### Current corpus and its limits

4 homebrew titles: 681 distinct imports across 8 libraries, 348 in
`coreinit` alone.

**This corpus measures homebrew, not games.** It over-represents
`OSScreen`, since `WHBLogConsole` draws through it, and under-represents
`GX2`, which is how commercial titles actually render. Treat the ranking as
"the floor any Cafe OS binary needs", not as a picture of what games do.

A commercial corpus is the obvious next step. Contributions of import lists
from titles you own — the CSV output, never the binaries — are very welcome.

Only derived data is published here: function names and counts. A list of
imported symbol names is factual metadata, not game content.

---

## Not implemented yet

31 symbols remain on Black Ops 2:

- [ ] **Green Hills runtime** (9) — `__ghsLock`, `__ghsUnlock`,
      `__ghs_flock_*`, `__gh_get_errno`, `__gh_set_errno`. The Wii U SDK
      used the GHS compiler; its runtime locking and file-locking hooks have
      no host equivalent. Now the largest remaining group.
- [ ] **`UC*`** (5) — user configuration over IPC
- [ ] **`MCP_*`** (3) — system configuration over IPC
- [ ] **`OSDriver_Register` / `OSDriver_Deregister`**
- [ ] `OSYieldThread`, `PPCSync`, `OSCompareAndSwapAtomicEx64`,
      `OSSetExceptionCallback`, `OSReleaseForeground`,
      `OSEnableHomeButtonMenu`, `ENVGetEnvironmentVariable`

On homebrew, `OSScreen` (8 functions at 75%) is the last compact group —
and the only graphics code anywhere in this project. Below it, the `FSA*`
family: the low-level filesystem primitives wut's runtime uses internally,
absent from Black Ops 2.

---

## Open questions

**libc symbol collision.** `coreinit` exports `exit`, `__os_snprintf` and
presumably other names that newlib already defines. Implementing them here
would collide at link time. This needs a project-wide linking strategy —
symbol prefixing, `ld --wrap`, or a freestanding build — not a per-function
workaround. `exit` appears in 100% of the corpus, so this blocks real
coverage.

**Function pointers imported as data.** `MEMAllocFromDefaultHeap`,
`MEMAllocFromDefaultHeapEx` and `MEMFreeToDefaultHeap` appear as
`.dimport_` entries, meaning games read a function *pointer* from
`coreinit`'s data segment and call through it. That requires exporting
global variables holding addresses, not function symbols — a mechanism this
project does not yet have.

**Guest stacks.** See the thread section above. This one needs someone who
knows how the recompiler emits code.

**Dynamic module loading.** `OSDynLoad_Acquire` and `OSDynLoad_FindExport`
are used by Black Ops 2. The game resolves symbols at runtime, which sits
awkwardly with static recompilation — that approach assumes everything is
known ahead of time. What gets loaded, and when, needs investigating.

---

## Building

Requires [devkitPro](https://devkitpro.org) with the `switch-dev` group.

```sh
make
```

Produces `coreinit-nx.nro`. Copy it to `/switch/` on your SD card and run it
from the Homebrew Menu. It runs the test suite and prints results to screen.

---

## Contributing

Adding a function means, in order:

1. **Check the census** in `census/frequency.csv`. If it is not used by any
   title in the corpus, it is probably not the best use of your time.
2. **Read the semantics** in decaf-emu, under
   `libdecaf/src/cafe/libraries/coreinit/`. This is a full HLE
   implementation and serves as the specification. Do not guess.
3. **Check the ABI size** in wut's headers
   (`WUT_CHECK_SIZE(TypeName, ...)`), and mirror wut's declarations
   literally, even where types are equivalent.
4. **Map it onto libnx.** Prefer libnx kernel primitives over C++ standard
   library equivalents — the toolchain builds with `-fno-exceptions`, and
   depending on the standard library's threading support adds a failure
   mode unrelated to this project.
5. **Write a test** that runs on device. Prefer tests where a wrong
   implementation hangs or fails visibly rather than passing silently.
6. **Update `tools/rpx-imports/implemented.txt`** so coverage stays honest.
7. **Open a pull request**, stating what you verified on hardware and what
   you only inferred from source.

That last distinction matters more than anything else here. The value of
this repository is not the code — it is knowing which behaviours have
actually been confirmed on a real console.

---

## Licence

MIT.

Cemu is MPL-2.0 and decaf-emu is GPL. Neither is copied from; both are read
as documentation of semantics, and the implementation here is written
independently against libnx.
