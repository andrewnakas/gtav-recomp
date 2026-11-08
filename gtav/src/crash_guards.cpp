// gtav - guards for guest code that dereferences objects the recompiled game can reach in a state
// the console never shows it (timing differs from the 360), where the 360 itself would crash.

#include <rex/hook.h>
#include <rex/logging.h>

#include <atomic>
#include <cstdint>

namespace {

uint32_t be32(const uint8_t* base, uint32_t addr) {
  const uint8_t* p = base + addr;
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

}  // namespace

// #3 (Windows, Mission Failed in the prologue): crash.txt read guest 0x000000E4 with
// LR 0x8282C59C. sub_8282C548 walks a list of objects and calls sub_8281E5F0(object, r29) on
// each; sub_8281E5F0 calls the object's virtual function at vtable +228 (slot 57). The object had
// vtable 0: its memory was already cleared (destroyed while still listed). A cleared object is no
// object any more, so it is skipped instead of calling through address 0.
REX_EXTERN(__imp__sub_8281E5F0);
REX_HOOK_RAW(sub_8281E5F0) {
  const uint32_t object = ctx.r3.u32;
  if (object >= 0x10000 && be32(base, object) == 0) {
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1, std::memory_order_relaxed) < 8)
      REXLOG_WARN("[guard] sub_8281E5F0: object {:08X} has no vtable (cleared while listed), skipped; lr {:08X}",
                  object, uint32_t(ctx.lr));
    return;
  }
  __imp__sub_8281E5F0(ctx, base);
}
