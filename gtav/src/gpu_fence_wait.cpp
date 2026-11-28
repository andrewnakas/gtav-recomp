// gtav - the game blocking on a GPU fence (native renderer, --gpu_skip_rendering).
//
// Every frame the update thread waits until the GPU has finished an earlier frame (sub_835833D8
// -> sub_832BA460 -> D3D 8380CEF8, which spins on the completed-fence value at device+11024).
// The command processor runs --gpu_skip_rendering_lag_ms behind the CPU's kicks (so draw data
// the game fills after a kick is complete when it is read), which made every such wait take the
// whole lag: ~6 ms of a ~20 ms frame. While a game thread is blocked here the SDK command
// processor lets kicks through one at a time instead (rex_skip_rendering_gpu_waiters).

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "gpu_fence_wait.h"
#include "wait_profile.h"

// SDK (librexgpu): wakes the command processor from its idle sleep.
extern "C" void rex_skip_rendering_wake();

namespace {
std::atomic<int> g_gpu_waiters{0};
}

#ifdef _WIN32
#define GTAV_HOOK_EXPORT __declspec(dllexport)
#else
#define GTAV_HOOK_EXPORT __attribute__((visibility("default")))  // exported by the link options
#endif
extern "C" GTAV_HOOK_EXPORT int rex_skip_rendering_gpu_waiters() { return g_gpu_waiters.load(std::memory_order_acquire); }

REXCVAR_DECLARE(bool, gtav_wait_profile);
// D3D waits for a fence by spinning (8380CEF8 loops on the spin step 83807DC8: no kernel call).
// On the console that is a hardware thread of its own; here it is a core at full clock for as long
// as the GPU is the slower side: 66% of the main thread on Linux at full size with a shared GPU,
// and on a phone heat that comes out of the GPU's and the other cores' clock budget.
// N >= 0: after N microseconds of spinning the thread sleeps until the renderer releases a fence
// (capture.cpp tells us at each write). -1: the game's own spin throughout.
// Same-run A/Bs 11-23, spin | sleep after 200 us, fps: S23 FE 32.4 | 32.8, 36.8 | 37.2, 32.4 | 32.2,
// 30.1 | 29.6, 31.8 | 31.5, 30.6 | 30.5; Linux at full size (GPU shared) 36.8 | 37.6, 26.3 | 26.0,
// 35.6 | 34.0, 29.7 | 30.0 and at quarter size (the game's code sets the frame) 65.0 | 68.7,
// 86.3 | 105.4, 96.1 | 105.4, 66.4 | 69.1, 57.6 | 57.5, 61.1 | 59.5, 62.9 | 67.7. Main thread on Linux
// 97% -> 41% of a core; on the phone 21-47 -> 16-17 M instructions a frame.
// On for Linux and Android (measured there) and the iPhone (13 mini 11-27, same-run A/B spin |
// sleep after 200 us: standing 40.9 | 42.4 fps, both report lines; scripted run 25.2 | 25.6 with
// recording 18.1 | 14.8 ms) and the Mac (M1 Air 11-24, 9 A/B reports: +0.3% fps, 4.6 -> 4.1 cores);
// Windows keeps the spin until tested.
#if defined(__linux__) || defined(__APPLE__)
#define GTAV_FENCE_BLOCK_DEFAULT 200
#else
#define GTAV_FENCE_BLOCK_DEFAULT -1
#endif
REXCVAR_DEFINE_INT32(gtav_fence_block_after_us, GTAV_FENCE_BLOCK_DEFAULT, "GTAV",
                     "Game threads waiting for a GPU fence sleep after spinning N microseconds (-1 = spin, live)");

namespace {
uint32_t Be32(const uint8_t* base, uint32_t address) {
  const uint8_t* p = base + address;
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
// --gtav_wait_profile: how far the fence waited for is behind the newest one the CPU issued and
// ahead of the one the GPU completed (in fence steps of 2), and how long the waits take.
struct {
  uint64_t waits = 0, behind_newest = 0, ahead_of_done = 0, newest = 0;
  double ms = 0;
} g_fence_stats;

// The fence wait in progress on this thread (8380CEF8), for the spin step it calls.
thread_local uint32_t t_wait_device = 0, t_wait_fence = 0;
thread_local std::chrono::steady_clock::time_point t_wait_start;
std::mutex g_block_mutex;
std::condition_variable g_block_cv;
std::atomic<int> g_block_sleepers{0};

// The game's own test in 8380CEF8: the fence is no further behind the newest issued one than the
// completed one is.
bool FenceDone(const uint8_t* base, uint32_t device, uint32_t fence) {
  const uint32_t next = Be32(base, device + 11036), done = Be32(base, Be32(base, device + 11024));
  return next - fence >= next - done;
}
}  // namespace

namespace gtav {
void FenceWritten() {
  // The value is in guest memory (a plain store); a sleeper counts itself before it reads it.
  std::atomic_thread_fence(std::memory_order_seq_cst);
  if (g_block_sleepers.load(std::memory_order_seq_cst) == 0) return;
  { std::lock_guard lock(g_block_mutex); }
  g_block_cv.notify_all();
}
}  // namespace gtav

// D3D's spin step: a few empty iterations, then its hang check; the caller loops on it until its
// fence is done. Inside 8380CEF8, once the wait has outlasted the spin allowance, sleep until the
// fence is done instead (2 ms at most at a time, so the step's hang check and the caller's own
// test still run).
REX_EXTERN(__imp__sub_83807DC8);
REX_HOOK_RAW(sub_83807DC8) {
  if (t_wait_fence) {
    if (const int32_t after = REXCVAR_GET(gtav_fence_block_after_us);
        after >= 0 && std::chrono::steady_clock::now() - t_wait_start >= std::chrono::microseconds(after)) {
      const uint32_t device = t_wait_device, fence = t_wait_fence;
      g_block_sleepers.fetch_add(1, std::memory_order_seq_cst);
      {
        std::unique_lock lock(g_block_mutex);
        g_block_cv.wait_for(lock, std::chrono::milliseconds(2), [&] { return FenceDone(base, device, fence); });
      }
      g_block_sleepers.fetch_sub(1, std::memory_order_seq_cst);
    }
  }
  __imp__sub_83807DC8(ctx, base);
}

// D3D: block until the GPU passes a fence (r3 device, r4 fence value).
REX_EXTERN(__imp__sub_8380CEF8);
REX_HOOK_RAW(sub_8380CEF8) {
  g_gpu_waiters.fetch_add(1, std::memory_order_acq_rel);
  rex_skip_rendering_wake();  // it may be sleeping up to 1 ms before it lets kicks through
  const bool stats = REXCVAR_GET(gtav_wait_profile);
  uint32_t device = ctx.r3.u32, fence = ctx.r4.u32;
  auto start = std::chrono::steady_clock::now();
  if (stats && device && fence) {
    uint32_t next = Be32(base, device + 11036), done = Be32(base, Be32(base, device + 11024));
    g_fence_stats.behind_newest += (next - fence) / 2;
    g_fence_stats.ahead_of_done += int32_t(fence - done) > 0 ? (fence - done) / 2 : 0;
    g_fence_stats.newest += fence == next;
  }
  t_wait_device = device, t_wait_fence = device ? fence : 0, t_wait_start = start;
  gtav::WaitProfileTimed(0x8380CEF8, ctx, base, [](PPCContext& c, uint8_t* b) { __imp__sub_8380CEF8(c, b); });
  t_wait_fence = 0;
  g_gpu_waiters.fetch_sub(1, std::memory_order_acq_rel);
  if (stats && device && fence) {
    g_fence_stats.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (++g_fence_stats.waits == 500) {
      auto& st = g_fence_stats;
      REXLOG_INFO("[waits] GPU fence waits: avg {:.2f} ms, target {:.1f} fences behind the newest issued "
                  "({:.0f}% the newest), {:.1f} ahead of the completed one",
                  st.ms / st.waits, double(st.behind_newest) / st.waits, 100.0 * st.newest / st.waits,
                  double(st.ahead_of_done) / st.waits);
      st = {};
    }
  }
}
