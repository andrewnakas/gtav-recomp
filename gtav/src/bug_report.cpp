// gtav - one-file bug reports: everything a GitHub issue needs, zipped next to the logs.
//
// Settings menu "Create bug report" (and automatically on the start after a crash) writes
// <logs>/gtav-bug-report-<date>-<time>.zip with: the end of the game log (and the previous rotated
// one), crash.txt, the launcher's install log, gtav_settings.ini, controls.ini and system.txt
// (build, OS, CPU, RAM, GPU from the log). The zip is plain "stored" (no compression library).

#include "bug_report.h"

#include "native/paths.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <intrin.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/utsname.h>
#else
#include <sys/utsname.h>
#endif

#ifndef GTAV_BUILD_ID
#define GTAV_BUILD_ID "unknown"
#endif

REXCVAR_DECLARE(std::string, log_file);

namespace gtav::bug_report {
namespace fs = std::filesystem;
namespace {

constexpr size_t kLogTailBytes = 24u << 20;  // the end of a log is what matters; GitHub takes 25 MB

uint32_t Crc32(const uint8_t* data, size_t size) {
  static const auto table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

// Minimal zip writer: stored entries, no zip64 (every entry and the archive stay far below 4 GB).
class ZipWriter {
 public:
  explicit ZipWriter(const fs::path& path) : out_(path, std::ios::binary) {}
  bool ok() const { return bool(out_); }
  void Add(const std::string& name, const std::string& data) {
    Entry e{name, Crc32(reinterpret_cast<const uint8_t*>(data.data()), data.size()), uint32_t(data.size()),
            uint32_t(out_.tellp())};
    Put32(0x04034b50);
    Put16(20), Put16(0x0800), Put16(0), Put16(0), Put16(0x21);  // version, UTF-8 names, stored, time/date
    Put32(e.crc), Put32(e.size), Put32(e.size);
    Put16(uint16_t(name.size())), Put16(0);
    out_.write(name.data(), std::streamsize(name.size()));
    out_.write(data.data(), std::streamsize(data.size()));
    entries_.push_back(e);
  }
  void Finish() {
    const uint32_t dir_start = uint32_t(out_.tellp());
    for (const Entry& e : entries_) {
      Put32(0x02014b50);
      Put16(20), Put16(20), Put16(0x0800), Put16(0), Put16(0), Put16(0x21);
      Put32(e.crc), Put32(e.size), Put32(e.size);
      Put16(uint16_t(e.name.size())), Put16(0), Put16(0), Put16(0), Put16(0);
      Put32(0), Put32(e.offset);
      out_.write(e.name.data(), std::streamsize(e.name.size()));
    }
    const uint32_t dir_size = uint32_t(out_.tellp()) - dir_start;
    Put32(0x06054b50);
    Put16(0), Put16(0), Put16(uint16_t(entries_.size())), Put16(uint16_t(entries_.size()));
    Put32(dir_size), Put32(dir_start), Put16(0);
  }

 private:
  struct Entry {
    std::string name;
    uint32_t crc, size, offset;
  };
  void Put16(uint16_t v) { out_.put(char(v & 0xFF)), out_.put(char(v >> 8)); }
  void Put32(uint32_t v) { Put16(uint16_t(v)), Put16(uint16_t(v >> 16)); }
  std::ofstream out_;
  std::vector<Entry> entries_;
};

// The file, or only its last `limit` bytes (cut at a line start).
bool ReadTail(const fs::path& path, size_t limit, std::string* out) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return false;
  const std::streamoff size = in.tellg();
  const std::streamoff start = size > std::streamoff(limit) ? size - std::streamoff(limit) : 0;
  in.seekg(start);
  out->assign(size_t(size - start), '\0');
  in.read(out->data(), std::streamsize(out->size()));
  if (start > 0) {
    const size_t nl = out->find('\n');
    if (nl != std::string::npos) out->erase(0, nl + 1);
    out->insert(0, "[... start of the log cut: only the last part is included ...]\n");
  }
  return true;
}

std::string Trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}

std::string SystemInfo(const std::string& reason, const std::string& log_text) {
  std::ostringstream o;
  const std::time_t now = std::time(nullptr);
  char when[64];
  std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
  o << "GTA V recomp bug report\n";
  o << "created: " << when << " (" << reason << ")\n";
  o << "build: " << GTAV_BUILD_ID << "\n";
#if defined(_WIN32)
  HKEY key;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key) ==
      ERROR_SUCCESS) {
    for (const char* name : {"ProductName", "DisplayVersion", "CurrentBuildNumber"}) {
      char value[256] = {};
      DWORD size = sizeof(value);
      if (RegQueryValueExA(key, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS)
        o << "os." << name << ": " << value << "\n";
    }
    RegCloseKey(key);
  }
  int regs[4];
  char brand[49] = {};
  for (int i = 0; i < 3; ++i) {
    __cpuid(regs, int(0x80000002u + i));
    std::memcpy(brand + 16 * i, regs, 16);
  }
  o << "cpu: " << Trim(brand) << "\n";
  MEMORYSTATUSEX mem{sizeof(mem)};
  if (GlobalMemoryStatusEx(&mem)) o << "ram: " << (mem.ullTotalPhys >> 20) << " MB\n";
#elif defined(__APPLE__)
  char buf[256];
  size_t size = sizeof(buf);
  if (sysctlbyname("kern.osproductversion", buf, &size, nullptr, 0) == 0) o << "os: macOS " << buf << "\n";
  size = sizeof(buf);
  if (sysctlbyname("machdep.cpu.brand_string", buf, &size, nullptr, 0) == 0) o << "cpu: " << buf << "\n";
  uint64_t memsize = 0;
  size = sizeof(memsize);
  if (sysctlbyname("hw.memsize", &memsize, &size, nullptr, 0) == 0) o << "ram: " << (memsize >> 20) << " MB\n";
#else
  {
    std::ifstream f("/etc/os-release");
    std::string line;
    while (std::getline(f, line))
      if (line.rfind("PRETTY_NAME=", 0) == 0) o << "os: " << line.substr(12) << "\n";
    struct utsname u {};
    if (uname(&u) == 0) o << "kernel: " << u.sysname << " " << u.release << " " << u.machine << "\n";
    std::ifstream cpu("/proc/cpuinfo");
    while (std::getline(cpu, line))
      if (line.rfind("model name", 0) == 0) {
        o << "cpu: " << Trim(line.substr(line.find(':') + 2)) << "\n";
        break;
      }
    std::ifstream mem("/proc/meminfo");
    while (std::getline(mem, line))
      if (line.rfind("MemTotal", 0) == 0) o << "ram: " << Trim(line.substr(9)) << "\n";
    if (const char* deck = std::getenv("SteamDeck")) o << "steam deck: " << deck << "\n";
    if (const char* session = std::getenv("XDG_SESSION_TYPE")) o << "session: " << session << "\n";
  }
#endif
  o << "threads: " << std::thread::hardware_concurrency() << "\n";
  // The renderer logs the GPUs it found and the one it uses.
  std::istringstream lines(log_text);
  std::string line;
  std::vector<std::string> gpu;
  while (std::getline(lines, line))
    if (line.find("[native] Vulkan device:") != std::string::npos || line.find("[native] using ") != std::string::npos ||
        line.find("audio endpoint") != std::string::npos)
      gpu.push_back(line.substr(line.find(']') + 2));
  for (size_t i = gpu.size() > 8 ? gpu.size() - 8 : 0; i < gpu.size(); ++i) o << "log: " << gpu[i] << "\n";
  return o.str();
}

fs::path LogPath() {
  const std::string& p = REXCVAR_GET(log_file);
  return p.empty() ? fs::path() : fs::absolute(p);
}

}  // namespace

fs::path LogDir() {
  const fs::path log = LogPath();
  return log.empty() ? native::ExeDir() / "logs" : log.parent_path();
}

std::string Create(const std::string& reason) {
  std::error_code ec;
  const fs::path dir = LogDir();
  fs::create_directories(dir, ec);
  const std::time_t now = std::time(nullptr);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
  const fs::path zip_path = dir / (std::string("gtav-bug-report-") + stamp + ".zip");
  ZipWriter zip(zip_path);
  if (!zip.ok()) return {};

  std::string log_text, text;
  const fs::path log = LogPath();
  if (!log.empty() && ReadTail(log, kLogTailBytes, &log_text)) zip.Add(log.filename().string(), log_text);
  if (!log.empty()) {  // the previous rotated log (spdlog: gtav.1.log)
    const fs::path rotated = log.parent_path() / (log.stem().string() + ".1" + log.extension().string());
    if (ReadTail(rotated, 4u << 20, &text)) zip.Add(rotated.filename().string(), text);
  }
  for (const fs::path& p : {fs::current_path(ec) / "crash.txt", dir / "crash.txt", dir / "last-crash.txt"})
    if (ReadTail(p, 1u << 20, &text)) zip.Add(p.filename().string(), text);
  for (const char* name : {"install.log", "codegen.log", "unattended.txt", "gtav_stdout.txt"})
    if (ReadTail(dir / name, 2u << 20, &text)) zip.Add(name, text);
  for (const char* name : {"gtav_settings.ini", "controls.ini"})
    if (ReadTail(native::ExeDir() / name, 1u << 20, &text)) zip.Add(name, text);
  zip.Add("system.txt", SystemInfo(reason, log_text));
  zip.Finish();
  REXLOG_INFO("[bug report] {} written ({})", zip_path.string(), reason);
  return zip_path.string();
}

void ShowInFolder(const std::string& path) {
#if defined(_WIN32)
  const std::wstring arg = L"/select,\"" + fs::path(path).wstring() + L"\"";
  ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  const std::string cmd = "open -R '" + path + "' >/dev/null 2>&1 &";
  std::system(cmd.c_str());
#else
  const std::string cmd = "xdg-open '" + fs::path(path).parent_path().string() + "' >/dev/null 2>&1 &";
  std::system(cmd.c_str());
#endif
}

std::string CreateIfLastRunCrashed() {
  // crash.txt (crash_handler.cpp) is written into the working directory by a crashing run; it is
  // moved into the logs once reported, so its presence means the last run crashed.
  std::error_code ec;
  const fs::path crash = fs::current_path(ec) / "crash.txt";
  if (!fs::exists(crash, ec)) return {};
  const fs::path kept = LogDir() / "last-crash.txt";
  fs::create_directories(kept.parent_path(), ec);
  fs::rename(crash, kept, ec);
  if (ec) fs::copy_file(crash, kept, fs::copy_options::overwrite_existing, ec), fs::remove(crash, ec);
  return Create("the previous run crashed");
}

}  // namespace gtav::bug_report
