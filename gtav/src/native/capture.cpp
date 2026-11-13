// gtav - guest D3D state tracking and frame recording for the native renderer.
//
// Hooks report bound objects (index buffer, streams, shaders) and commands here; each draw
// snapshots the device register shadow and constants into the current FramePacket, which the
// Swap hook hands to the render thread. --gtav_native_layout_probe=N logs the objects seen by
// the first draws of guest frame N (used to verify layouts before the draw path relies on them).

#include "capture.h"

#include "d3d_map.h"
#include "frame.h"
#include "paths.h"
#include "renderer.h"
#include "shaders.h"
#include "xenos_regs.h"

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <rex/cvar.h>
#include <rex/kernel.h>
#include <rex/logging.h>
#include <rex/system.h>

#define XXH_INLINE_ALL
#include <xxhash.h>

#include "safe_read.h"
#include "../linux_threads.h"
#ifndef _WIN32
#include <unistd.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <objc/message.h>
#include <objc/runtime.h>
#if TARGET_OS_IPHONE
#include <os/proc.h>
#endif
extern "C" void* MTLCreateSystemDefaultDevice(void);
#if TARGET_OS_IPHONE
extern "C" int gtav_ios_thermal_state();  // ios_setup.mm
#endif
// Bytes the process has allocated on the (only) Metal device: MoltenVK's buffers and textures.
static uint64_t MetalAllocatedBytes() {
  static void* device = MTLCreateSystemDefaultDevice();
  if (!device) return 0;
  using Fn = uint64_t (*)(void*, SEL);
  return reinterpret_cast<Fn>(objc_msgSend)(device, sel_registerName("currentAllocatedSize"));
}
#endif
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <string>

REXCVAR_DECLARE(std::string, gtav_native);
REXCVAR_DEFINE_BOOL(gtav_native_snapshot_draws, false, "GTAV",
                    "Copy every draw's vertex and index data at the D3D call instead of reading guest "
                    "memory when the frame is recorded");
REXCVAR_DEFINE_INT32(gtav_native_fence_lag, 0, "GTAV",
                     "Kicks the game's GPU fence is held back (the real GPU runs behind the CPU; released "
                     "when the command processor idles and at every swap)");
// On by default on macOS: the renderer there runs up to a few frames behind (record ~20-33 ms per
// open-world frame), so the game recycled streamed meshes and textures under queued frames:
// one-frame exploded geometry, black ground and sky (ow2: ~5 per 1000 frames, 2 in 21000 with the
// wait, same fps). Android's renderer runs behind the same way (record ~25 ms a frame). Linux
// keeps the copies.
#if defined(__APPLE__) || defined(__ANDROID__)
#define GTAV_FENCE_WAIT_DEFAULT true
#else
#define GTAV_FENCE_WAIT_DEFAULT false
#endif
REXCVAR_DEFINE_BOOL(gtav_native_fence_wait, GTAV_FENCE_WAIT_DEFAULT, "GTAV",
                    "The command processor waits at a GPU fence until the renderer recorded the draws before "
                    "it (textures too) when their frame is complete, instead of copying their vertex data");
// The phone records slowly enough that 100 ms timed out every few frames (slab flicker).
#if defined(__APPLE__) && TARGET_OS_IPHONE
#define GTAV_FENCE_WAIT_MS_DEFAULT 1000
#else
#define GTAV_FENCE_WAIT_MS_DEFAULT 100
#endif
REXCVAR_DEFINE_INT32(gtav_native_fence_wait_ms, GTAV_FENCE_WAIT_MS_DEFAULT, "GTAV",
                     "Longest wait of the command processor at a GPU fence for the renderer (then it copies, "
                     "or gives up for the frame being recorded; Mac: stalls past 100 ms left one-frame slabs)");
REXCVAR_DEFINE_BOOL(gtav_native_fence_wait_draw, false, "GTAV",
                    "Experiment, with --gtav_native_fence_wait: wait only until the draw before the fence is "
                    "recorded, not its whole frame (the command processor then reaches the fences of the frame "
                    "still being built and copies ~3x more draw data: no gain measured, Mac swapping)");
REXCVAR_DEFINE_BOOL(gtav_native_fence_copy, true, "GTAV",
                    "Copy a draw's vertex and index data when the GPU fence after it is reached before the "
                    "frame was recorded (the game may reuse that memory from then on)");
REXCVAR_DEFINE_BOOL(gtav_native_check_draw_data, false, "GTAV",
                    "Hash every draw's vertex and index data at capture and again at record time, and log how "
                    "many draws the game rewrote in between (diagnostic; costs ~40% of both threads)");
REXCVAR_DEFINE_BOOL(gtav_native_dump_on_collapse, false, "GTAV",
                    "Diagnostic: dump the first frame with a collapsed tree leaf draw (tree orbs)");
REXCVAR_DEFINE_BOOL(gtav_native_pm4_scan, false, "GTAV",
                    "Diagnostic: log ALU constant writes found in the PM4 stream between draws (tree orbs)");
REXCVAR_DEFINE_INT32(gtav_native_layout_probe, 0, "GTAV",
                     "Log objects and register shadow for the first draws of this guest frame");


namespace gtav::native {
namespace {

constexpr int kProbeDraws = 6;
constexpr uint32_t kStreamCount = kMaxStreams;

struct BoundState {
  uint32_t index_buffer = 0, vertex_shader = 0, pixel_shader = 0;
  uint32_t streams[kStreamCount][3] = {};  // vertex buffer object, offset, stride
};
BoundState g_bound;  // the game renders from one thread; revisit if that changes

// ALU constants the game writes inline into the command buffer (83813580 returns where the data
// goes), which never reach the device shadow. Kept per register until a regular
// Set*ShaderConstantF overwrites the register again.
struct InlineConstants {
  uint8_t data[2][256][16];
  bool valid[2][256];
  bool any = false;
  // The pending load: its data is written by the caller after 83813580 returns.
  uint32_t pending_ptr = 0, pending_bank = 0, pending_start = 0, pending_count = 0;
};
InlineConstants g_inline_consts;

void FlushInlineConstants() {
  auto& c = g_inline_consts;
  if (!c.pending_ptr) return;
  const uint8_t* src = REX_KERNEL_MEMORY()->TranslateVirtual(c.pending_ptr);
  for (uint32_t i = 0; i < c.pending_count && c.pending_start + i < 256; i++) {
    std::memcpy(c.data[c.pending_bank][c.pending_start + i], src + 16 * i, 16);
    c.valid[c.pending_bank][c.pending_start + i] = true;
  }
  c.any = true;
  c.pending_ptr = 0;
}
FramePacket g_packet;
std::atomic<uint64_t> g_frame{0};
struct Collapse {
  bool frame_bad = false, frame_good = false, requested = false;
  uint32_t streak = 0;  // consecutive frames with only collapsed leaf draws
} g_collapse;
int g_probe_logged = 0;

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

const uint8_t* Guest(uint32_t addr) { return REX_KERNEL_MEMORY()->TranslateVirtual(addr); }

std::string Words(const uint8_t* p, int n) {
  std::string s;
  for (int i = 0; i < n; i++) s += fmt::format("{:08X} ", be32(p + 4 * i));
  return s;
}

bool Probing() {
  int32_t at = REXCVAR_GET(gtav_native_layout_probe);
  return at > 0 && g_frame.load(std::memory_order_relaxed) == uint64_t(at) && g_probe_logged < kProbeDraws;
}

void ProbeDraw(const char* kind, uint32_t device) {
  g_probe_logged++;
  const uint8_t* dev = Guest(device);
  REXLOG_INFO("[native-probe] {} ib={:08X} vs={:08X}/{:016X} ps={:08X}/{:016X}", kind, g_bound.index_buffer,
              g_bound.vertex_shader, ShaderHash(g_bound.vertex_shader), g_bound.pixel_shader,
              ShaderHash(g_bound.pixel_shader));
  for (int i = 0; i < 4; i++)
    if (uint32_t rt = be32(dev + d3d::kDevRenderTargets + 4 * i))
      REXLOG_INFO("[native-probe]   rt{} {:08X}: {}", i, rt, Words(Guest(rt), 12));
  if (uint32_t ds = be32(dev + d3d::kDevDepthStencil))
    REXLOG_INFO("[native-probe]   ds {:08X}: {}", ds, Words(Guest(ds), 12));
  REXLOG_INFO("[native-probe]   viewport: {}", Words(dev + d3d::kDevViewport, 6));
}

// Guest D3D resources: a 24-byte header (Common, RefCount, Fence, ReadFence, Identifier,
// BaseFlush), then the type-specific part.
constexpr uint32_t kResourceHeader = 24;

}  // namespace

void OnSetIndices(uint32_t, uint32_t index_buffer) { g_bound.index_buffer = index_buffer; }

void OnSetStreamSource(uint32_t, uint32_t stream, uint32_t vb, uint32_t offset, uint32_t stride) {
  if (stream < kStreamCount) {
    g_bound.streams[stream][0] = vb;
    g_bound.streams[stream][1] = offset;
    g_bound.streams[stream][2] = stride;
  }
}

void OnInlineConstants(uint32_t bank, uint32_t start, uint32_t count, uint32_t data) {
  FlushInlineConstants();
  if (bank > 1 || !data) return;
  auto& c = g_inline_consts;
  c.pending_ptr = data;
  c.pending_bank = bank;
  c.pending_start = start;
  c.pending_count = count;
}

void OnSetConstants(uint32_t bank, uint32_t start, uint32_t count) {
  FlushInlineConstants();
  auto& c = g_inline_consts;
  if (!c.any || bank > 1) return;
  for (uint32_t i = start; i < start + count && i < 256; i++) c.valid[bank][i] = false;
}

void OnFlushConstants(uint32_t bank, uint64_t dirty_mask) {
  FlushInlineConstants();
  auto& c = g_inline_consts;
  if (!c.any || bank > 1) return;
  for (uint32_t group = 0; group < 64; group++)
    if (dirty_mask & (1ull << (63 - group)))
      for (uint32_t r = 4 * group; r < 4 * group + 4; r++) c.valid[bank][r] = false;
}

void OnSetVertexShader(uint32_t, uint32_t shader) { g_bound.vertex_shader = shader; }
void OnSetPixelShader(uint32_t, uint32_t shader) { g_bound.pixel_shader = shader; }

// Resource headers store the CPU virtual address; the 0xE0000000 range maps to physical memory
// with a 0x1000 offset, so masking off the top bits would point one page early.
static void FlushIndexedVertices();

static uint32_t GuestPhysical(uint32_t virtual_address) {
  uint32_t physical = REX_KERNEL_MEMORY()->GetPhysicalAddress(virtual_address);
  return physical != UINT32_MAX ? physical : virtual_address & 0x1FFFFFFF;
}

// Where the device writes its next PM4 packet: the ring, or a D3D command buffer being recorded.
static uint32_t WritePointer(uint32_t device) {
  uint32_t write = be32(Guest(device) + d3d::kDevCmdWrite);
  return write ? GuestPhysical(write) : 0;
}

// Diagnostic (tree orbs): parses the PM4 the device wrote since the previous draw and counts ALU
// constant writes by packet kind, following INDIRECT_BUFFERs one level. GTA's wind-animated tree
// leaves (VS AF15EF63) read _globalEntityScale from c232; in some frames the D3D shadow holds a
// stale value there for all of them, so the per-tree value must reach the GPU some other way.
namespace pm4scan {
constexpr uint64_t kLeafVs = 0xAF15EF63BB4B093Full;
constexpr uint32_t kWatchReg = 0x4000 + 232 * 4;  // VS c232.x
uint32_t g_pos = 0;
uint32_t g_watch[4] = {};
const char* g_watch_src = "none";
uint32_t g_watch_addr = 0;
struct Stats {
  uint32_t draws = 0, set_alu = 0, load_alu = 0, type0_alu = 0, set2 = 0, set_shader = 0, ibs = 0, resets = 0;
  uint32_t watch_set = 0, watch_load = 0, watch_type0 = 0, watch_other = 0;
  uint32_t checked = 0, vs_mismatch = 0, ps_mismatch = 0;
} g_stats;
// Shader identity check: the microcode the GPU loads (IM_LOAD / IM_LOAD_IMMEDIATE, hashed like the
// emulator keys it) against the shaders the capture used. A draw's own IM_LOADs and draw packet are
// written after the capture hook, so the first draw packet of the next scan belongs to it.
uint64_t g_loaded[2] = {};  // vertex, pixel microcode hash

// Guest physical memory read through process_vm_readv: unallocated guest pages may be protected,
// and a fault here (on the game's D3D thread) hangs the game. False if any of it is unreadable.
bool SafeCopy(uint32_t phys, uint32_t size, std::vector<uint8_t>& out) {
  out.resize(size);
  if (!size) return true;
  if (phys >= 0x20000000 || size > 0x20000000 - phys) return false;
#ifdef _WIN32
  std::memcpy(out.data(), REX_KERNEL_MEMORY()->TranslatePhysical(phys), size);
  return true;
#else
  return SafeReadMemory(out.data(), REX_KERNEL_MEMORY()->TranslatePhysical(phys), size);
#endif
}
struct Expected {
  bool valid = false;
  uint64_t ucode[2] = {}, container[2] = {};
} g_expected;

void CheckDraw() {
  if (!g_expected.valid) return;
  g_expected.valid = false;
  g_stats.checked++;
  static uint32_t logged = 0;
  for (int t = 0; t < 2; t++) {
    if (!g_expected.ucode[t] || !g_loaded[t] || g_expected.ucode[t] == g_loaded[t]) continue;
    (t ? g_stats.ps_mismatch : g_stats.vs_mismatch)++;
    if (logged < 80) {
      logged++;
      REXLOG_INFO("[native-pm4] {} mismatch: captured {:016X} (ucode {:016X}), GPU loads ucode {:016X} = {:016X}",
                  t ? "ps" : "vs", g_expected.container[t], g_expected.ucode[t], g_loaded[t],
                  ContainerForUcode(g_loaded[t]));
    }
  }
}

void Note(uint32_t reg, uint32_t n, const uint8_t* data, const char* src, uint32_t addr, uint32_t& counter) {
  if (reg + n <= kWatchReg || reg > kWatchReg + 3) return;
  counter++;
  for (uint32_t i = 0; i < 4; i++)
    if (kWatchReg + i >= reg && kWatchReg + i < reg + n) g_watch[i] = data ? be32(data + 4 * (kWatchReg + i - reg)) : 0;
  g_watch_src = src;
  g_watch_addr = addr;
}

void Parse(uint32_t begin, uint32_t end, int depth) {
  std::vector<uint8_t> segment, extra;
  if (!SafeCopy(begin, end - begin, segment)) {
    g_stats.resets++;
    return;
  }
  const uint8_t* p = segment.data();
  uint32_t size = end - begin;
  for (uint32_t at = 0; at + 4 <= size;) {
    uint32_t h = be32(p + at);
    uint32_t type = h >> 30;
    if (type == 2) {
      at += 4;
      continue;
    }
    if (type == 1) {
      at += 12;
      continue;
    }
    uint32_t count = ((h >> 16) & 0x3FFF) + 1;
    const uint8_t* body = p + at + 4;
    if (at + 4 + 4 * count > size) break;
    if (type == 0) {
      uint32_t reg = h & 0x7FFF;
      if (reg >= 0x4000 && reg < 0x4800) {
        g_stats.type0_alu++;
        Note(reg, (h & 0x8000) ? 1 : count, body, "type0", 0, g_stats.watch_type0);
      }
    } else {
      uint32_t op = (h >> 8) & 0x7F;
      if (op == 0x2D && count >= 1) {  // SET_CONSTANT
        uint32_t ot = be32(body);
        if (((ot >> 16) & 0xFF) == 0) {
          g_stats.set_alu++;
          Note(0x4000 + (ot & 0x7FF), count - 1, body + 4, "SET_CONSTANT", 0, g_stats.watch_set);
        }
      } else if (op == 0x2F && count >= 3) {  // LOAD_ALU_CONSTANT
        uint32_t addr = be32(body) & 0x1FFFFFFC, ot = be32(body + 4), n = be32(body + 8) & 0xFFF;
        if (((ot >> 16) & 0xFF) == 0 && SafeCopy(addr, 4 * n, extra)) {
          g_stats.load_alu++;
          Note(0x4000 + (ot & 0x7FF), n, extra.data(), "LOAD_ALU_CONSTANT", addr, g_stats.watch_load);
        }
      } else if (op == 0x55 || op == 0x56) {  // SET_CONSTANT2 / SET_SHADER_CONSTANTS (register index)
        (op == 0x55 ? g_stats.set2 : g_stats.set_shader)++;
        Note(be32(body) & 0xFFFF, count - 1, body + 4, op == 0x55 ? "SET_CONSTANT2" : "SET_SHADER_CONSTANTS", 0,
             g_stats.watch_other);
      } else if (op == 0x27 && count >= 2) {  // IM_LOAD: type | address, start << 16 | dwords
        uint32_t at_type = be32(body), dwords = be32(body + 4) & 0xFFFF, addr = at_type & 0x1FFFFFFC;
        if ((at_type & 3) < 2 && dwords && SafeCopy(addr, dwords * 4, extra))
          g_loaded[at_type & 3] = XXH3_64bits(extra.data(), dwords * 4);
      } else if (op == 0x2B && count >= 2) {  // IM_LOAD_IMMEDIATE: type, start << 16 | dwords, code
        uint32_t t = be32(body), dwords = be32(body + 4) & 0xFFFF;
        if (t < 2 && dwords && dwords <= count - 2) g_loaded[t] = XXH3_64bits(body + 8, dwords * 4);
      } else if (op == 0x22 || op == 0x36) {  // DRAW_INDX / DRAW_INDX_2
        CheckDraw();
      } else if (op == 0x3F && count == 2 && depth == 0) {  // INDIRECT_BUFFER
        g_stats.ibs++;
        uint32_t ib = be32(body) & 0x1FFFFFFC, dwords = be32(body + 4) & 0xFFFFF;
        if (dwords && dwords < 0x10000) Parse(ib, ib + dwords * 4, 1);
      }
    }
    at += 4 + 4 * count;
  }
}

void OnDraw(uint32_t write, uint64_t vs_hash, const uint8_t* captured_vs) {
  if (write > g_pos && write - g_pos < (1u << 20) && g_pos && write <= 0x20000000) Parse(g_pos, write, 0);
  else g_stats.resets++, g_expected.valid = false;
  g_pos = write;
  g_stats.draws++;
  g_expected.valid = true;
  g_expected.container[0] = ShaderHash(g_bound.vertex_shader);
  g_expected.container[1] = ShaderHash(g_bound.pixel_shader);
  g_expected.ucode[0] = ShaderUcodeHash(g_bound.vertex_shader);
  g_expected.ucode[1] = ShaderUcodeHash(g_bound.pixel_shader);
  static uint32_t logged = 0;
  if (vs_hash == kLeafVs && logged < 60) {
    logged++;
    uint32_t c[4];
    for (int i = 0; i < 4; i++) c[i] = be32(captured_vs + 232 * 16 + 4 * i);
    auto f = [](uint32_t v) { float x; std::memcpy(&x, &v, 4); return x; };
    REXLOG_INFO("[native-pm4] leaf draw: captured c232 ({:.3f},{:.3f},{:.3f},{:.3f}), last ring write "
                "({:.3f},{:.3f},{:.3f},{:.3f}) by {} {:08X}",
                f(c[0]), f(c[1]), f(c[2]), f(c[3]), f(g_watch[0]), f(g_watch[1]), f(g_watch[2]), f(g_watch[3]),
                g_watch_src, g_watch_addr);
  }
  if (g_stats.draws % 50000 == 0) {
    auto& s = g_stats;
    REXLOG_INFO("[native-pm4] per 50000 draws: ALU writes SET_CONSTANT {} LOAD_ALU_CONSTANT {} type0 {} "
                "SET_CONSTANT2 {} SET_SHADER_CONSTANTS {}, indirect buffers {}, scan resets {}; c232 written by "
                "SET_CONSTANT {} LOAD {} type0 {} other {}; shader check {} draws, vs mismatches {}, ps {}",
                s.set_alu, s.load_alu, s.type0_alu, s.set2, s.set_shader, s.ibs, s.resets, s.watch_set,
                s.watch_load, s.watch_type0, s.watch_other, s.checked, s.vs_mismatch, s.ps_mismatch);
    s = {};
  }
}
}  // namespace pm4scan

// Copies the index slice and the vertex range a draw reaches (all or nothing), through push(data,
// size) -> offset, into d.snap_*.
template <class Push>
static void SnapshotDrawData(DrawCommand& d, Push&& push) {
  auto* mem = REX_KERNEL_MEMORY();
  uint32_t first = d.start, last = d.start + d.count;  // vertices [first, last)
  if (d.indexed) {
    uint32_t isz = d.index32 ? 4 : 2;
    const uint8_t* ip = d.index_address ? mem->TranslatePhysical(d.index_address) : nullptr;
    first = ~0u, last = 0;
    if (ip && uint64_t(d.start + d.count) * isz <= d.index_size) {
      ip += size_t(d.start) * isz;
      d.snap_index = push(ip, d.count * isz);
      d.snap_index_size = d.count * isz;
      for (uint32_t i = 0; i < d.count; i++) {
        uint32_t v = isz == 4 ? be32(ip + 4 * i) : uint32_t(ip[2 * i] << 8 | ip[2 * i + 1]);
        if (isz == 4 || v != 0xFFFF) first = std::min(first, v), last = std::max(last, v + 1);
      }
      if (first < last) first += uint32_t(d.base_vertex), last += uint32_t(d.base_vertex);
    }
  }
  if (first < last && last - first <= (1u << 20)) {
    d.snap_first = first;
    d.snap_vertices = last - first;
    for (uint32_t s = 0; s < kMaxStreams; s++) {
      const auto& sb = d.streams[s];
      if (!sb.address || !sb.stride || sb.size <= sb.offset) continue;
      uint64_t lo = uint64_t(first) * sb.stride, hi = std::min<uint64_t>(uint64_t(last) * sb.stride, sb.size - sb.offset);
      if (lo >= hi) { d.snap_vertices = 0; break; }
      d.snap_stream[s] = push(mem->TranslatePhysical(sb.address + sb.offset) + lo, uint32_t(hi - lo));
      d.snap_vertices = std::min<uint32_t>(d.snap_vertices, uint32_t((hi - lo) / sb.stride));
    }
  }
  if (!d.snap_vertices) d.snap_index_size = 0;  // all or nothing
}

// --gtav_native_fence_copy: the renderer records a frame after its Swap, but the command processor
// writes the GPU fences that follow each draw as it goes (mid-frame), and from then on the game
// may reuse the draw's memory (streaming recycles it constantly while driving): a draw read after
// that rendered from new data for one frame (huge stray meshes over the sky). Every draw is
// queued with the fence that follows it; when the command processor reaches that fence before
// the frame is recorded, the draw's data is copied then; during the recording it waits instead.
// A copy at the D3D call itself is too early: some buffers are still being filled then.
struct PendingCopy {
  uint64_t frame;
  uint32_t draw, fence;
  bool indexed, index32;
  int32_t base_vertex;
  uint32_t start, count, index_address, index_size;
  StreamBinding streams[kMaxStreams];
};
struct FenceCopy {
  DrawCommand d;  // snap_* offsets into the frame's g_fence_arenas entry
};
// One arena per frame for its fence copies, handed to the packet whole (ApplyFenceCopies swaps it
// in): the copies were vectors of their own, moved into the packet's arena again on the render
// thread (3-5 MB a frame in the open world: ~13% of that thread). Spares keep their capacity.
static std::map<uint64_t, ByteArena> g_fence_arenas;
static std::vector<ByteArena> g_spare_fence_arenas;
static std::mutex g_copy_mutex;
static std::condition_variable g_copy_cv;
static std::deque<PendingCopy> g_pending_copies;
// Draws are queued here by the game's render thread under a lock of their own: g_copy_mutex is
// held by the command processor through its copies and waits (the game's D3D thread spent ~16% of
// its time blocked on it). Moved into g_pending_copies (DrainIncomingCopies) under g_copy_mutex.
static std::mutex g_incoming_mutex;
static std::vector<PendingCopy> g_incoming_copies, g_incoming_spare;
static void DrainIncomingCopies() {  // g_copy_mutex held
  {
    std::lock_guard lock(g_incoming_mutex);
    if (g_incoming_copies.empty()) return;
    std::swap(g_incoming_copies, g_incoming_spare);
  }
  for (const auto& p : g_incoming_spare) g_pending_copies.push_back(p);
  g_incoming_spare.clear();
}
static std::map<std::pair<uint64_t, uint32_t>, FenceCopy> g_fence_copies;  // (frame, draw)
static uint64_t g_copy_floor = 0;           // frames below this were recorded
static uint64_t g_recording = ~0ull;        // frame being recorded
static std::atomic<uint32_t> g_recorded_draws{0};  // of g_recording: draws that read their data
static std::atomic<uint32_t> g_fence_address{0};  // guest physical, where the GPU writes the fence
static constexpr uint32_t kDevFence = 11036;      // next fence value (InsertFence 8380E440)
static constexpr uint32_t kDevFenceDone = 11024;  // -> where the GPU writes the completed fence
static struct {
  uint64_t copies = 0, bytes = 0, waits = 0, wait_timeouts = 0;
  double wait_ms = 0;
} g_copy_stats;

static void FenceReached(uint32_t value) {
  std::unique_lock lock(g_copy_mutex);
  DrainIncomingCopies();
  while (!g_pending_copies.empty() && int32_t(value - g_pending_copies.front().fence) >= 0) {
    const PendingCopy& p = g_pending_copies.front();
    if (p.frame < g_copy_floor) {
      g_pending_copies.pop_front();
      continue;
    }
    // Being read right now (or --gtav_native_fence_wait: handed to the renderer, not read yet): the
    // GPU is not done before the renderer. Waiting also covers textures, which are never copied.
    // Never for the frame the game is still building: it may itself wait on this fence (loading
    // screens poll queries), and the renderer cannot record that frame before its Swap.
    // Being recorded: a draw reads its guest data (vertices, indices, textures) when it is recorded,
    // so only a draw the renderer has not reached yet must wait, and only until it is reached.
    // (Waiting for the whole frame stalled the GPU, and with it the game, for ~10 ms a time.)
    if (p.frame == g_recording && !REXCVAR_GET(gtav_native_fence_wait)) {
      if (g_recorded_draws.load(std::memory_order_acquire) > p.draw) {
        g_pending_copies.pop_front();
        continue;
      }
      uint64_t frame = p.frame;
      uint32_t draw = p.draw;
      auto start = std::chrono::steady_clock::now();
      g_copy_stats.waits++;
      bool reached = false;
      while (!(reached = g_recording != frame || g_recorded_draws.load(std::memory_order_acquire) > draw)) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(REXCVAR_GET(gtav_native_fence_wait_ms))) break;
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        lock.lock();
      }
      if (!reached) g_copy_stats.wait_timeouts++;
      g_copy_stats.wait_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      if (!reached) break;
      continue;  // re-evaluate the front: it may have been recorded and popped meanwhile
    }
    if (REXCVAR_GET(gtav_native_fence_wait) && REXCVAR_GET(gtav_native_fence_wait_draw) &&
        p.frame < g_frame.load(std::memory_order_relaxed)) {
      // Until this draw is recorded (it read its vertices, indices and textures then), not until
      // its whole frame is: the later draws' fences hold the game off their memory meanwhile.
      // (Waiting for the whole frame cost the game ~8 ms a frame in the open world.)
      uint64_t frame = p.frame;
      uint32_t draw = p.draw;
      auto start = std::chrono::steady_clock::now();
      g_copy_stats.waits++;
      auto recorded = [&] {
        return g_recording == frame && g_recorded_draws.load(std::memory_order_acquire) > draw;
      };
      bool reached = false;
      while (!(reached = g_copy_floor > frame || recorded())) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(REXCVAR_GET(gtav_native_fence_wait_ms))) break;
        if (g_recording == frame) {
          lock.unlock();
          std::this_thread::sleep_for(std::chrono::microseconds(50));
          lock.lock();
        } else {
          g_copy_cv.wait_for(lock, std::chrono::milliseconds(1));  // until its recording starts
        }
      }
      g_copy_stats.wait_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      if (!reached) {
        g_copy_stats.wait_timeouts++;
        if (frame != g_recording) goto copy;  // (the game waits on its own frame): copy
        break;
      }
      if (recorded()) g_pending_copies.pop_front();
      continue;  // a finished frame's entries are popped at the top
    }
    if (p.frame == g_recording ||
        (REXCVAR_GET(gtav_native_fence_wait) && p.frame < g_frame.load(std::memory_order_relaxed))) {
      uint64_t frame = p.frame;
      auto start = std::chrono::steady_clock::now();
      g_copy_stats.waits++;
      if (!g_copy_cv.wait_for(lock, std::chrono::milliseconds(REXCVAR_GET(gtav_native_fence_wait_ms)), [&] { return g_copy_floor > frame; }))
        g_copy_stats.wait_timeouts++;
      g_copy_stats.wait_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      if (g_copy_floor <= frame) {
        if (frame != g_recording) goto copy;  // timed out (the game waits on its own frame): copy
        break;
      }
      continue;
    }
  copy:
    {
      FenceCopy c;
      c.d.indexed = p.indexed, c.d.index32 = p.index32, c.d.base_vertex = p.base_vertex;
      c.d.start = p.start, c.d.count = p.count, c.d.index_address = p.index_address, c.d.index_size = p.index_size;
      std::copy(std::begin(p.streams), std::end(p.streams), c.d.streams);
      auto found = g_fence_arenas.find(p.frame);
      if (found == g_fence_arenas.end()) {
        found = g_fence_arenas.emplace(p.frame, ByteArena{}).first;
        if (!g_spare_fence_arenas.empty()) {
          found->second = std::move(g_spare_fence_arenas.back());
          g_spare_fence_arenas.pop_back();
        }
      }
      ByteArena& arena = found->second;
      const size_t before = arena.size();
      SnapshotDrawData(c.d, [&](const uint8_t* data, uint32_t size) {
        uint32_t at = uint32_t(arena.size());
        std::memcpy(arena.Grow(size), data, size);
        return at;
      });
      if (c.d.snap_vertices) {
        g_copy_stats.copies++;
        g_copy_stats.bytes += arena.size() - before;
        g_fence_copies[{p.frame, p.draw}] = std::move(c);
      } else {
        arena.Shrink(arena.size() - before);
      }
    }
    g_pending_copies.pop_front();
  }
}

void ApplyFenceCopies(FramePacket& f) {
  std::lock_guard lock(g_copy_mutex);
  g_recording = f.index;
  g_recorded_draws.store(0, std::memory_order_release);
  // This frame's arena becomes the packet's; the packet's previous one (and those of frames that
  // were never recorded) go back to the spares.
  f.fence_arena.clear();
  for (auto it = g_fence_arenas.begin(); it != g_fence_arenas.end() && it->first <= f.index;
       it = g_fence_arenas.erase(it)) {
    if (it->first == f.index) std::swap(f.fence_arena, it->second);
    it->second.clear();
    if (g_spare_fence_arenas.size() < 4) g_spare_fence_arenas.push_back(std::move(it->second));
  }
  constexpr uint32_t kFenceBit = FramePacket::kFenceBit;
  for (auto it = g_fence_copies.begin(); it != g_fence_copies.end() && it->first.first <= f.index;
       it = g_fence_copies.erase(it)) {
    if (it->first.first != f.index || it->first.second >= f.draws.size()) continue;
    DrawCommand& d = f.draws[it->first.second];
    const FenceCopy& c = it->second;
    if (d.snap_vertices) continue;
    d.snap_first = c.d.snap_first;
    d.snap_vertices = c.d.snap_vertices;
    for (uint32_t s = 0; s < kMaxStreams; s++) d.snap_stream[s] = c.d.snap_stream[s] | kFenceBit;
    d.snap_index = c.d.snap_index | kFenceBit;
    d.snap_index_size = c.d.snap_index_size;
  }
}

void DrawsRecorded(uint32_t count) { g_recorded_draws.store(count, std::memory_order_release); }

void FenceCopiesRecorded(uint64_t frame) {
  {
    std::lock_guard lock(g_copy_mutex);
    g_recording = ~0ull;
    g_copy_floor = frame + 1;
    DrainIncomingCopies();
    while (!g_pending_copies.empty() && g_pending_copies.front().frame <= frame) g_pending_copies.pop_front();
#if defined(__APPLE__)
    if (frame % 120 == 0) {
      // phys_footprint is what jetsam counts; os_proc_available_memory is the headroom left (iOS).
      task_vm_info_data_t vm{};
      mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
      if (task_info(mach_task_self(), TASK_VM_INFO, task_info_t(&vm), &n) == KERN_SUCCESS) {
#if TARGET_OS_IPHONE
        malloc_statistics_t ms{};
        malloc_zone_statistics(nullptr, &ms);
        // Thermal state (NSProcessInfo: 0 nominal .. 3 critical): a hot phone throttles the GPU 2x+.
        static const char* kThermal[] = {"nominal", "fair", "serious", "critical"};
        REXLOG_INFO("[native] memory at frame {}: footprint {} MB, available {} MB, compressed {} MB; Metal {} MB, "
                    "malloc {} MB; thermal {}",
                    frame, vm.phys_footprint >> 20, os_proc_available_memory() >> 20, vm.compressed >> 20,
                    MetalAllocatedBytes() >> 20, ms.size_in_use >> 20,
                    kThermal[std::clamp(gtav_ios_thermal_state(), 0, 3)]);
#else
        REXLOG_INFO("[native] memory at frame {}: footprint {} MB, compressed {} MB", frame, vm.phys_footprint >> 20,
                    vm.compressed >> 20);
#endif
      }
    }
#endif
    if (frame % 300 == 0) {
      auto& st = g_copy_stats;
      REXLOG_INFO("[native] fence copies per 300 frames: {} draws ({} KB), GPU waited for the renderer {} times "
                  "({:.1f} ms, {} timeouts), {} queued; packet arenas grew {} times ({} MB moved)",
                  st.copies, st.bytes / 1024, st.waits, st.wait_ms, st.wait_timeouts, g_pending_copies.size(),
                  g_arena_grows.exchange(0), g_arena_grow_bytes.exchange(0) >> 20);
      st = {};
    }
  }
  g_copy_cv.notify_all();
}

// Constant bytes per kind (fetch, vs, ps): {compared, copied}, logged every 300 frames.
static uint64_t g_copy_bytes[3][2];

static DrawCommand* RecordDraw(uint32_t device, bool indexed, uint32_t primitive, int32_t base_vertex,
                               uint32_t start, uint32_t count) {
  FlushIndexedVertices();
  if (!Active()) return nullptr;
  if (REXCVAR_GET(gtav_native_pm4_scan)) {
    FlushInlineConstants();
    uint8_t vs[256 * 16];
    std::memcpy(vs, Guest(device) + d3d::kDevVsConstants, sizeof(vs));
    for (uint32_t i = 0; i < 256; i++)
      if (g_inline_consts.valid[0][i]) std::memcpy(vs + 16 * i, g_inline_consts.data[0][i], 16);
    pm4scan::OnDraw(WritePointer(device), ShaderHash(g_bound.vertex_shader), vs);
  }
  const uint8_t* dev = Guest(device);
  DrawCommand d;
  d.indexed = indexed;
  d.primitive = primitive;
  d.base_vertex = base_vertex;
  d.start = start;
  d.count = count;
  // The shaders the device holds, as the draw reads them: the Set*Shader hooks are not the only
  // writers. Outdoors 28 scenery draws a frame (5106-index meshes) bound their pixel shader another
  // way, so the hooked value was a depth pass's null shader and they came out depth-only.
  uint32_t vertex_shader = be32(dev + d3d::kDevVertexShader), pixel_shader = be32(dev + d3d::kDevPixelShader);
  if (!vertex_shader) vertex_shader = g_bound.vertex_shader;
  d.vs_hash = ShaderHash(vertex_shader);
  d.ps_hash = ShaderHash(pixel_shader);
  d.ps_ucode_hash = ShaderUcodeHash(pixel_shader);
  d.regs = g_packet.Push(dev + kRegShadowBegin, kRegShadowSize);
  {
    // RB_MODECONTROL in the device shadow lags: D3D derives it while flushing the draw, after this
    // snapshot. GTA's light volumes follow a depth-only stencil pass and still read 5 (depth only)
    // although they write color (mask 0007, a lighting pixel shader, additive blend), so the
    // lights were dropped. Depth-only draws always have mask 0 and no pixel shader.
    uint8_t* shadow = g_packet.arena.data() + d.regs;
    if ((xenos_regs::Read(shadow, xenos_regs::RB_MODECONTROL) & 7) == 5 && d.ps_hash &&
        (xenos_regs::Read(shadow, xenos_regs::RB_COLOR_MASK) & 0xFFFF) != 0) {
      uint32_t mode = xenos_regs::Read(shadow, xenos_regs::RB_MODECONTROL);
      xenos_regs::Write(shadow, xenos_regs::RB_MODECONTROL, (mode & ~7u) | 4);
    }
  }
  // Consecutive draws mostly repeat the previous draw's constants and fetch constants: share its
  // block instead (the replay then skips the byte swap and texture lookups by offset). Compared
  // before copying: pushing 10 KB a draw and taking it back cost ~8% of the game thread.
  const DrawCommand* prev = g_packet.draws.empty() ? nullptr : &g_packet.draws.back();
  // Blocks keep their full size (the renderer reads them by offset), but only the registers the
  // shaders read are copied and compared (the rest of a block is undefined).
  auto push_or_share = [](const uint8_t* src, uint32_t size, const uint32_t* previous, int kind) {
    g_copy_bytes[kind][0] += size;
    if (previous && std::memcmp(src, g_packet.At(*previous), size) == 0) return *previous;
    g_copy_bytes[kind][1] += size;
    return g_packet.Push(src, size);
  };
  d.fetch = push_or_share(dev + d3d::kDevFetchConstants, kFetchSize, prev ? &prev->fetch : nullptr, 0);
  FlushInlineConstants();
  // Constants the D3D shadow does not hold yet (written by inline PM4 packets) are patched in.
  auto push_constants = [&](uint32_t bank, const uint8_t* shadow, const ConstantUse& use, const uint32_t* previous) {
    const uint8_t* src = shadow;
    alignas(16) uint8_t patched[kAluSize];
    if (g_inline_consts.any) {
      for (const auto& run : use.runs) std::memcpy(patched + 16 * run.first, shadow + 16 * run.first, 16 * run.count);
      for (const auto& run : use.runs)
        for (uint32_t i = run.first; i < uint32_t(run.first + run.count); i++)
          if (g_inline_consts.valid[bank][i]) std::memcpy(patched + 16 * i, g_inline_consts.data[bank][i], 16);
      src = patched;
    }
    uint32_t bytes = 0;
    for (const auto& run : use.runs) bytes += 16 * run.count;
    g_copy_bytes[1 + bank][0] += bytes;
    if (previous) {
      const uint8_t* old = g_packet.At(*previous);
      bool same = true;
      for (const auto& run : use.runs)
        if (std::memcmp(src + 16 * run.first, old + 16 * run.first, 16 * run.count) != 0) {
          same = false;
          break;
        }
      if (same) return *previous;
    }
    g_copy_bytes[1 + bank][1] += bytes;
    uint32_t at = uint32_t(g_packet.arena.size());
    uint8_t* dst = g_packet.arena.Grow(kAluSize);
    for (const auto& run : use.runs) std::memcpy(dst + 16 * run.first, src + 16 * run.first, 16 * run.count);
    return at;
  };
  const ConstantUse& vs_use = ShaderConstants(vertex_shader);
  static const ConstantUse kNoConstants{0, {}};
  const ConstantUse& ps_use = pixel_shader ? ShaderConstants(pixel_shader) : kNoConstants;
  d.vs_const_count = uint16_t(vs_use.count);
  d.ps_const_count = uint16_t(ps_use.count);
  d.vs_use = &vs_use;
  d.ps_use = &ps_use;
  d.vs_consts = push_constants(0, dev + d3d::kDevVsConstants, vs_use, prev ? &prev->vs_consts : nullptr);
  d.ps_consts = push_constants(1, dev + d3d::kDevPsConstants, ps_use, prev ? &prev->ps_consts : nullptr);
  // --gtav_native_dump_on_collapse: wind-animated tree leaf draws (VS AF15EF63) whose
  // _globalEntityScale (c232) has z = 0 collapse to a point (black tree "orbs"). OnCaptureSwap
  // requests a frame dump once that lasts (to compare with the emulator in shadow mode).
  if (REXCVAR_GET(gtav_native_dump_on_collapse) && d.vs_hash == 0xAF15EF63BB4B093Full)
    (be32(g_packet.At(d.vs_consts) + 232 * 16 + 8) == 0 ? g_collapse.frame_bad : g_collapse.frame_good) = true;
  // The device's own field, as DrawIndexedVertices reads it: SetIndices (83806E70) is not the
  // only writer, so the hooked value can be stale.
  uint32_t index_buffer = be32(dev + d3d::kDevIndexBuffer);
  if (!index_buffer) index_buffer = g_bound.index_buffer;
  if (indexed && index_buffer) {
    const uint8_t* ib = Guest(index_buffer);
    d.index32 = (be32(ib) & d3d::kIndexBuffer32Flag) != 0;
    d.index_address = GuestPhysical(be32(ib + kResourceHeader));
    d.index_size = be32(ib + kResourceHeader + 4);
  }
  if (uint32_t decl = be32(dev + d3d::kDevVertexDeclaration)) {
    const uint8_t* p = Guest(decl);
    uint32_t n = std::min(be32(p + d3d::kDeclCount), kMaxVertexElements);
    for (uint32_t i = 0; i < n; i++) {
      const uint8_t* e = p + d3d::kDeclElements + 12 * i;
      d.elements[i] = {be16(e), be16(e + 2), be32(e + 4), e[8], e[9], e[10], 0};
    }
    d.element_count = n;
  }
  for (uint32_t s = 0; s < kStreamCount; s++) {
    if (uint32_t vb = g_bound.streams[s][0]) {
      const uint8_t* v = Guest(vb);
      // Vertex buffer: its fetch constant (address | type 3, endian | size in dwords).
      d.streams[s].address = GuestPhysical(be32(v + kResourceHeader) & 0xFFFFFFFC);
      // Fetch constant dword 1: endian in bits 0-1, size in dwords in bits 2-25.
      uint32_t word1 = be32(v + kResourceHeader + 4);
      d.streams[s].size = ((word1 >> 2) & 0x00FFFFFF) * 4;
      d.streams[s].endian = word1 & 3;
      d.streams[s].offset = g_bound.streams[s][1];
      d.streams[s].stride = g_bound.streams[s][2];
    }
  }
  for (int i = 0; i < 4; i++) d.render_targets[i] = be32(dev + d3d::kDevRenderTargets + 4 * i);
  d.depth_stencil = be32(dev + d3d::kDevDepthStencil);
  if (REXCVAR_GET(gtav_native_check_draw_data))
    d.data_hash = DrawDataHash(d, [](uint32_t a) { return REX_KERNEL_MEMORY()->TranslatePhysical(a); });
  if (REXCVAR_GET(gtav_native_snapshot_draws) && !d.is_inline) {
    SnapshotDrawData(d, [](const uint8_t* data, uint32_t size) { return g_packet.Push(data, size); });
  } else if (!d.is_inline && REXCVAR_GET(gtav_native_fence_copy)) {
    if (uint32_t done = be32(dev + kDevFenceDone)) g_fence_address = GuestPhysical(done);
    PendingCopy p{GuestFrame(), uint32_t(g_packet.draws.size()), be32(dev + kDevFence), d.indexed, d.index32,
                  d.base_vertex, d.start, d.count, d.index_address, d.index_size, {}};
    std::copy(std::begin(d.streams), std::end(d.streams), p.streams);
    std::lock_guard lock(g_incoming_mutex);
    g_incoming_copies.push_back(p);
  }
  g_packet.commands.push_back({CommandKind::kDraw, uint32_t(g_packet.draws.size()), WritePointer(device)});
  g_packet.draws.push_back(d);
  return &g_packet.draws.back();
}

// BeginVertices(dev, primitive, vertex count, stride) returns where the CPU writes the vertices
// (inside the command buffer); EndVertices(dev) closes the draw, so the data is copied there.
static int64_t g_inline_draw = -1;
static uint32_t g_inline_ptr = 0, g_inline_bytes = 0;

void OnBeginVertices(uint32_t device, uint32_t primitive, uint32_t count, uint32_t stride, uint32_t data) {
  g_inline_draw = -1;
  DrawCommand* d = RecordDraw(device, false, primitive, 0, 0, count);
  if (!d || !data) return;
  d->is_inline = true;
  d->streams[0] = {0, count * stride, 0, stride, 2};
  g_inline_draw = int64_t(g_packet.draws.size() - 1);
  g_inline_ptr = data;
  g_inline_bytes = count * stride;
}

void OnEndVertices() {
  if (g_inline_draw < 0 || size_t(g_inline_draw) >= g_packet.draws.size()) return;
  uint32_t at = g_packet.Push(Guest(g_inline_ptr), g_inline_bytes);
  auto& d = g_packet.draws[size_t(g_inline_draw)];
  d.inline_vertices = at;
  d.inline_size = g_inline_bytes;
  g_inline_draw = -1;
}

// BeginIndexedVertices(dev, primitive, base vertex, vertex count, index count, index format,
// stride, &indices, &vertices) hands the CPU two buffers in GPU ring memory and needs no End call:
// the data is complete by the next D3D call, so it is copied then (FlushIndexedVertices).
static int64_t g_up_draw = -1;
static uint32_t g_up_vertices = 0, g_up_vertex_bytes = 0, g_up_indices = 0, g_up_index_bytes = 0;

static void FlushIndexedVertices() {
  if (g_up_draw < 0) return;
  int64_t draw = g_up_draw;
  g_up_draw = -1;
  if (size_t(draw) >= g_packet.draws.size()) return;
  uint32_t vertices = g_packet.Push(Guest(g_up_vertices), g_up_vertex_bytes);
  uint32_t indices = g_packet.Push(Guest(g_up_indices), g_up_index_bytes);
  auto& d = g_packet.draws[size_t(draw)];
  d.inline_vertices = vertices;
  d.inline_size = g_up_vertex_bytes;
  d.inline_indices = indices;
  d.inline_index_size = g_up_index_bytes;
}

void OnBeginIndexedVertices(uint32_t device, uint32_t primitive, int32_t base_vertex, uint32_t vertex_count,
                            uint32_t index_count, uint32_t index_format, uint32_t stride) {
  FlushIndexedVertices();
  const uint8_t* dev = Guest(device);
  uint32_t vertices = be32(dev + d3d::kDevUpVertices), indices = be32(dev + d3d::kDevUpIndices);
  DrawCommand* d = RecordDraw(device, true, primitive, base_vertex, 0, index_count);
  if (!d || !vertices || !indices) return;
  d->is_inline = true;
  d->index32 = (index_format & 4) != 0;
  d->index_address = 0;
  d->index_size = 0;
  d->streams[0] = {0, vertex_count * stride, 0, stride, 2};
  g_up_draw = int64_t(g_packet.draws.size() - 1);
  g_up_vertices = vertices;
  g_up_vertex_bytes = vertex_count * stride;
  g_up_indices = indices;
  g_up_index_bytes = index_count * (d->index32 ? 4 : 2);
}

void OnDrawIndexed(uint8_t*, uint32_t device, uint32_t primitive, uint32_t base_vertex, uint32_t start,
                   uint32_t count) {
  if (Probing()) ProbeDraw("drawidx", device);
  RecordDraw(device, true, primitive, int32_t(base_vertex), start, count);
}

void OnDraw(uint8_t*, uint32_t device, uint32_t primitive, uint32_t start, uint32_t count) {
  if (Probing()) ProbeDraw("draw", device);
  RecordDraw(device, false, primitive, 0, start, count);
}

// Diagnostic: which shadowed registers the D3D runtime changes inside its draw calls (state it
// flushes lazily there; a snapshot taken before the call misses it).
std::vector<uint8_t> g_probe_pre;
std::unordered_map<uint32_t, uint32_t> g_probe_changes;
uint32_t g_probe_draws = 0, g_probe_reports = 0;

void ProbeDrawBefore(uint32_t device) {
  if (g_probe_reports >= 6) return;
  const uint8_t* shadow = Guest(device) + kRegShadowBegin;
  g_probe_pre.assign(shadow, shadow + kRegShadowSize);
}

void ProbeDrawAfter(uint32_t device) {
  if (g_probe_reports >= 6 || g_probe_pre.size() != kRegShadowSize) return;
  const uint8_t* shadow = Guest(device) + kRegShadowBegin;
  for (const auto& b : xenos_regs::kBlocks)
    for (uint32_t i = 0; i < b.count; i++) {
      uint32_t off = b.device_offset - kRegShadowBegin + 4 * i;
      if (off + 4 <= kRegShadowSize && std::memcmp(shadow + off, g_probe_pre.data() + off, 4) != 0)
        g_probe_changes[b.first_reg + i]++;
    }
  if (++g_probe_draws % 2000 == 0) {
    g_probe_reports++;
    std::vector<std::pair<uint32_t, uint32_t>> v(g_probe_changes.begin(), g_probe_changes.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
    std::string line;
    for (size_t k = 0; k < std::min<size_t>(v.size(), 16); k++) line += fmt::format(" {:04X}x{}", v[k].first, v[k].second);
    REXLOG_INFO("[native] registers changed inside D3D draw calls (per 2000 draws):{}", line);
    g_probe_changes.clear();
  }
}

uint32_t g_clear_surface_before = 0, g_clear_depth_before = 0, g_clear_logged = 0;

void OnClearBegin(uint32_t device) {
  FlushIndexedVertices();
  const uint8_t* shadow = Guest(device) + kRegShadowBegin;
  g_clear_surface_before = xenos_regs::Read(shadow, xenos_regs::RB_SURFACE_INFO);
  g_clear_depth_before = xenos_regs::Read(shadow, xenos_regs::RB_DEPTH_INFO);
}

void OnClear(uint32_t device, uint32_t flags, uint32_t color, float z, uint32_t stencil, uint32_t rect_count,
             uint32_t rects) {
  if (!Active()) return;
  {
    const uint8_t* shadow = Guest(device) + kRegShadowBegin;
    uint32_t surface = xenos_regs::Read(shadow, xenos_regs::RB_SURFACE_INFO);
    uint32_t depth = xenos_regs::Read(shadow, xenos_regs::RB_DEPTH_INFO);
    if ((surface != g_clear_surface_before || depth != g_clear_depth_before) && g_clear_logged < 40) {
      g_clear_logged++;
      REXLOG_INFO("[native] clear flags {:X}: surface info {:08X} -> {:08X}, depth info {:08X} -> {:08X} across D3D Clear",
                  flags, g_clear_surface_before, surface, g_clear_depth_before, depth);
    }
  }
  ClearCommand c{flags, color, stencil, z, g_packet.Push(Guest(device) + kRegShadowBegin, kRegShadowSize)};
  if (rect_count && rects) {
    // More rects than fit: clear their bounding box.
    const uint8_t* p = Guest(rects);
    c.rect_count = std::min(rect_count, ClearCommand::kMaxRects);
    for (uint32_t i = 0; i < rect_count; i++) {
      uint32_t slot = std::min(i, ClearCommand::kMaxRects - 1);
      for (int k = 0; k < 4; k++) {
        int32_t v = int32_t(be32(p + 16 * i + 4 * k));
        c.rects[slot][k] = i < ClearCommand::kMaxRects ? v
                           : k < 2 ? std::min(c.rects[slot][k], v) : std::max(c.rects[slot][k], v);
      }
    }
  }
  g_packet.commands.push_back({CommandKind::kClear, uint32_t(g_packet.clears.size()), WritePointer(device)});
  g_packet.clears.push_back(c);
}

void OnResolve(uint32_t device, uint32_t flags, uint32_t rect, uint32_t dest_texture, uint32_t dest_point,
               uint32_t clear_color, float clear_z, uint32_t level, uint32_t face) {
  FlushIndexedVertices();
  if (!Active()) return;
  ResolveCommand r;
  r.flags = flags;
  r.dest_texture = dest_texture;
  r.dest_level = level;
  r.dest_face = face;
  r.regs = g_packet.Push(Guest(device) + kRegShadowBegin, kRegShadowSize);
  if (rect) {
    const uint8_t* p = Guest(rect);
    for (int i = 0; i < 4; i++) r.source_rect[i] = be32(p + 4 * i);
    r.has_rect = true;
  }
  r.clear_z = clear_z;
  if (clear_color) {
    const uint8_t* p = Guest(clear_color);
    for (int i = 0; i < 4; i++) {
      uint32_t v = be32(p + 4 * i);
      std::memcpy(&r.clear_color[i], &v, 4);
    }
  }
  if (dest_point) {
    const uint8_t* p = Guest(dest_point);
    r.dest_point[0] = be32(p);
    r.dest_point[1] = be32(p + 4);
    r.has_point = true;
  }
  if (dest_texture) {
    const uint8_t* t = Guest(dest_texture);
    for (int i = 0; i < 13; i++) r.dest_words[i] = be32(t + 4 * i);
    // The texture object stores a virtual base address; the fetch constants the GPU samples with
    // carry the physical one (the 0xE0000000 range maps with a 0x1000 offset). Use the physical
    // address so resolves and later texture fetches agree.
    uint32_t& dword1 = r.dest_words[8];
    uint32_t physical = REX_KERNEL_MEMORY()->GetPhysicalAddress(dword1 & 0xFFFFF000);
    if (physical != UINT32_MAX) dword1 = (dword1 & 0xFFF) | (physical & 0xFFFFF000);
  }
  g_packet.commands.push_back({CommandKind::kResolve, uint32_t(g_packet.resolves.size()), WritePointer(device)});
  g_packet.resolves.push_back(r);
}

// D3D occlusion queries. With --gtav_native=on the command processor only fakes them (every query
// passes 1000 samples, so lens flares shine through walls); the native renderer counts the samples
// for real and GetData returns that count instead (OverrideQueryResult).
namespace {

struct QueryState {
  uint32_t issued = 0, done = 0;  // serial of the last END, and of the last published result
  uint32_t samples = 0;
  uint64_t issue_frame = 0;
};
std::mutex g_query_mutex;
std::unordered_map<uint32_t, QueryState> g_queries;
struct QueryStats {
  uint32_t issued = 0, native = 0, stale = 0, pending = 0, fallback = 0;
} g_query_stats;

}  // namespace

void OnQueryIssue(uint32_t query, uint32_t flags) {
  FlushIndexedVertices();
  if (!Active() || !query) return;
  const uint8_t* q = Guest(query);
  constexpr uint32_t kOcclusion = 9;  // D3DQUERYTYPE_OCCLUSION
  if (be32(q + 4) != kOcclusion) return;
  uint32_t device = be32(q);
  QueryCommand c{query, 0, (flags & 2) != 0};  // D3DISSUE_BEGIN = 2, D3DISSUE_END = 1
  if (!c.begin) {
    if (!(flags & 1)) return;
    std::lock_guard lock(g_query_mutex);
    QueryState& st = g_queries[query];
    c.serial = ++st.issued;
    st.issue_frame = g_frame.load(std::memory_order_relaxed);
    g_query_stats.issued++;
  }
  g_packet.commands.push_back({CommandKind::kQuery, uint32_t(g_packet.queries.size()), WritePointer(device)});
  g_packet.queries.push_back(c);
}

void PublishQueryResult(uint32_t query, uint32_t serial, uint64_t samples) {
  std::lock_guard lock(g_query_mutex);
  auto it = g_queries.find(query);
  if (it == g_queries.end() || serial < it->second.done) return;
  it->second.done = serial;
  it->second.samples = uint32_t(std::min<uint64_t>(samples, UINT32_MAX));
}

QueryOverride OverrideQueryResult(uint32_t query, uint32_t* samples) {
  std::lock_guard lock(g_query_mutex);
  auto it = g_queries.find(query);
  if (it == g_queries.end() || !it->second.issued) return QueryOverride::kOriginal;
  QueryState& st = it->second;
  if (st.done == st.issued) {
    *samples = st.samples;
    g_query_stats.native++;
    return QueryOverride::kReady;
  }
  // GTA re-issues its corona queries every few frames, sooner than the native renderer (two to
  // three frames behind, results after the GPU fence) answers them: waiting for the latest left
  // ~96% of polls "not ready" and flares froze. The newest finished result is a few frames old.
  if (st.done) {
    *samples = st.samples;
    g_query_stats.stale++;
    return QueryOverride::kReady;
  }
  // The native frame holding it may never render (dropped, or the END sat in an unrun command
  // buffer): after a few frames the command processor's answer stands.
  constexpr uint64_t kPatienceFrames = 8;
  if (g_frame.load(std::memory_order_relaxed) - st.issue_frame > kPatienceFrames) {
    g_query_stats.fallback++;
    return QueryOverride::kOriginal;
  }
  g_query_stats.pending++;
  return QueryOverride::kNotReady;
}

// D3D command buffers. GTA records some draws (outdoor scenery) into command buffers and runs
// them later, possibly every frame: the draw hooks fire while recording (their PM4 goes into the
// buffer), nothing fires when the GPU executes them. Commands captured with a write pointer inside
// a buffer that 838087C8 runs are moved out of the frame into that buffer's recording and appended
// to the frame at every run, like the GPU executes them.
namespace {

struct RecordedBuffer {
  uint32_t begin = 0, end = 0;  // guest physical PM4 range
  FramePacket packet;
};
std::vector<RecordedBuffer> g_buffers;
struct BufferStats {
  uint32_t runs = 0, replayed = 0, recorded = 0, empty = 0, frames = 0;
} g_buffer_stats;

void CopyCommand(FramePacket& dst, const FramePacket& src, const Command& c, uint32_t pm4) {
  auto blob = [&](uint32_t at, uint32_t size) { return size ? dst.Push(src.At(at), size) : 0u; };
  switch (c.kind) {
    case CommandKind::kDraw: {
      DrawCommand d = src.draws[c.index];
      d.regs = blob(d.regs, kRegShadowSize);
      d.fetch = blob(d.fetch, kFetchSize);
      d.vs_consts = blob(d.vs_consts, kAluSize);
      d.ps_consts = blob(d.ps_consts, kAluSize);
      if (d.inline_size) d.inline_vertices = blob(d.inline_vertices, d.inline_size);
      if (d.inline_index_size) d.inline_indices = blob(d.inline_indices, d.inline_index_size);
      dst.commands.push_back({c.kind, uint32_t(dst.draws.size()), pm4});
      dst.draws.push_back(d);
      break;
    }
    case CommandKind::kClear: {
      ClearCommand k = src.clears[c.index];
      k.regs = blob(k.regs, kRegShadowSize);
      dst.commands.push_back({c.kind, uint32_t(dst.clears.size()), pm4});
      dst.clears.push_back(k);
      break;
    }
    case CommandKind::kQuery: {
      dst.commands.push_back({c.kind, uint32_t(dst.queries.size()), pm4});
      dst.queries.push_back(src.queries[c.index]);
      break;
    }
    case CommandKind::kResolve: {
      ResolveCommand k = src.resolves[c.index];
      k.regs = blob(k.regs, kRegShadowSize);
      dst.commands.push_back({c.kind, uint32_t(dst.resolves.size()), pm4});
      dst.resolves.push_back(k);
      break;
    }
  }
}

// Moves the frame's commands written into [begin, end) into `out` (replacing what it held).
// Returns how many moved; the per-kind vectors keep the moved entries, only commands is rebuilt.
uint32_t TakeRecorded(uint32_t begin, uint32_t end, FramePacket& out) {
  uint32_t moved = 0;
  for (const auto& c : g_packet.commands)
    if (c.pm4 >= begin && c.pm4 < end) moved++;
  if (!moved) return 0;
  out.Clear();
  std::vector<Command> keep;
  keep.reserve(g_packet.commands.size() - moved);
  for (const auto& c : g_packet.commands) {
    if (c.pm4 >= begin && c.pm4 < end) CopyCommand(out, g_packet, c, c.pm4);
    else keep.push_back(c);
  }
  g_packet.commands.swap(keep);
  return moved;
}

RecordedBuffer& BufferFor(uint32_t begin, uint32_t end) {
  for (auto& b : g_buffers)
    if (b.begin == begin) {
      b.end = std::max(b.end, end);
      return b;
    }
  if (g_buffers.size() < 24)
    REXLOG_INFO("[native] command buffer {:08X}-{:08X} ({} bytes) runs for the first time", begin, end, end - begin);
  g_buffers.push_back({begin, end, {}});
  return g_buffers.back();
}

}  // namespace

void OnCommandBufferRun(uint32_t device, uint32_t write_before, uint32_t write_after) {
  FlushIndexedVertices();
  if (!Active() || !write_before || write_after <= write_before || write_after - write_before > 4096) return;
  const uint8_t* p = Guest(write_before);
  for (uint32_t at = 0; at + 12 <= write_after - write_before; at += 4) {
    if (be32(p + at) != 0xC0013F00) continue;  // PM4 type 3 INDIRECT_BUFFER, 2 dwords
    uint32_t begin = be32(p + at + 4), dwords = be32(p + at + 8) & 0xFFFFF;
    uint32_t end = begin + dwords * 4;
    g_buffer_stats.runs++;
    RecordedBuffer& b = BufferFor(begin, end);
    g_buffer_stats.recorded += TakeRecorded(begin, end, b.packet);
    if (b.packet.commands.empty()) g_buffer_stats.empty++;
    for (const auto& c : b.packet.commands) CopyCommand(g_packet, b.packet, c, 0);
    g_buffer_stats.replayed += uint32_t(b.packet.commands.size());
    at += 8;
  }
}

void OnCaptureSwap(uint32_t front_buffer) {
  linux_threads::Note(linux_threads::kD3D);
  FlushIndexedVertices();
  // Commands recorded this frame into known buffers that did not run yet: they run later.
  for (auto& b : g_buffers) {
    FramePacket fresh;
    if (TakeRecorded(b.begin, b.end, fresh)) {
      g_buffer_stats.recorded += uint32_t(fresh.commands.size());
      b.packet = std::move(fresh);
    }
  }
  if (g_buffer_stats.frames % 300 == 299 && g_query_stats.issued) {
    std::lock_guard lock(g_query_mutex);
    REXLOG_INFO("[native] occlusion queries per 300 frames: {} issued, results {} native, {} a few frames old, "
                "{} not ready yet, {} fell back to the command processor ({} query objects)",
                g_query_stats.issued, g_query_stats.native, g_query_stats.stale, g_query_stats.pending,
                g_query_stats.fallback, g_queries.size());
    g_query_stats = {};
  }
  if (g_buffer_stats.frames % 300 == 299) {
    auto kb = [](uint64_t b) { return b / 300 / 1024; };
    REXLOG_INFO("[native] capture constants per frame (KB compared / copied): fetch {}/{} vs {}/{} ps {}/{}",
                kb(g_copy_bytes[0][0]), kb(g_copy_bytes[0][1]), kb(g_copy_bytes[1][0]), kb(g_copy_bytes[1][1]),
                kb(g_copy_bytes[2][0]), kb(g_copy_bytes[2][1]));
    std::memset(g_copy_bytes, 0, sizeof(g_copy_bytes));
  }
  if (++g_buffer_stats.frames % 300 == 0 && g_buffer_stats.runs) {
    REXLOG_INFO("[native] command buffers: {} known, per 300 frames {} runs ({} with nothing recorded), "
                "{} commands replayed, {} recorded",
                g_buffers.size(), g_buffer_stats.runs, g_buffer_stats.empty, g_buffer_stats.replayed,
                g_buffer_stats.recorded);
    g_buffer_stats = {0, 0, 0, 0, g_buffer_stats.frames};
  }
  uint64_t frame = g_frame.fetch_add(1, std::memory_order_relaxed);
  if (g_collapse.frame_bad || g_collapse.frame_good) {
    g_collapse.streak = g_collapse.frame_bad && !g_collapse.frame_good ? g_collapse.streak + 1 : 0;
    if (g_collapse.streak == 120 && !g_collapse.requested) {
      g_collapse.requested = true;
      REXLOG_INFO("[native] tree leaf draws collapsed (c232.z = 0) for 120 frames at guest frame {}: requesting a "
                  "frame dump", frame);
      std::ofstream(LogPath("dump_request")) << "collapse\n";
    }
    g_collapse.frame_bad = g_collapse.frame_good = false;
  }
  if (!Active()) return;
  g_packet.index = frame;
  g_packet.front_buffer = front_buffer;
  if (REXCVAR_GET(gtav_native) == "on") NoteGuestSwap(frame);
  {
    static size_t max_bytes = 0, max_draws = 0, max_inline = 0;
    max_bytes = std::max(max_bytes, g_packet.arena.size());
    max_draws = std::max(max_draws, g_packet.draws.size());
    if (frame % 300 == 0) {
      REXLOG_INFO("[native] packets per 300 frames: largest {} KB, most draws {}", max_bytes >> 10, max_draws);
      max_bytes = max_draws = 0;
    }
  }
  Submit(std::move(g_packet));
  g_packet = TakeSpare();
}

uint64_t GuestFrame() { return g_frame.load(std::memory_order_relaxed); }

}  // namespace gtav::native

#ifdef _WIN32
#define GTAV_HOOK_EXPORT __declspec(dllexport)
#else
#define GTAV_HOOK_EXPORT __attribute__((visibility("default")))  // exported by the link options
#endif
// Called by the SDK's command processor (gpu_skip_rendering) before every EVENT_WRITE_SHD; returns
// the value to write. The game's fence (value N at every kick, sub_8380D8A8) is held back by
// --gtav_native_fence_lag kicks: the real GPU runs about a frame behind the CPU, and the game
// fills some vertex buffers after the kick that carries their draw; this command processor runs
// right behind the CPU, so a copy at the real fence caught them half-filled (foliage flicker).
// Held-back values are released once the command processor idles (the game waits for them) and
// at every swap.
namespace gtav::native {
static std::mutex g_fence_mutex;
static uint32_t g_fence_latest = 0, g_fence_written = 0, g_fence_endian = 0;
static bool g_fence_any = false;

static void StoreFence(uint32_t address, uint32_t value, uint32_t endian) {
  switch (endian) {  // xenos::GpuSwap, then the command processor's host-order store
    case 1: value = ((value & 0x00FF00FFu) << 8) | ((value >> 8) & 0x00FF00FFu); break;
    case 2: value = __builtin_bswap32(value); break;
    case 3: value = (value << 16) | (value >> 16); break;
  }
  std::memcpy(REX_KERNEL_MEMORY()->TranslatePhysical(address), &value, 4);
}

// Releases every held-back fence (command processor thread: idle, or before its swap).
void FenceFlush() {
  std::lock_guard lock(g_fence_mutex);
  uint32_t address = g_fence_address.load(std::memory_order_relaxed);
  if (!g_fence_any || !address || g_fence_written == g_fence_latest) return;
  g_fence_written = g_fence_latest;
  FenceReached(g_fence_written);
  StoreFence(address & 0x1FFFFFFF, g_fence_written, g_fence_endian);
}
}  // namespace gtav::native

#ifdef _WIN32
#define GTAV_HOOK_EXPORT __declspec(dllexport)
#else
#define GTAV_HOOK_EXPORT __attribute__((visibility("default")))  // exported by the link options
#endif
extern "C" GTAV_HOOK_EXPORT uint32_t rex_skip_rendering_fence(uint32_t address, uint32_t value, uint32_t endian) {
  using namespace gtav::native;
  gtav::linux_threads::Note(gtav::linux_threads::kCommandProcessor);
  uint32_t fence = g_fence_address.load(std::memory_order_relaxed);
  if (!fence || (address & 0x1FFFFFFF) != (fence & 0x1FFFFFFF)) return value;
  std::lock_guard lock(g_fence_mutex);
  g_fence_latest = value;
  g_fence_endian = endian;
  uint32_t lag = uint32_t(std::max(0, REXCVAR_GET(gtav_native_fence_lag))) * 2;  // the kick adds 2
  uint32_t target = value - lag;
  if (!g_fence_any || int32_t(target - g_fence_written) > 0) g_fence_written = target;
  g_fence_any = true;
  FenceReached(g_fence_written);
  return g_fence_written;
}

extern "C" GTAV_HOOK_EXPORT void rex_skip_rendering_idle() { gtav::native::FenceFlush(); }
