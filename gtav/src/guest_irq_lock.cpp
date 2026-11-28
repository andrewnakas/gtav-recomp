// gtav - the guest's "interrupts off" sections.
//
// The 360's inline interlocked operations switch interrupts off around their load-reserve /
// store-conditional pair (mfmsr rX; mtmsrd r13; lwarx; ...; stwcx.; mtmsrd rX). The runtime maps
// "interrupts off" to its one global recursive lock, so every interlocked operation of every game
// thread queued for the same mutex, about a million times a second. S23 FE 11-23: 5.5% of the
// process's CPU samples (a quarter of a core) in the three calls, much of it in the kernel, since
// a thread that finds the lock taken goes to sleep for a section a few instructions long.
//
// GTA V has 1,309 such sections. 1,308 are a load-reserve, register arithmetic and a
// store-conditional: exchange (424), add (406 + 372), compare-exchange (63 + 30), and/or (13).
// The generated stwcx. is a host compare-and-swap against the value its lwarx read, atomic by
// itself, and nothing between the two reads shared memory, so the result is one the console could
// have produced (there the store fails when the reservation is lost and the loop runs again).
// These need no lock: gtav_guest_irq_lock=0.
//
// The last one is split over a helper pair: 83829CC0 (interrupts off, ldarx of a list
// head) and 83829CD8 (stdcx., interrupts back on), called by the pop (83570218) and the push
// (835702C0) of a lock-free pool's free list, head = {first node, depth}. The pop reads the first
// node's link between the two calls, and the depth is not a sequence number: on the console the
// reservation fails the store when another thread popped and pushed in between; a
// compare-and-swap alone would accept the same head value with a stale link (ABA) and hand out a
// node twice. This pair keeps the global lock, in every mode: pool operations exclude each other
// exactly as before.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/thread/mutex.h>

#include <mutex>

#include "guest_irq_lock.h"

// Measured on Linux, Android, the iPhone and the Mac (max-perf line): Windows keeps the lock.
// M1 Air 11-24, 9 A/B reports lock | no lock: +0.3% fps (48-58 fps both sides).
// iPhone 13 mini 11-27, same-run A/B lock | no lock: standing 40.3 | 41.7 fps (both report lines),
// scripted run 22.1 | 22.4.
#if defined(__linux__) || defined(__APPLE__)
#define GTAV_GUEST_IRQ_LOCK_DEFAULT 0
#else
#define GTAV_GUEST_IRQ_LOCK_DEFAULT 1
#endif
REXCVAR_DEFINE_INT32(gtav_guest_irq_lock, GTAV_GUEST_IRQ_LOCK_DEFAULT, "GTAV",
                     "The game's interlocked operations: 0 = no lock, 1 = the runtime's global lock, 2 = as "
                     "before 11-23 (live with a separate game library; with the game code inside the "
                     "executable the build decides, REX_GUEST_IRQ_LOCK)");

namespace {
thread_local int32_t t_pair_locked = 0;
}  // namespace

// (r3 list head, r4 where to keep the MSR) -> r3 the head; interrupts stay off.
REX_EXTERN(__imp__sub_83829CC0);
REX_HOOK_RAW(sub_83829CC0) {
  rex::thread::global_critical_region::mutex().lock();  // recursive: the body may take it again
  t_pair_locked++;
  __imp__sub_83829CC0(ctx, base);
}

// (r3 list head, r4 new head, r5 the kept MSR) -> r3 1 stored / 0 try again; interrupts back on.
REX_EXTERN(__imp__sub_83829CD8);
REX_HOOK_RAW(sub_83829CD8) {
  __imp__sub_83829CD8(ctx, base);
  if (t_pair_locked) {
    t_pair_locked--;
    rex::thread::global_critical_region::mutex().unlock();
  }
}
