// gtav - remappable controls: keyboard/mouse (GTA V PC actions and defaults, per context),
// PC mouse options, and controller button remapping. Saved to controls.ini next to the exe.
#pragma once

#include <string>

namespace gtav::native::controls {

// Reads controls.ini (or the PC defaults) and hands everything to the input drivers. Once at start.
void Load();

// Esc menu: the Controls window, while *open. Render thread, inside the menu's ImGui frame.
void DrawWindow(bool* open, float scale);

// Key capture for rebinding ("press a key"): window thread asks, then delivers the key's bind name
// ("Shift", "LMB", "WheelUp", ...) or cancels (Esc).
bool Capturing();
void OnCaptureKey(const std::string& name);
void CancelCapture();

}  // namespace gtav::native::controls
