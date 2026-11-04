// gtav - remappable controls (see controls.h).
//
// Keyboard/mouse: every action GTA V on PC lists in Settings > Keyboard/Mouse, with its PC default
// keys (primary, secondary), grouped by the context it applies in. A binding drives the pad
// control of the same game action (the console version reads only the pad); see the SDK's mnk
// driver (SetContextBindings) and pc_controls.cpp (which context is active).
// Mouse: the PC options (invert, per-context sensitivity).
// Controller: every button and trigger can be given another one's function.

#include "controls.h"

#include "paths.h"

#include <imgui.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/input/pad_remap.h>
#include <rex/logging.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace gtav::native::controls {
namespace {

using rex::input::mnk::InputContext;
using O = rex::input::mnk::PadOutput;

enum Table { kFoot = 0, kVehicle = 1, kBicycle = 3, kAircraft = 4, kSub = 6, kParachute = 7, kMenu = 8, kShared = 9 };

struct Action {
  Table table;
  const char* id;
  const char* name;  // as GTA V on PC names it
  O out;
  const char* defaults;  // "primary,secondary"
};

// PC defaults: docs.fivem.net/docs/game-references/controls (INPUT_* PC key and pad button).
const Action kActions[] = {
    // General (every context but the pause menu)
    {kShared, "next_camera", "Change Camera View", O::kBack, "V"},
    {kShared, "pause", "Pause", O::kStart, "P,Escape"},
    {kShared, "phone", "Phone / Phone Up", O::kDUp, "Up,MMB"},
    {kShared, "phone_down", "Phone Down", O::kDDown, "Down"},
    {kShared, "phone_left", "Phone Left", O::kDLeft, "Left"},
    {kShared, "phone_right", "Phone Right", O::kDRight, "Right"},
    {kShared, "phone_select", "Phone Select / Skip Cutscene", O::kA, "Return"},
    {kShared, "phone_cancel", "Phone Cancel", O::kB, "Backspace"},
    {kShared, "phone_option", "Phone Option", O::kY, "Delete"},
    {kShared, "character_wheel", "Character Wheel", O::kDDown, "Alt"},
    {kShared, "radar", "Expand Radar", O::kDDown, "Z"},
    {kShared, "look_behind", "Look Behind", O::kR3, "C"},
    {kShared, "switch_michael", "Switch to Michael", O::kSwitchMichael, "F5"},
    {kShared, "switch_franklin", "Switch to Franklin", O::kSwitchFranklin, "F6"},
    {kShared, "switch_trevor", "Switch to Trevor", O::kSwitchTrevor, "F7"},
    // On foot
    {kFoot, "move_forward", "Move Forward", O::kLSUp, "W"},
    {kFoot, "move_back", "Move Back", O::kLSDown, "S"},
    {kFoot, "move_left", "Move Left", O::kLSLeft, "A"},
    {kFoot, "move_right", "Move Right", O::kLSRight, "D"},
    {kFoot, "sprint", "Sprint", O::kA, "Shift"},
    {kFoot, "jump", "Jump", O::kX, "Space"},
    {kFoot, "enter", "Enter Vehicle", O::kY, "F"},
    {kFoot, "attack", "Attack", O::kRT, "LMB"},
    {kFoot, "aim", "Aim", O::kLT, "RMB"},
    {kFoot, "reload", "Reload / Melee Attack Light", O::kB, "R"},
    {kFoot, "cover", "Take Cover", O::kRB, "Q"},
    {kFoot, "context", "Context Action / Talk", O::kDRight, "E"},
    {kFoot, "grenade", "Throw Grenade / Detonate", O::kDLeft, "G"},
    {kFoot, "weapon_wheel", "Weapon Wheel", O::kLB, "Tab"},
    {kFoot, "duck", "Stealth Mode", O::kL3, "Control"},
    {kFoot, "special", "Special Ability", O::kBothSticks, "CapsLock"},
    {kFoot, "sniper_zoom_in", "Sniper Zoom In", O::kLSUp, "RBracket"},
    {kFoot, "sniper_zoom_out", "Sniper Zoom Out", O::kLSDown, "LBracket"},
    {kFoot, "next_weapon", "Next Weapon", O::kWeaponNext, "WheelDown"},
    {kFoot, "prev_weapon", "Previous Weapon", O::kWeaponPrevious, "WheelUp"},
    {kFoot, "weapon_unarmed", "Select Unarmed", O::kWeaponUnarmed, "1"},
    {kFoot, "weapon_melee", "Select Melee", O::kWeaponMelee, "2"},
    {kFoot, "weapon_shotgun", "Select Shotgun", O::kWeaponShotgun, "3"},
    {kFoot, "weapon_heavy", "Select Heavy Weapon", O::kWeaponHeavy, "4"},
    {kFoot, "weapon_special", "Select Special Weapon", O::kWeaponSpecial, "5"},
    {kFoot, "weapon_pistol", "Select Pistol", O::kWeaponPistol, "6"},
    {kFoot, "weapon_smg", "Select SMG", O::kWeaponSmg, "7"},
    {kFoot, "weapon_rifle", "Select Assault Rifle", O::kWeaponRifle, "8"},
    {kFoot, "weapon_sniper", "Select Sniper Rifle", O::kWeaponSniper, "9"},
    // Vehicles (cars, motorbikes, boats)
    {kVehicle, "accelerate", "Accelerate", O::kRT, "W"},
    {kVehicle, "brake", "Brake / Reverse", O::kLT, "S"},
    {kVehicle, "steer_left", "Steer Left", O::kLSLeft, "A"},
    {kVehicle, "steer_right", "Steer Right", O::kLSRight, "D"},
    {kVehicle, "lean_forward", "Lean Forward", O::kLSUp, "Shift"},
    {kVehicle, "lean_back", "Lean Back", O::kLSDown, "Control"},
    {kVehicle, "handbrake", "Handbrake", O::kRB, "Space"},
    {kVehicle, "attack", "Vehicle Attack", O::kRB, "LMB"},
    {kVehicle, "aim", "Vehicle Aim", O::kLB, "RMB"},
    {kVehicle, "duck", "Duck", O::kA, "X"},
    {kVehicle, "exit", "Exit Vehicle", O::kY, "F"},
    {kVehicle, "horn", "Horn", O::kL3, "E"},
    {kVehicle, "headlight", "Headlights", O::kDRight, "H"},
    {kVehicle, "radio_wheel", "Radio Station Wheel", O::kDLeft, "Q"},
    {kVehicle, "cinematic", "Cinematic Camera", O::kB, "R"},
    {kVehicle, "next_weapon", "Next Vehicle Weapon", O::kVehicleWeaponNext, "WheelUp"},
    {kVehicle, "special", "Special Ability", O::kBothSticks, "CapsLock"},
    // Bicycle
    {kBicycle, "pedal", "Pedal", O::kRT, "W"},
    {kBicycle, "rear_brake", "Rear Brake", O::kLT, "S"},
    {kBicycle, "front_brake", "Front Brake", O::kLT, "Q"},
    {kBicycle, "steer_left", "Steer Left", O::kLSLeft, "A"},
    {kBicycle, "steer_right", "Steer Right", O::kLSRight, "D"},
    {kBicycle, "lean_forward", "Lean Forward", O::kLSUp, "Shift"},
    {kBicycle, "lean_back", "Lean Back", O::kLSDown, "Control"},
    {kBicycle, "jump", "Jump", O::kRB, "Space"},
    {kBicycle, "sprint", "Sprint", O::kA, "CapsLock"},
    {kBicycle, "duck", "Duck", O::kA, "X"},
    {kBicycle, "exit", "Exit Vehicle", O::kY, "F"},
    {kBicycle, "cinematic", "Cinematic Camera", O::kB, "R"},
    // Aircraft
    {kAircraft, "throttle_up", "Throttle Up", O::kRT, "W"},
    {kAircraft, "throttle_down", "Throttle Down", O::kLT, "S"},
    {kAircraft, "yaw_left", "Yaw Left", O::kLB, "A"},
    {kAircraft, "yaw_right", "Yaw Right", O::kRB, "D"},
    {kAircraft, "roll_left", "Roll Left", O::kLSLeft, "Numpad4"},
    {kAircraft, "roll_right", "Roll Right", O::kLSRight, "Numpad6"},
    {kAircraft, "pitch_down", "Pitch Down", O::kLSUp, "Numpad8"},
    {kAircraft, "pitch_up", "Pitch Up", O::kLSDown, "Numpad5"},
    {kAircraft, "attack", "Fly Attack", O::kA, "RMB"},
    {kAircraft, "landing_gear", "Landing Gear", O::kL3, "G"},
    {kAircraft, "vtol", "Vertical Flight Mode / Bomb Bay", O::kDRight, "E"},
    {kAircraft, "duck", "Duck", O::kA, "X"},
    {kAircraft, "exit", "Exit Vehicle", O::kY, "F"},
    {kAircraft, "attack_camera", "Weapon Camera", O::kR3, "Insert"},
    {kAircraft, "target_left", "Select Target Left", O::kLB, "Numpad7"},
    {kAircraft, "target_right", "Select Target Right", O::kRB, "Numpad9"},
    {kAircraft, "cinematic", "Cinematic Camera", O::kB, "R"},
    {kAircraft, "next_weapon", "Next Aircraft Weapon", O::kAircraftWeaponNext, "WheelUp"},
    {kAircraft, "special", "Special Ability", O::kBothSticks, "CapsLock"},
    // Submarine
    {kSub, "throttle_up", "Throttle Up", O::kRT, "W"},
    {kSub, "throttle_down", "Throttle Down", O::kLT, "S"},
    {kSub, "turn_hard_left", "Turn Hard Left", O::kLB, "A"},
    {kSub, "turn_hard_right", "Turn Hard Right", O::kRB, "D"},
    {kSub, "turn_left", "Turn Left", O::kLSLeft, "Numpad4"},
    {kSub, "turn_right", "Turn Right", O::kLSRight, "Numpad6"},
    {kSub, "pitch_down", "Pitch Down", O::kLSUp, "Numpad8"},
    {kSub, "pitch_up", "Pitch Up", O::kLSDown, "Numpad5"},
    {kSub, "ascend", "Ascend", O::kX, "Shift"},
    {kSub, "descend", "Descend", O::kA, "Control"},
    {kSub, "exit", "Exit Vehicle", O::kY, "F"},
    {kSub, "cinematic", "Cinematic Camera", O::kB, "R"},
    // Parachute
    {kParachute, "pitch_down", "Pitch Down", O::kLSUp, "W"},
    {kParachute, "pitch_up", "Pitch Up", O::kLSDown, "S"},
    {kParachute, "turn_left", "Turn Left", O::kLSLeft, "A"},
    {kParachute, "turn_right", "Turn Right", O::kLSRight, "D"},
    {kParachute, "deploy", "Deploy / Detach Parachute", O::kY, "F,LMB"},
    {kParachute, "brake_left", "Brake Left", O::kLB, "Q"},
    {kParachute, "brake_right", "Brake Right", O::kRB, "E"},
    {kParachute, "smoke", "Parachute Smoke", O::kA, "X"},
    // Pause menu (frontend)
    {kMenu, "up", "Up", O::kDUp, "Up"},
    {kMenu, "down", "Down", O::kDDown, "Down"},
    {kMenu, "left", "Left", O::kDLeft, "Left"},
    {kMenu, "right", "Right", O::kDRight, "Right"},
    {kMenu, "accept", "Accept", O::kA, "Return,LMB"},
    {kMenu, "cancel", "Back", O::kB, "Backspace,Escape"},
    {kMenu, "x", "Option (X)", O::kX, "Space"},
    {kMenu, "y", "Option (Y)", O::kY, "Tab"},
    {kMenu, "tab_left", "Previous Tab", O::kLB, "Q"},
    {kMenu, "tab_right", "Next Tab", O::kRB, "E"},
    {kMenu, "lt", "Map Zoom Out", O::kLT, "PageDown"},
    {kMenu, "rt", "Map Zoom In", O::kRT, "PageUp"},
    {kMenu, "pan_up", "Map Up", O::kLSUp, "W"},
    {kMenu, "pan_down", "Map Down", O::kLSDown, "S"},
    {kMenu, "pan_left", "Map Left", O::kLSLeft, "A"},
    {kMenu, "pan_right", "Map Right", O::kLSRight, "D"},
    {kMenu, "ls", "Left Stick Press", O::kL3, "Shift"},
    {kMenu, "rs", "Right Stick Press", O::kR3, "Control"},
    {kMenu, "select", "Select", O::kBack, "CapsLock,Home"},
    {kMenu, "scroll_up", "Scroll Up", O::kScrollUp, "WheelUp"},
    {kMenu, "scroll_down", "Scroll Down", O::kScrollDown, "WheelDown"},
    {kMenu, "close", "Close Pause Menu", O::kStart, "P"},
};
constexpr size_t kActionCount = std::size(kActions);

struct Group {
  const char* title;
  Table table;
};
const Group kGroups[] = {
    {"General", kShared},           {"On Foot", kFoot},       {"Vehicle (car, motorbike, boat)", kVehicle},
    {"Bicycle", kBicycle},          {"Aircraft", kAircraft},  {"Submarine", kSub},
    {"Parachute", kParachute},      {"Pause Menu", kMenu},
};

// PC Settings > Keyboard/Mouse, mouse part.
struct MouseOptions {
  float foot = 1.0f, driving = 1.0f, flying = 1.0f, sub = 1.0f;
  bool invert = false, invert_flight = false, invert_sub = false;
};

const char* kPadNames[rex::input::kPadControlCount] = {"A", "B", "X", "Y", "LB", "RB", "L3", "R3", "Back", "Start",
                                                       "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right",
                                                       "LT", "RT"};

std::mutex g_mutex;
std::array<std::string, kActionCount> g_keys;  // "primary,secondary"
MouseOptions g_mouse;
std::array<uint8_t, rex::input::kPadControlCount> g_pad;
std::atomic<int> g_capture_action{-1};
std::atomic<int> g_capture_slot{0};

std::filesystem::path IniPath() { return ExeDir() / "controls.ini"; }

std::pair<std::string, std::string> Split(const std::string& keys) {
  const size_t comma = keys.find(',');
  if (comma == std::string::npos) return {keys, ""};
  return {keys.substr(0, comma), keys.substr(comma + 1)};
}

void ResetKeys() {
  for (size_t i = 0; i < kActionCount; ++i) g_keys[i] = kActions[i].defaults;
}
void ResetMouse() { g_mouse = MouseOptions{}; }
void ResetPad() {
  for (int i = 0; i < rex::input::kPadControlCount; ++i) g_pad[i] = uint8_t(i);
}

void Apply() {  // g_mutex held
  std::vector<std::pair<std::string, O>> tables[10];
  for (size_t i = 0; i < kActionCount; ++i)
    if (!g_keys[i].empty()) tables[kActions[i].table].push_back({g_keys[i], kActions[i].out});
  for (int t = 0; t < 10; ++t) {
    if (t == 2 || t == 5) continue;  // motorbike and boat use the vehicle bindings
    rex::input::mnk::SetContextBindings(t, tables[t]);
  }
  rex::input::mnk::SetContextBindings(2, tables[kVehicle]);
  rex::input::mnk::SetContextBindings(5, tables[kVehicle]);
  using C = InputContext;
  rex::input::mnk::SetMouseOptions(C::kOnFoot, g_mouse.foot, g_mouse.invert);
  rex::input::mnk::SetMouseOptions(C::kParachute, g_mouse.foot, g_mouse.invert);
  for (C c : {C::kCar, C::kBike, C::kBicycle, C::kBoat}) rex::input::mnk::SetMouseOptions(c, g_mouse.driving, g_mouse.invert);
  rex::input::mnk::SetMouseOptions(C::kAircraft, g_mouse.flying, g_mouse.invert_flight);
  rex::input::mnk::SetMouseOptions(C::kSub, g_mouse.sub, g_mouse.invert_sub);
  rex::input::mnk::SetMouseOptions(C::kFrontend, 1.0f, false);
  rex::input::SetPadRemap(g_pad);
}

void Save() {  // g_mutex held
  std::ofstream out(IniPath());
  out << "# GTA V controls (F1 menu > Controls). keys: primary,secondary (empty = unbound).\n";
  for (size_t i = 0; i < kActionCount; ++i)
    out << "key." << int(kActions[i].table) << '.' << kActions[i].id << '=' << g_keys[i] << '\n';
  out << "mouse.foot_scale=" << g_mouse.foot << "\nmouse.driving_scale=" << g_mouse.driving
      << "\nmouse.flying_scale=" << g_mouse.flying << "\nmouse.sub_scale=" << g_mouse.sub
      << "\nmouse.invert=" << g_mouse.invert << "\nmouse.invert_flight=" << g_mouse.invert_flight
      << "\nmouse.invert_sub=" << g_mouse.invert_sub << '\n';
  for (int i = 0; i < rex::input::kPadControlCount; ++i) out << "pad." << i << '=' << int(g_pad[i]) << '\n';
}

void Changed() {  // g_mutex held
  Apply();
  Save();
}

}  // namespace

void Load() {
  std::lock_guard lock(g_mutex);
  ResetKeys();
  ResetMouse();
  ResetPad();
  std::ifstream in(IniPath());
  std::string line;
  while (in && std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string name = line.substr(0, eq), value = line.substr(eq + 1);
    if (name.rfind("key.", 0) == 0) {
      for (size_t i = 0; i < kActionCount; ++i)
        if (name == "key." + std::to_string(int(kActions[i].table)) + "." + kActions[i].id) g_keys[i] = value;
    } else if (name.rfind("pad.", 0) == 0) {
      const int i = std::atoi(name.c_str() + 4), v = std::atoi(value.c_str());
      if (i >= 0 && i < rex::input::kPadControlCount && v >= 0 && v < rex::input::kPadControlCount) g_pad[i] = uint8_t(v);
    } else {
      const float f = float(std::atof(value.c_str()));
      if (name == "mouse.foot_scale") g_mouse.foot = f;
      if (name == "mouse.driving_scale") g_mouse.driving = f;
      if (name == "mouse.flying_scale") g_mouse.flying = f;
      if (name == "mouse.sub_scale") g_mouse.sub = f;
      if (name == "mouse.invert") g_mouse.invert = value == "1";
      if (name == "mouse.invert_flight") g_mouse.invert_flight = value == "1";
      if (name == "mouse.invert_sub") g_mouse.invert_sub = value == "1";
    }
  }
  Apply();
  REXLOG_INFO("[controls] {} key bindings{}", kActionCount, in ? " (controls.ini)" : " (PC defaults)");
}

bool Capturing() { return g_capture_action.load() >= 0; }
void CancelCapture() { g_capture_action = -1; }

void OnCaptureKey(const std::string& name) {
  const int a = g_capture_action.exchange(-1);
  if (a < 0 || name.empty()) return;
  std::lock_guard lock(g_mutex);
  auto [primary, secondary] = Split(g_keys[a]);
  (g_capture_slot == 0 ? primary : secondary) = name;
  g_keys[a] = secondary.empty() ? primary : primary + "," + secondary;
  Changed();
}

void DrawWindow(bool* open, float scale) {
  ImGui::SetNextWindowSize(ImVec2(760.0f * scale, 620.0f * scale), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Controls", open, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }
  std::lock_guard lock(g_mutex);
  if (ImGui::BeginTabBar("controls_tabs")) {
    if (ImGui::BeginTabItem("Keyboard / Mouse")) {
      if (ImGui::Button("Restore PC defaults")) {
        ResetKeys();
        Changed();
      }
      ImGui::SameLine();
      ImGui::TextDisabled(Capturing() ? "Press a key or mouse button (Esc cancels)..." : "Click a key to change it.");
      ImGui::BeginChild("keys", ImVec2(0, 0), false);
      for (const Group& g : kGroups) {
        if (!ImGui::CollapsingHeader(g.title, g.table == kShared || g.table == kFoot ? ImGuiTreeNodeFlags_DefaultOpen : 0))
          continue;
        if (ImGui::BeginTable(g.title, 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
          for (size_t i = 0; i < kActionCount; ++i) {
            if (kActions[i].table != g.table) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kActions[i].name);
            auto [primary, secondary] = Split(g_keys[i]);
            for (int slot = 0; slot < 2; ++slot) {
              ImGui::TableNextColumn();
              ImGui::PushID(int(i * 2 + slot));
              const bool waiting = g_capture_action == int(i) && g_capture_slot == slot;
              const std::string& key = slot == 0 ? primary : secondary;
              if (ImGui::Button(waiting ? "..." : key.empty() ? "-" : key.c_str(), ImVec2(150.0f * scale, 0))) {
                g_capture_slot = slot;
                g_capture_action = int(i);
              }
              if (ImGui::BeginPopupContextItem("clear")) {  // right click: unbind
                if (ImGui::MenuItem("Clear")) {
                  (slot == 0 ? primary : secondary).clear();
                  g_keys[i] = secondary.empty() ? primary : primary.empty() ? secondary : primary + "," + secondary;
                  Changed();
                }
                ImGui::EndPopup();
              }
              ImGui::PopID();
            }
          }
          ImGui::EndTable();
        }
      }
      if (ImGui::CollapsingHeader("Mouse", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool changed = false;
        changed |= ImGui::Checkbox("Invert Mouse", &g_mouse.invert);
        changed |= ImGui::Checkbox("Invert Mouse Flight", &g_mouse.invert_flight);
        changed |= ImGui::Checkbox("Invert Mouse Sub", &g_mouse.invert_sub);
        ImGui::SetNextItemWidth(260.0f * scale);
        changed |= ImGui::SliderFloat("Mouse On-Foot Scale", &g_mouse.foot, 0.1f, 3.0f, "%.2f");
        ImGui::SetNextItemWidth(260.0f * scale);
        changed |= ImGui::SliderFloat("Mouse Driving Scale", &g_mouse.driving, 0.1f, 3.0f, "%.2f");
        ImGui::SetNextItemWidth(260.0f * scale);
        changed |= ImGui::SliderFloat("Mouse Flying Scale", &g_mouse.flying, 0.1f, 3.0f, "%.2f");
        ImGui::SetNextItemWidth(260.0f * scale);
        changed |= ImGui::SliderFloat("Mouse Sub Scale", &g_mouse.sub, 0.1f, 3.0f, "%.2f");
        if (ImGui::Button("Restore mouse defaults")) {
          ResetMouse();
          changed = true;
        }
        if (changed) Changed();
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Controller")) {
      ImGui::TextWrapped("Give any button or trigger another one's function. Layout, targeting mode, "
                         "vibration, invert look and sensitivity are in the game's own Settings > Controls.");
      if (ImGui::Button("Restore controller defaults")) {
        ResetPad();
        Changed();
      }
      if (ImGui::BeginTable("pad", 2, ImGuiTableFlags_RowBg)) {
        for (int i = 0; i < rex::input::kPadControlCount; ++i) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(kPadNames[i]);
          ImGui::TableNextColumn();
          int to = g_pad[i];
          ImGui::PushID(i);
          ImGui::SetNextItemWidth(200.0f * scale);
          if (ImGui::Combo("##to", &to, kPadNames, rex::input::kPadControlCount)) {
            g_pad[i] = uint8_t(to);
            Changed();
          }
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

}  // namespace gtav::native::controls
