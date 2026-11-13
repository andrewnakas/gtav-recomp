// gtav - Linux desktop thread placement (--gtav_linux_pin, live: an A/B can flip it).
//
// Hybrid Intel CPUs (this laptop: 4 P-cores with hyperthreading = CPUs 0-7, 4 E-cores = CPUs
// 8-11) leave the placement of the game's four hot threads to the scheduler, which lands the
// update thread on an E-core (about half the speed) or on the sibling hyperthread of another
// busy thread. With pinning on, each hot thread (Main XThread, the game's D3D thread, the native
// render thread, the SDK command processor) gets the first hyperthread of its own P-core, the
// sibling stays free, and background work (Vulkan pipeline compiles, shader compiler processes)
// goes to the E-cores. Nothing else is pinned: Windows lost fps when everything was.
//
// Topology comes from /sys; machines without SMT P-cores or with fewer than four of them are
// left alone. A placer thread applies the setting every 250 ms, so the hot threads only have to
// register themselves (Note) from the code that already runs on them.
#include "linux_threads.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <sched.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <dirent.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(gtav_linux_pin, false, "GTAV",
                    "Linux: give the game's hot threads a P-core each and keep background work on the E-cores");
REXCVAR_DEFINE_INT32(gtav_linux_timer_slack_us, 0, "GTAV",
                     "Linux: timer slack of the hot threads in microseconds (0 = the kernel default, 50)");

namespace gtav::linux_threads {
namespace {

struct Topology {
  std::vector<int> hot;         // one CPU per P-core (its first hyperthread), in order
  std::vector<int> background;  // the E-cores
  int cpus = 0;
  std::string background_list;
};

Topology ReadTopology() {
  Topology t;
  t.cpus = int(sysconf(_SC_NPROCESSORS_CONF));
  std::vector<int> seen_first;  // first CPU of each core already counted
  for (int cpu = 0; cpu < t.cpus; cpu++) {
    std::ifstream in("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/core_cpus_list");
    std::string list;
    if (!(in >> list)) return Topology{};
    // "0-1" or "0,4" (siblings) or "8" (a core without SMT).
    int first = std::atoi(list.c_str());
    if (first != cpu) continue;  // a sibling of a core already seen
    bool smt = list.find('-') != std::string::npos || list.find(',') != std::string::npos;
    (smt ? t.hot : t.background).push_back(cpu);
  }
  if (t.hot.size() < 4 || t.background.empty()) return Topology{};
  // "8-11" style list for taskset (the E-cores are contiguous on Intel hybrid parts).
  t.background_list = std::to_string(t.background.front());
  if (t.background.size() > 1) t.background_list += "-" + std::to_string(t.background.back());
  return t;
}

const Topology& Topo() {
  static const Topology t = ReadTopology();
  return t;
}

std::atomic<int> g_tid[kKinds];
std::string g_background_list;  // written once before the placer starts

bool SetAffinity(int tid, const std::vector<int>& cpus) {
  cpu_set_t set;
  CPU_ZERO(&set);
  for (int c : cpus) CPU_SET(c, &set);
  return sched_setaffinity(tid, sizeof(set), &set) == 0;
}

std::vector<int> AllCpus() {
  std::vector<int> all;
  for (int i = 0; i < Topo().cpus; i++) all.push_back(i);
  return all;
}

// Threads that only do background work: Vulkan pipeline compiles and shader module creation
// (renderer.cpp), the SDK's pipeline cache workers. Matched by name (/proc/self/task/*/comm).
bool IsBackgroundName(const char* name) {
  static const char* const kPrefixes[] = {"Vulkan Pipeline", "Vulkan Storage", "Pipeline warm", "SPIR-V"};
  for (const char* p : kPrefixes)
    if (std::strncmp(name, p, std::strlen(p)) == 0) return true;
  return false;
}

void PlaceBackground(bool pin, std::vector<int>& done) {
  DIR* dir = opendir("/proc/self/task");
  if (!dir) return;
  while (dirent* e = readdir(dir)) {
    if (e->d_name[0] == '.') continue;
    int tid = std::atoi(e->d_name);
    char path[64], name[32] = {};
    std::snprintf(path, sizeof(path), "/proc/self/task/%d/comm", tid);
    if (FILE* f = std::fopen(path, "r")) {
      size_t n = std::fread(name, 1, sizeof(name) - 1, f);
      name[n] = 0;
      std::fclose(f);
    }
    if (!IsBackgroundName(name)) continue;
    bool seen = std::find(done.begin(), done.end(), tid) != done.end();
    if (pin && !seen) {
      if (SetAffinity(tid, Topo().background)) done.push_back(tid);
    } else if (!pin && seen) {
      SetAffinity(tid, AllCpus());
      done.erase(std::find(done.begin(), done.end(), tid));
    }
  }
  closedir(dir);
}

void Placer() {
  const Topology& t = Topo();
  int applied = -1;
  int pinned_tid[kKinds] = {};
  std::vector<int> background_done;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const bool want = REXCVAR_GET(gtav_linux_pin);
    for (int k = 0; k < kKinds; k++) {
      int tid = g_tid[k].load(std::memory_order_acquire);
      if (!tid) continue;
      // Pin a new thread, or re-place every thread when the setting changed.
      if (want && (pinned_tid[k] != tid || applied != 1)) {
        if (SetAffinity(tid, {t.hot[size_t(k) % t.hot.size()]})) pinned_tid[k] = tid;
      } else if (!want && pinned_tid[k]) {
        SetAffinity(pinned_tid[k], AllCpus());
        pinned_tid[k] = 0;
      }
    }
    PlaceBackground(want, background_done);
    if (applied != int(want)) {
      REXLOG_INFO("[linux] hot threads {} (P-cores {} {} {} {}, background CPUs {}); main {} d3d {} render {} cp {}",
                  want ? "pinned" : "unpinned", t.hot[0], t.hot[1], t.hot[2], t.hot[3], t.background_list,
                  g_tid[kMain].load(), g_tid[kD3D].load(), g_tid[kRender].load(), g_tid[kCommandProcessor].load());
      applied = int(want);
    }
  }
}

const bool g_placer = [] {
  if (Topo().hot.empty()) {
    REXLOG_INFO("[linux] no hybrid SMT topology: thread placement off");
    return false;
  }
  g_background_list = Topo().background_list;
  std::thread(Placer).detach();
  return true;
}();

}  // namespace

void Note(Kind kind) {
  static thread_local bool noted[kKinds];
  if (noted[kind]) return;
  noted[kind] = true;
  if (Topo().hot.empty()) return;
  g_tid[kind].store(int(syscall(SYS_gettid)), std::memory_order_release);
  int slack = REXCVAR_GET(gtav_linux_timer_slack_us);
  if (slack > 0) prctl(PR_SET_TIMERSLACK, (unsigned long)slack * 1000UL);
}

const char* BackgroundCpuList() {
  return REXCVAR_GET(gtav_linux_pin) ? g_background_list.c_str() : "";
}

}  // namespace gtav::linux_threads

#else

namespace gtav::linux_threads {
void Note(Kind) {}
const char* BackgroundCpuList() { return ""; }
}  // namespace gtav::linux_threads

#endif
