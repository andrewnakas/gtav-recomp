// gtav - per-second frame and draw statistics.
//
// Milestone 0/1 of the native renderer: confirms the D3D addresses in d3d_map.h (plausible
// per-frame call counts, game behaves the same) and gives a frame-time baseline. Swap time is
// the guest thread blocked in D3D Swap (VdSwap + GPU backpressure), so a high swap share
// means the GPU side is the bottleneck.

#include "stats.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

REXCVAR_DEFINE_BOOL(gtav_native_stats, false, "GTAV", "Log per-second frame time and D3D call counts");

namespace gtav::native {
namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kNames[kStatCount] = {"drawidx", "draw", "beginverts", "drawother", "vs",
                                            "ps",      "alu",  "rs",         "ib",        "vb"};

std::atomic<uint64_t> g_counts[kStatCount];

struct FrameStats {
  Clock::time_point window_start{}, last_swap{};
  uint32_t frames = 0;
  double max_ms = 0, swap_ms = 0;
  std::vector<double> intervals;  // frame-to-frame ms this window, for the 1% / 5% lows
};
FrameStats g_frame;  // touched only by the swapping thread

}  // namespace

bool StatsEnabled() { return REXCVAR_GET(gtav_native_stats); }

void StatCount(StatCounter c) {
  if (StatsEnabled()) g_counts[c].fetch_add(1, std::memory_order_relaxed);
}

void StatsSwap(Clock::time_point begin, Clock::time_point end, uint32_t device) {
  auto& f = g_frame;
  if (f.frames == 0 && f.window_start == Clock::time_point{}) f.window_start = begin;
  if (f.last_swap != Clock::time_point{}) {
    double ms = std::chrono::duration<double, std::milli>(end - f.last_swap).count();
    f.max_ms = std::max(f.max_ms, ms);
    f.intervals.push_back(ms);
  }
  f.last_swap = end;
  f.swap_ms += std::chrono::duration<double, std::milli>(end - begin).count();
  f.frames++;
  double window = std::chrono::duration<double>(end - f.window_start).count();
  if (window < 1.0) return;
  uint64_t c[kStatCount];
  for (int i = 0; i < kStatCount; i++) c[i] = g_counts[i].exchange(0, std::memory_order_relaxed);
  std::string per_frame;
  for (int i = 0; i < kStatCount; i++) per_frame += fmt::format(" {}={}", kNames[i], c[i] / f.frames);
  // p99 / p95 frame intervals: the 1% and 5% lows of this window (1000 / p99 = "1% low fps").
  double p99 = 0, p95 = 0;
  if (!f.intervals.empty()) {
    std::sort(f.intervals.begin(), f.intervals.end());
    p99 = f.intervals[std::min(f.intervals.size() - 1, size_t(f.intervals.size() * 99 / 100))];
    p95 = f.intervals[std::min(f.intervals.size() - 1, size_t(f.intervals.size() * 95 / 100))];
  }
  REXLOG_INFO("[native] fps={:.1f} avg={:.2f}ms max={:.2f}ms p99={:.2f}ms p95={:.2f}ms swap={:.2f}ms/frame dev=0x{:08X}{}",
              f.frames / window, window * 1000.0 / f.frames, f.max_ms, p99, p95, f.swap_ms / f.frames, device,
              per_frame);
  f = FrameStats{};
  f.window_start = end;
  f.last_swap = end;
}

}  // namespace gtav::native
