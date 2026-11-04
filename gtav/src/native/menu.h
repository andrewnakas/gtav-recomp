// Settings menu of the native renderer: Esc opens it over the game (Dear ImGui, drawn by the
// backend into the swap chain). Input arrives from the window thread (SDL event watch / WndProc)
// and is queued; the render thread builds the UI once per presented frame.
#pragma once

#include <cstdint>
#include <string>

struct ImDrawData;

namespace gtav::native::menu {

// Window thread. Coordinates are window pixels; buttons 0 left, 1 right, 2 middle.
void OnEscape();
bool IsOpen();
void OnMouseMove(float x, float y);
void OnMouseButton(int button, bool down);
void OnMouseWheel(float dy);
enum class Key { kUp, kDown, kLeft, kRight, kEnter, kSpace, kTab };
void OnKey(Key key, bool down);
// F11 / Alt+Enter: flips gtav_fullscreen and saves it (the renderer applies it next frame).
void ToggleFullscreen();

// Applies gtav_settings.ini (next to the exe) over the command line. Once, before the first frame.
void LoadSettings();

// Render thread: sets one of the menu's settings as its controls do (logs/menu_set test hook).
void TestSet(const std::string& name, const std::string& value);

// Render thread. Builds the menu for a display of w x h pixels when it is open; null when closed.
// `fps` is the presented rate over the last second, `frame_ms` the average time per frame.
// `pending_shaders`: shaders still compiling (first launch); shown as a status line while non-zero.
ImDrawData* Frame(uint32_t w, uint32_t h, uint32_t fps, double frame_ms, uint32_t pending_shaders = 0);

// The font atlas (RGBA8) for the backend's upload; true once, the first time the menu opens.
bool TakeFontAtlas(const uint8_t** pixels, int* width, int* height);
void SetFontTexture(uint32_t descriptor);

}  // namespace gtav::native::menu
