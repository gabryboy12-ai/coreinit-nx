# coreinit-nx

An implementation of the Wii U **Cafe OS `coreinit`** API on top of
[libnx](https://github.com/switchbrew/libnx), so that statically
recompiled Wii U code can run natively on Nintendo Switch homebrew.

=======
> **Status: early.** Eight modules implemented, 36 tests passing on real
> hardware. Measured coverage: **26.0%** of coreinit requirements across a
> 4-title homebrew corpus and 1 original WiiU title. This is a foundation, 
> not a finished runtime.
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

Ordered by measured frequency in the current corpus:

- [x] **Heaps** — `MEMAllocFromExpHeapEx`, `MEMFreeToExpHeap`,
      `MEMCreateExpHeapEx`, `MEMAllocFromFrmHeapEx`,
      `MEMRecordStateForFrmHeap`, `MEMFreeByStateToFrmHeap`,
      `MEMGetBaseHeapHandle` and friends. Present in **100%** of the corpus.
      Two distinct allocators: FrmHeap is a two-ended stack, ExpHeap is a
      general-purpose allocator. This is the first module that requires
      *implementing* rather than *mapping*: `MEMCreateExpHeapEx` receives a
      region of guest memory and must suballocate it, because returned
      pointers have to fall inside that region — games do arithmetic on them.
- [ ] `OSSavesDone_ReadyToRelease` — 100%
- [ ] Alarms — `OSCreateAlarm`, `OSCancelAlarm`, `OSGetAlarmUserData` — 75%
- [ ] Thread-local storage — `OSGetThreadSpecific`, `OSSetThreadSpecific`
- [ ] `OSFastMutex_*` — a second, distinct mutex family
- [ ] Semaphores — `OSInitSemaphore`
- [ ] Spinlocks — `OSUninterruptibleSpinLock_*`
- [ ] Filesystem — the `FSA*` family, 25 functions
- [ ] `OSFatal`, thread introspection (`OSGetThreadPriority`,
      `OSGetThreadAffinity`)

Graphics (`GX2`) is explicitly **out of scope** for this repository. 202
distinct imports already appear in a 4-title corpus. It deserves its own
project.

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
