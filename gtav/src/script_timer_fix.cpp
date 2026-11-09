// gtav - script thread timers (TIMERA/TIMERB) at frame rates above the 360's 30 fps.
//
// sub_835243F0 (scrThread::Run's wrapper) adds int(time step * 1000) to the thread's TimerA (+28)
// and TimerB (+32) every frame, truncating (fctiwz). The 360 runs at 30 fps, so a frame is >= 1 ms
// even in slow motion. Above that a slowed frame truncates to 0 and the timers stop: #21, dying
// shows WASTED forever, because respawn_controller slows time (SET_TIME_SCALE 0.05) and waits for
// TIMERB > 100 before it fades out and respawns. The dropped fraction is carried per thread here and
// added before the original runs, so the timers advance at the game's real (scaled) speed.

#include <rex/hook.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace {

constexpr uint32_t kTimeStep = 0x839C7844;   // float, the scaled game time step in seconds
constexpr uint32_t kMsPerSecond = 0x8206EFE4;  // float 1000.0, the constant the game multiplies by
constexpr uint32_t kFlagA = 0x83DDD629, kFlagB = 0x839C784C;  // the game's "timers advance" test

uint32_t be32(const uint8_t* base, uint32_t addr) {
  const uint8_t* p = base + addr;
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
void store_be32(uint8_t* base, uint32_t addr, uint32_t v) {
  uint8_t* p = base + addr;
  p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
}

// Script threads all run on the one script update thread, so no lock is needed.
std::unordered_map<uint32_t, float> g_carry;

}  // namespace

REX_EXTERN(__imp__sub_835243F0);
REX_HOOK_RAW(sub_835243F0) {
  const uint32_t thread = ctx.r3.u32;
  const bool advances = base[kFlagA] == 0 || base[kFlagB] != 0;
  if (thread >= 0x10000 && advances) {
    // The game's own product, in the same float precision, so the carry is exactly what it drops.
    const float ms = std::bit_cast<float>(be32(base, kTimeStep)) * std::bit_cast<float>(be32(base, kMsPerSecond));
    if (ms > 0.0f && ms < 1000.0f) {
      float& carry = g_carry[thread];
      carry += ms - std::trunc(ms);
      if (carry >= 1.0f) {
        const uint32_t extra = uint32_t(carry);
        carry -= float(extra);
        store_be32(base, thread + 28, be32(base, thread + 28) + extra);
        store_be32(base, thread + 32, be32(base, thread + 32) + extra);
      }
    }
  }
  __imp__sub_835243F0(ctx, base);
}
