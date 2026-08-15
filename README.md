# coreinit-nx

An implementation of the Wii U **Cafe OS `coreinit`** API on top of
[libnx](https://github.com/switchbrew/libnx), so that statically
recompiled Wii U code can run natively on Nintendo Switch homebrew.

> **Status: early.** One module implemented and verified on hardware.
> This is a foundation, not a finished runtime.

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

### Lazy creation

`HandleTable::get()` creates the host object on first use rather than
requiring explicit initialisation. Game code may use a primitive that was
statically initialised and never passed to an `OSInit*` call. Degrading
cleanly beats crashing.

---

## Implemented

### `coreinit/mutex.h`

`OSInitMutex`, `OSInitMutexEx`, `OSLockMutex`, `OSUnlockMutex`,
`OSTryLockMutex`

**Verified on hardware:** Cafe OS mutexes are recursive — the same thread
may acquire one repeatedly. libnx `Mutex` is *not* recursive; `RMutex` is.
Mapping `OSMutex` onto `RMutex` was tested on a real console and behaves
correctly, including the recursive acquire case.

This is worth stating explicitly because getting it wrong does not fail
loudly: it surfaces later as intermittent deadlocks when the game enters a
code path that reacquires a lock it already holds.

`OSMutex` size: `0x2c` bytes.

### `coreinit/condition.h`

`OSInitCond`, `OSInitCondEx`, `OSWaitCond`, `OSSignalCond`

`OSSignalCond` is a **broadcast**, mapped to `condvarWakeAll`. This is not
an assumption: wut documents it as "will wake up any threads waiting",
equivalent to `notify_all`.

`OSCondition` size: `0x1c` bytes.

**Verified on hardware:** a thread holding the mutex at recursion depth 2
can wait on a condition, be woken by another thread, and resume with its
recursion depth intact. Verified by a third thread attempting to acquire
the mutex after a single unlock — it must fail.

### `coreinit/thread.h`

`OSCreateThread`, `OSResumeThread`, `OSSuspendThread`, `OSJoinThread`,
`OSExitThread`

`OSThread` size: `0x6a0` bytes.

**Verified on hardware:** `OSCreateThread` creates a thread *suspended*, as
Cafe OS does — it does not run until `OSResumeThread`. Argument passing and
the exit value survive the entry point trampoline.

Mapping decisions, all arbitrary and open to revision:

- **Stack.** The guest-supplied stack pointer is stored but unused; libnx
  allocates the host stack, sized from the guest's `stackSize` request.
  Recompiled code will need the guest stack as its emulated PPC stack, but
  how that separates from the ARM64 host stack depends on how the
  recompiler emits code — unknown at time of writing.
- **Priority.** Cafe OS 0–31 (lower is higher) is mapped linearly onto the
  window between the process priority and `0x3F`. Relative ordering is
  preserved, absolute values are not.
- **Affinity.** Cafe OS affinity is a bitmask over 3 cores; libnx wants a
  single core id. First set bit wins, `-2` for "any", and a failed create
  retries on `-2` rather than giving up.
- **Entry point.** Cafe OS uses `int(int, const char**)`, libnx uses
  `void(void*)`. A trampoline bridges them and captures the return value.

**Not implemented:** suspending an already-running thread. `OSSuspendThread`
maintains the counter but does not stop execution.
---
## Design notes

### Mutexes do not use libnx `RMutex`

The obvious mapping for a recursive mutex is `RMutex`, and it works in
isolation. It breaks as soon as condition variables arrive: `condvarWait`
takes a plain `Mutex`, and it releases it exactly once — while an
`RMutex`'s recursion counter would remain untouched, leaving inconsistent
state.

Instead, `HostMutex` locks a plain `Mutex` **exactly once** regardless of
recursion depth, tracking the depth itself. `OSWaitCond` then saves the
depth, lets `condvarWait` perform its single release and reacquire, and
restores it.

`mutexIsLockedByCurrentThread` makes ownership tracking possible without
handling thread tags manually.

### Known limitation: structure tags are not written

`OSCondition` carries a `tag` field expected to hold `OS_CONDITION_TAG`
(`0x634E6456`). Since guest structures are treated as opaque keys, this
field is never written. Game code calling into `coreinit` will not notice,
but code that validates tags — as debug builds do — would.

### Creating threads on Horizon

Thread priority must be queried with `svcGetThreadPriority` rather than
hardcoded: a thread cannot be created with better priority than the
process, and the value depends on how the homebrew was launched. Use
`cpuid = -2` for the process default core.

---
## Not implemented yet

In rough dependency order:

- [x] Condition variables — `OSInitCond`, `OSWaitCond`, `OSSignalCond`
- [ ] Threads — `OSCreateThread`, `OSJoinThread`, `OSExitThread`.
      Core affinity needs a decision: the Wii U has 3 cores, Horizon
      exposes a different number and reserves one.
- [ ] Time — `OSGetTime`, `OSGetSystemTime`, `OSTicksToCalendarTime`
- [ ] Heaps — `MEMCreateExpHeap`, `MEMAllocFromExpHeap`. Endianness
      returns here: games inspect heap structures directly.
- [ ] Filesystem — `FSOpenFile`, `FSReadFile`
- [ ] Everything else

Graphics (`GX2`) is explicitly **out of scope** for this repository. It is a
much larger problem and deserves its own project.

---

## Building

Requires [devkitPro](https://devkitpro.org) with the `switch-dev` group.

```sh
make
```

Produces `coreinit-nx.nro`. Copy it to `/switch/` on your SD card and run it
from the Homebrew Menu. It executes the test suite and prints results to
screen.

---

## Contributing

Adding a function means, in order:

1. **Read the semantics** in decaf-emu, under
   `libdecaf/src/cafe/libraries/coreinit/`. This is a full HLE
   implementation and serves as the specification. Do not guess.
2. **Check the ABI size** in wut's headers
   (`WUT_CHECK_SIZE(TypeName, ...)`).
3. **Map it onto libnx.** Prefer libnx kernel primitives over C++ standard
   library equivalents — the toolchain builds with `-fno-exceptions`, and
   depending on the standard library's threading support adds a failure
   mode that has nothing to do with this project.
4. **Write a test** that runs on device. Prefer tests where a wrong
   implementation hangs or fails visibly rather than passing silently.
5. **Open a pull request**, stating what you verified on hardware and what
   you only inferred from source.

That last distinction matters more than anything else here. The value of
this repository is not the code — it is knowing which behaviours have
actually been confirmed on a real console.

---

## Licence

MIT.
