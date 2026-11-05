// gtav - keyboard and mouse like GTA V on PC (issue #2).
//
// The SDK's keyboard/mouse driver (mnk_pc_layout) maps every PC key to the pad button of the same
// game action, per context: on foot, car/motorbike/boat, bicycle, aircraft, submarine, parachute,
// pause menu. This file tells it the context and carries out the PC actions the console pad has no
// button for: the mouse wheel and 1-9 select weapons (PC: SELECT_NEXT/PREV_WEAPON and
// SELECT_WEAPON_<category>).
//
// Both run inside the game's own script thread: PLAYER_PED_ID's handler (scripts call it many times
// a frame) is hooked, and from there, at most every 30 ms, the game's own script command handlers
// are called with the player ped (IS_PED_IN_ANY_VEHICLE, ..., SET_CURRENT_PED_WEAPON): exactly the
// game's answers, on the thread that owns the data. Native hashes are joaat of the command names
// (old-gen); handlers from the registration calls to sub_835253D0.

#include "pc_controls.h"
#include "bug_report.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "native/safe_read.h"
#ifdef _WIN32
#include <windows.h>
#endif

#include <array>
#include <map>
#include <bit>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <thread>
#include <filesystem>
#include <fstream>
#include "native/paths.h"

REX_EXTERN(__imp__sub_82CE9CA8);  // PLAYER_PED_ID
REX_EXTERN(sub_82D0BF30);         // IS_PED_IN_ANY_VEHICLE(ped, include_entering)
REX_EXTERN(sub_82D0C8E0);         // IS_PED_IN_ANY_PLANE(ped)
REX_EXTERN(sub_82D0C8A0);         // IS_PED_IN_ANY_HELI(ped)
REX_EXTERN(sub_82D0C820);         // IS_PED_IN_ANY_BOAT(ped)
REX_EXTERN(sub_82D0C860);         // IS_PED_IN_ANY_SUB(ped)
REX_EXTERN(sub_82D0C7A0);         // IS_PED_ON_ANY_BIKE(ped)
REX_EXTERN(sub_82D0C510);         // GET_VEHICLE_PED_IS_IN(ped, last)
REX_EXTERN(sub_82CB2690);         // GET_ENTITY_MODEL(entity)
REX_EXTERN(sub_82D32000);         // IS_THIS_MODEL_A_BICYCLE(model)
REX_EXTERN(sub_82D0CD48);         // GET_PED_PARACHUTE_STATE(ped)
REX_EXTERN(sub_82C835F0);         // IS_PAUSE_MENU_ACTIVE()
REX_EXTERN(sub_82D57A40);         // GET_SELECTED_PED_WEAPON(ped)
REX_EXTERN(sub_82D5C080);         // HAS_PED_GOT_WEAPON(ped, hash, 0)
REX_EXTERN(sub_82D5BFD0);         // SET_CURRENT_PED_WEAPON(ped, hash, equip_now)
REX_EXTERN(sub_82C9C078);         // SET_ENTITY_HEALTH(entity, health)
REX_EXTERN(sub_82C9ADD8);         // GET_ENTITY_COORDS(entity, alive) -> vector3
REX_EXTERN(sub_82D5F218);         // GET_CLOSEST_VEHICLE(x, y, z, radius, model, flags)
REX_EXTERN(sub_82D0C3D8);         // SET_PED_INTO_VEHICLE(ped, vehicle, seat)
REX_EXTERN(sub_82D28BB8);         // REQUEST_SCRIPT(name)
REX_EXTERN(sub_82D28BD8);         // HAS_SCRIPT_LOADED(name)
REX_EXTERN(sub_82D28BC8);         // SET_SCRIPT_AS_NO_LONGER_NEEDED(name)
REX_EXTERN(sub_82D1FEC8);         // GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH(hash)
REX_EXTERN(sub_83527850);         // START_NEW_SCRIPT(name, stack size) (registered via sub_83524A28)

REXCVAR_DEFINE_BOOL(gtav_trace_cutscene_audio, false, "GTAV",
                    "Diagnostic: log the cutscene audio entity (0x83A320F8) state, its two stream slots and "
                    "their positions whenever they change (cutscene_audio_fix.cpp)");
REXCVAR_DEFINE_BOOL(gtav_debug_commands, false, "GTAV",
                    "Test hook: <exe>/logs/native_cmd is run on the script thread once per second "
                    "('kill' = SET_ENTITY_HEALTH(player, 0)) and deleted");

namespace {

using rex::input::mnk::InputContext;

constexpr uint32_t Joaat(std::string_view s) {
  uint32_t h = 0;
  for (char c : s) {
    h += uint8_t(c >= 'A' && c <= 'Z' ? c + 32 : c);
    h += h << 10;
    h ^= h >> 6;
  }
  h += h << 3;
  h ^= h >> 11;
  h += h << 15;
  return h;
}

uint32_t Load32(uint8_t* base, uint32_t a) {
  const uint8_t* p = base + a;
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
void Store32(uint8_t* base, uint32_t a, uint32_t v) {
  uint8_t* p = base + a;
  p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
}

// Calls a script command handler as the script VM does: r3 = {result*, argc, args*}, 4-byte
// arguments. Scratch space below the stack pointer, with the stack pointer moved past it.
using Handler = void (*)(PPCContext&, uint8_t*);
uint32_t Call(PPCContext& ctx, uint8_t* base, Handler fn, std::initializer_list<uint32_t> args) {
  const PPCContext saved = ctx;
  const uint32_t sp = (ctx.r1.u32 - 512) & ~15u;
  const uint32_t call = sp + 256, argv = sp + 288, result = sp + 352;
  Store32(base, call + 0, result);
  Store32(base, call + 4, uint32_t(args.size()));
  Store32(base, call + 8, argv);
  uint32_t i = 0;
  for (uint32_t a : args) Store32(base, argv + 4 * i++, a);
  Store32(base, result, 0);
  ctx.r1.u32 = sp;
  ctx.r3.u64 = call;
  fn(ctx, base);
  const uint32_t r = Load32(base, result);
  ctx = saved;
  return r;
}

// As Call, for commands that return a vector: its three floats (raw bits) into out.
void CallVector(PPCContext& ctx, uint8_t* base, Handler fn, std::initializer_list<uint32_t> args, uint32_t out[3]) {
  const PPCContext saved = ctx;
  const uint32_t sp = (ctx.r1.u32 - 512) & ~15u;
  const uint32_t call = sp + 256, argv = sp + 288, result = sp + 352;
  Store32(base, call + 0, result);
  Store32(base, call + 4, uint32_t(args.size()));
  Store32(base, call + 8, argv);
  uint32_t i = 0;
  for (uint32_t a : args) Store32(base, argv + 4 * i++, a);
  ctx.r1.u32 = sp;
  ctx.r3.u64 = call;
  fn(ctx, base);
  for (int k = 0; k < 3; ++k) out[k] = Load32(base, result + 4 * k);
  ctx = saved;
}

// A script name for the next Call: written into the same scratch area, past Call's own use.
uint32_t ScratchString(PPCContext& ctx, uint8_t* base, const std::string& text) {
  const uint32_t sp = (ctx.r1.u32 - 512) & ~15u;
  const uint32_t at = sp + 400;
  const size_t n = std::min<size_t>(text.size(), 100);
  std::memcpy(base + at, text.data(), n);
  base[at + n] = 0;
  return at;
}

std::string g_pending_mission;  // debug command "mission <script>": started once loaded
int g_mission_tries = 0;

// PC weapon categories (keys 1-9) in wheel order; the mouse wheel steps through them in this order.
// Hashes the game does not know just fail HAS_PED_GOT_WEAPON.
#define W(name) Joaat("WEAPON_" name)
constexpr uint32_t kUnarmed[] = {W("UNARMED")};
constexpr uint32_t kMelee[] = {W("KNIFE"),    W("NIGHTSTICK"), W("HAMMER"),  W("BAT"),        W("CROWBAR"),
                               W("GOLFCLUB"), W("BOTTLE"),     W("DAGGER"),  W("HATCHET"),    W("KNUCKLE"),
                               W("MACHETE"),  W("FLASHLIGHT"), W("SWITCHBLADE"), W("POOLCUE"), W("WRENCH"),
                               W("BATTLEAXE")};
constexpr uint32_t kShotgun[] = {W("PUMPSHOTGUN"), W("SAWNOFFSHOTGUN"), W("BULLPUPSHOTGUN"), W("ASSAULTSHOTGUN"),
                                 W("MUSKET"),      W("HEAVYSHOTGUN"),   W("DBSHOTGUN"),      W("AUTOSHOTGUN")};
constexpr uint32_t kHeavy[] = {W("GRENADELAUNCHER"), W("RPG"),         W("MINIGUN"),         W("FIREWORK"),
                               W("RAILGUN"),         W("HOMINGLAUNCHER"), W("GRENADELAUNCHER_SMOKE"),
                               W("COMPACTLAUNCHER")};
constexpr uint32_t kThrown[] = {W("GRENADE"),  W("STICKYBOMB"), W("SMOKEGRENADE"), W("BZGAS"),     W("MOLOTOV"),
                                W("FIREEXTINGUISHER"), W("PETROLCAN"), W("BALL"), W("FLARE"),     W("PROXMINE"),
                                W("SNOWBALL"), W("PIPEBOMB")};
constexpr uint32_t kPistol[] = {W("PISTOL"),      W("COMBATPISTOL"), W("APPISTOL"),       W("PISTOL50"),
                                W("SNSPISTOL"),   W("HEAVYPISTOL"),  W("VINTAGEPISTOL"),  W("MARKSMANPISTOL"),
                                W("REVOLVER"),    W("STUNGUN"),      W("FLAREGUN")};
constexpr uint32_t kSmg[] = {W("MICROSMG"), W("SMG"),     W("ASSAULTSMG"), W("COMBATPDW"), W("MACHINEPISTOL"),
                             W("MINISMG"),  W("MG"),      W("COMBATMG"),   W("GUSENBERG")};
constexpr uint32_t kRifle[] = {W("ASSAULTRIFLE"),   W("CARBINERIFLE"), W("ADVANCEDRIFLE"),
                               W("SPECIALCARBINE"), W("BULLPUPRIFLE"), W("COMPACTRIFLE")};
constexpr uint32_t kSniper[] = {W("SNIPERRIFLE"), W("HEAVYSNIPER"), W("MARKSMANRIFLE"), W("REMOTESNIPER")};
#undef W

struct Category {
  const uint32_t* weapons;
  size_t count;
};
// Index = PC key 1..9.
constexpr Category kCategories[10] = {
    {nullptr, 0},
    {kUnarmed, std::size(kUnarmed)}, {kMelee, std::size(kMelee)},   {kShotgun, std::size(kShotgun)},
    {kHeavy, std::size(kHeavy)},     {kThrown, std::size(kThrown)}, {kPistol, std::size(kPistol)},
    {kSmg, std::size(kSmg)},         {kRifle, std::size(kRifle)},   {kSniper, std::size(kSniper)},
};

std::atomic<bool> g_busy{false};
std::chrono::steady_clock::time_point g_next_check{};
InputContext g_context = InputContext::kOnFoot;

InputContext Classify(PPCContext& ctx, uint8_t* base, uint32_t ped) {
  if (Call(ctx, base, sub_82D0BF30, {ped, 0}) & 0xFF) {
    if (Call(ctx, base, sub_82D0C8E0, {ped}) & 0xFF) return InputContext::kAircraft;
    if (Call(ctx, base, sub_82D0C8A0, {ped}) & 0xFF) return InputContext::kAircraft;
    if (Call(ctx, base, sub_82D0C860, {ped}) & 0xFF) return InputContext::kSub;
    if (Call(ctx, base, sub_82D0C820, {ped}) & 0xFF) return InputContext::kBoat;
    if (Call(ctx, base, sub_82D0C7A0, {ped}) & 0xFF) {
      const uint32_t vehicle = Call(ctx, base, sub_82D0C510, {ped, 0});
      const uint32_t model = vehicle ? Call(ctx, base, sub_82CB2690, {vehicle}) : 0;
      return model && (Call(ctx, base, sub_82D32000, {model}) & 0xFF) ? InputContext::kBicycle
                                                                      : InputContext::kBike;
    }
    return InputContext::kCar;
  }
  // GET_PED_PARACHUTE_STATE: -1 none, 0 free fall, 1 deploying, 2 gliding, 3 landing.
  const int32_t para = int32_t(Call(ctx, base, sub_82D0CD48, {ped}));
  if (para >= 0 && para <= 2) return InputContext::kParachute;
  return InputContext::kOnFoot;
}

bool Has(PPCContext& ctx, uint8_t* base, uint32_t ped, uint32_t hash) {
  return hash == Joaat("WEAPON_UNARMED") || (Call(ctx, base, sub_82D5C080, {ped, hash, 0}) & 0xFF);
}

// PC: a category key selects that category's weapon, again the next one in it; the wheel steps
// through every weapon the player has, in wheel order.
void Select(PPCContext& ctx, uint8_t* base, uint32_t ped, int request) {
  const uint32_t current = Call(ctx, base, sub_82D57A40, {ped});
  uint32_t pick = 0;
  if (request >= 1 && request <= 9) {
    const Category& c = kCategories[request];
    size_t at = c.count;
    for (size_t i = 0; i < c.count; ++i)
      if (c.weapons[i] == current) at = i;
    for (size_t step = 1; step <= c.count && !pick; ++step) {
      const uint32_t w = c.weapons[(at == c.count ? step - 1 : at + step) % c.count];
      if (Has(ctx, base, ped, w)) pick = w;
    }
  } else {
    std::array<uint32_t, 96> owned{};
    size_t n = 0, at = 0;
    for (int k = 1; k <= 9; ++k)
      for (size_t i = 0; i < kCategories[k].count && n < owned.size(); ++i) {
        const uint32_t w = kCategories[k].weapons[i];
        if (w == current) at = n;
        if (w == current || Has(ctx, base, ped, w)) owned[n++] = w;
      }
    if (n > 1) pick = owned[(at + (request == rex::input::mnk::kWeaponNext ? 1 : n - 1)) % n];
  }
  if (pick && pick != current) Call(ctx, base, sub_82D5BFD0, {ped, pick, 0});
}

}  // namespace

REX_HOOK_RAW(sub_82CE9CA8) {
  const uint32_t call = ctx.r3.u32;
  __imp__sub_82CE9CA8(ctx, base);
  const auto now = std::chrono::steady_clock::now();
  int request = rex::input::mnk::TakeWeaponRequest();
  if (!request && now < g_next_check) return;
  if (g_busy.exchange(true)) return;
  g_next_check = now + std::chrono::milliseconds(30);
  const uint32_t ped = Load32(base, Load32(base, call));
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("[controls] PC keyboard/mouse layout active (player ped {:08X})", ped);
  }
  if (ped) {
    const InputContext context = Classify(ctx, base, ped);
    if (context != g_context) {
      g_context = context;
      rex::input::mnk::SetInputContext(context);
      static const char* kNames[] = {"on foot", "car", "motorbike", "bicycle", "aircraft",
                                     "boat",    "submarine", "parachute", "pause menu"};
      REXLOG_INFO("[controls] {} layout", kNames[int(context)]);
    }
    if (REXCVAR_GET(gtav_debug_commands)) {
      static auto next = now;
      if (now >= next) {
        next = now + std::chrono::seconds(1);
        const auto path = gtav::native::ExeDir() / "logs" / "native_cmd";
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
          std::string cmd, arg;
          { std::ifstream in(path); in >> cmd >> arg; }
          std::filesystem::remove(path, ec);
          if (cmd == "mission") {
            g_pending_mission = arg;
            g_mission_tries = 0;
          }
          if (cmd == "report") gtav::bug_report::Create("debug command");
          if (cmd == "stacks") {  // the script stack pool (sub_83525480): size -> free/total
            const uint32_t table = Load32(base, 0x83DDDB38);
            const uint32_t count = Load32(base, 0x83DDDB38 + 4) >> 16;
            std::map<uint32_t, std::pair<int, int>> sizes;
            for (uint32_t i = 0; i < count && i < 4096; ++i) {
              const uint32_t e = table + 12 * i;
              auto& [free_count, total] = sizes[Load32(base, e + 4)];
              total++;
              if (!Load32(base, e)) free_count++;
            }
            std::string line;
            for (auto& [size, ft] : sizes) line += fmt::format(" {}:{}/{}", size, ft.first, ft.second);
            REXLOG_INFO("[controls] debug stacks ({} entries, size:free/total):{}", count, line);
          }
          if (cmd == "threads")  // "threads family3": how many threads run that script
            REXLOG_INFO("[controls] debug threads '{}': {}", arg,
                        Call(ctx, base, sub_82D1FEC8, {Joaat(arg)}));
          REXLOG_INFO("[controls] debug command '{}'", cmd);
          if (cmd == "kill") Call(ctx, base, sub_82C9C078, {ped, 0});

          if (cmd == "car") {  // the nearest vehicle, driver's seat
            uint32_t xyz[3];
            CallVector(ctx, base, sub_82C9ADD8, {ped, 1}, xyz);
            const uint32_t radius = std::bit_cast<uint32_t>(30.0f);
            uint32_t vehicle = 0;
            for (uint32_t flags : {70u, 71u, 127u, 0u}) {
              vehicle = Call(ctx, base, sub_82D5F218, {xyz[0], xyz[1], xyz[2], radius, 0, flags});
              if (vehicle) break;
            }
            REXLOG_INFO("[controls] debug car: vehicle {:08X}", vehicle);
            if (vehicle) Call(ctx, base, sub_82D0C3D8, {ped, vehicle, uint32_t(-1)});
          }
        }
      }
    }
    if (!g_pending_mission.empty()) {  // load, then start (GTA V mission stacks come in pools)
      static auto next_try = now;
      if (now >= next_try) {
        next_try = now + std::chrono::milliseconds(500);
        const std::string name = g_pending_mission;
        Call(ctx, base, sub_82D28BB8, {ScratchString(ctx, base, name)});
        if (Call(ctx, base, sub_82D28BD8, {ScratchString(ctx, base, name)}) & 0xFF) {
          uint32_t id = 0, used = 0;
          for (uint32_t stack : {15000u, 21000u, 9300u, 8032u, 6304u, 5000u, 4600u, 4592u}) {
            id = Call(ctx, base, sub_83527850, {ScratchString(ctx, base, name), stack});
            if (id) {
              used = stack;
              break;
            }
          }
          Call(ctx, base, sub_82D28BC8, {ScratchString(ctx, base, name)});
          REXLOG_INFO("[controls] debug mission '{}': thread {} (stack {})", name, id, used);
          g_pending_mission.clear();
        } else if (++g_mission_tries > 60) {
          REXLOG_INFO("[controls] debug mission '{}': script never loaded", name);
          g_pending_mission.clear();
        }
      }
    }
    for (; request; request = rex::input::mnk::TakeWeaponRequest())
      if (context == InputContext::kOnFoot && request <= rex::input::mnk::kWeaponPrevious) Select(ctx, base, ped, request);
  }
  g_busy = false;
}

namespace gtav {
// Scripts stop while the game is paused, so the pause menu is read from memory instead: the two
// flags IS_PAUSE_MENU_ACTIVE (sub_82C835F0 -> sub_82265E60) tests first, bytes 0x839F64F1 and
// 0x839F64DB.
void StartVehicleContextPoll() {
  std::thread([] {
    bool last = false;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(REXCVAR_GET(gtav_trace_cutscene_audio) ? 10 : 30));
      auto* memory = REX_KERNEL_MEMORY();
      if (!memory) continue;
      auto* base = memory->TranslateVirtual<uint8_t*>(0);
      uint8_t a = 0, b = 0;
#ifdef _WIN32
      MEMORY_BASIC_INFORMATION info{};
      if (!VirtualQuery(base + 0x839F64DB, &info, sizeof(info)) || info.State != MEM_COMMIT) continue;
      a = base[0x839F64F1], b = base[0x839F64DB];
#else
      if (!gtav::native::SafeReadMemory(&a, base + 0x839F64F1, 1) ||
          !gtav::native::SafeReadMemory(&b, base + 0x839F64DB, 1))
        continue;
#endif
      if (REXCVAR_GET(gtav_trace_cutscene_audio)) {
        static std::string last_line;
        static auto next = std::chrono::steady_clock::now();
        if (std::chrono::steady_clock::now() >= next) {
          next = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
          auto rd = [&](uint32_t addr) -> uint32_t {
            uint8_t v[4] = {};
#ifdef _WIN32
            std::memcpy(v, base + addr, 4);
#else
            if (!gtav::native::SafeReadMemory(v, base + addr, 4)) return 0xDEADDEAD;
#endif
            return uint32_t(v[0]) << 24 | uint32_t(v[1]) << 16 | uint32_t(v[2]) << 8 | v[3];
          };
          constexpr uint32_t kAudio = 0x83A320F8, kSlots = 0x83A320C0 + 10296;
          const uint32_t state = rd(kAudio + 5068), flag = rd(kAudio + 5072) >> 24;
          const int32_t sa = int32_t(rd(kAudio + 5044)), sb = int32_t(rd(kAudio + 5048));
          std::string line = fmt::format("state {} flag {} slots {} {}", state, flag, sa, sb);
          for (int i = 0; i < 2; ++i) {
            const uint32_t st = rd(kSlots + 4 * i);
            line += fmt::format(" | slot{} {:08X}", i, st);
            if (st >= 0xC0000000 && st < 0xE0000000)
            {
              line += fmt::format(" pos {} end {} |", rd(st + 124), rd(st + 136));
              for (uint32_t o = 0; o < 256; o += 4) line += fmt::format(" {:08X}", rd(st + o));
            }
          }
          if (line != last_line) {
            last_line = line;
            REXLOG_INFO("[cuts] {}", line);
          }
        }
      }
      const bool paused = a || b;
      if (paused != last) {
        last = paused;
        rex::input::mnk::SetFrontendActive(paused);
        REXLOG_INFO("[controls] {} layout", paused ? "pause menu" : "gameplay");
      }
    }
  }).detach();
}
}  // namespace gtav
