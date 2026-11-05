// gtav - workaround: cutscene audio streams never report "finished".
//
// At cutscene end the game calls sub_823F30F0(audio, immediate=0, fade). With audio+5072 set,
// that defers the stream release until each stream stops playing on its own, but in our
// runtime a stream that has played to its end (pos +124 >= end +136) stays "playing" forever,
// so the slot (controller 0x83A320C0 + 10296, 2 slots) is never freed, the audio sits in state 5
// and the next cutscene hangs on load (the state-2 attach handler sub_824DC130 waits on IsBusy).
// While the next cutscene waits, if the audio is in state 5 and its streams are at their end,
// do the immediate release (free the slots with a short fade, then Reset) that the game was
// waiting for. Also when the streams stopped moving before their end (see StreamDone). Root cause (missing end-of-stream completion in the runtime) is still open.

#include <rex/hook.h>
#include <rex/logging.h>

#include <chrono>
#include <cstdint>

namespace {

constexpr uint32_t kAudioObj = 0x83A320F8;   // cutscene audio entity (controller + 56)
constexpr uint32_t kStreamSlots = 0x83A320C0 + 10296;

uint32_t be32(const uint8_t* base, uint32_t addr) {
  const uint8_t* p = base + addr;
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

// A stream the deferred release waits for: done when it played to its end, or when its position
// (ms) stopped moving. In our runtime a stream that was stopped or faded out before its end stays
// "playing" as well: the next cutscene then waited for the slot, gave up after a few seconds and
// played without audio (issues #3, #4: freeze, then no dialogue for the rest of the cutscene).
struct Watch {
  uint32_t stream = 0, pos = 0;
  std::chrono::steady_clock::time_point since{};
};
Watch g_watch[2];

bool StreamDone(const uint8_t* base, int32_t slot_index, Watch& w, bool* stalled) {
  if (slot_index < 0) return true;
  uint32_t s = be32(base, kStreamSlots + 4 * uint32_t(slot_index));
  if (s < 0xC0000000 || s >= 0xE0000000) return true;  // free / not a stream
  uint32_t pos = be32(base, s + 124), end = be32(base, s + 136);
  if (end != 0 && pos >= end) return true;
  const auto now = std::chrono::steady_clock::now();
  if (w.stream != s || w.pos != pos) {
    w = {s, pos, now};
    return false;
  }
  if (now - w.since >= std::chrono::milliseconds(250)) {
    *stalled = true;
    return true;
  }
  return false;
}

}  // namespace

REX_EXTERN(__imp__sub_823F30F0);

static void UnstickCutsceneAudio(PPCContext& ctx, uint8_t* base) {
  if (be32(base, kAudioObj + 5068) != 5 || !base[kAudioObj + 5072]) return;
  int32_t a = int32_t(be32(base, kAudioObj + 5044)), b = int32_t(be32(base, kAudioObj + 5048));
  bool stalled = false;
  const bool done_a = StreamDone(base, a, g_watch[0], &stalled);
  const bool done_b = StreamDone(base, b, g_watch[1], &stalled);
  if (!done_a || !done_b) return;
  REXLOG_WARN("[cuts] stream(s) {} {} {} but never released: forcing audio release", a, b,
              stalled ? "stopped before their end" : "finished");
  g_watch[0] = g_watch[1] = {};
  PPCContext saved = ctx;
  ctx.r3.u64 = kAudioObj;
  ctx.r4.u64 = 1;   // immediate
  ctx.r5.u64 = 10;  // fade ms
  __imp__sub_823F30F0(ctx, base);
  ctx = saved;
}

// CutSceneManager state 2 handler: attach the streamed cut file.
REX_EXTERN(__imp__sub_824DC130);
REX_HOOK_RAW(sub_824DC130) {
  UnstickCutsceneAudio(ctx, base);
  __imp__sub_824DC130(ctx, base);
}
