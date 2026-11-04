// gtav - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/input/input_system.h>
#include <rex/rex_app.h>

#include "native/menu.h"
#include "native/controls.h"
#include "pc_controls.h"
#include "native/touch_controls.h"
#include <rex/system/kernel_state.h>
#include <rex/ui/presenter.h>
#include <rex/runtime.h>
#include <rex/ui/windowed_app_context.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
#include "ios_setup.h"
#endif

#include <atomic>
#include <functional>
#include <optional>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

class GtavApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<GtavApp>(new GtavApp(ctx, "gtav",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // Adds the on-screen gamepad (gtav_touch_controls) next to the SDK's pad and keyboard drivers.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    // gtav_settings.ini (Esc menu) before the runtime starts: the language (read by the game at
    // boot) and the stereo mix (audio device opened at setup) apply from it. The renderer loads
    // it again when it starts.
    gtav::native::menu::LoadSettings();
    gtav::native::controls::Load();  // key bindings, mouse options, controller remap (controls.ini)
    config.input_factory = [](bool tool_mode) -> std::unique_ptr<rex::system::IInputSystem> {
      auto input = rex::input::CreateDefaultInputSystem(tool_mode);
      if (!tool_mode) input->AddDriver(gtav::native::touch::CreateDriver());
      return input;
    };
  }
  // void OnLoadXexImage(std::string& xex_image) override {}
  // GTAV_DUMP_IMAGE=<path>: write the loaded (title-update patched) guest image and exit.
  // Used to rescan the patched code for entry points the analyzer misses.
  void OnPostLoadXexImage() override {
    const char* path = std::getenv("GTAV_DUMP_IMAGE");
    if (!path) return;
    auto* image = REX_KERNEL_MEMORY()->TranslateVirtual(0x82000000);
    if (FILE* f = std::fopen(path, "wb")) {
      std::fwrite(image, 1, 0x1DC0000, f);
      std::fclose(f);
    }
    std::_Exit(0);
  }
  // GTAV_SHOT_DIR=<dir> [GTAV_SHOT_SECS=<n>, default 30]: every n seconds save the guest
  // output as <dir>/shot_<seconds>.ppm. For checking unattended boots (no screen capture needed).
  void OnPostSetup() override {
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    gtav::StartVehicleContextPoll();  // keyboard/mouse: on-foot or vehicle layout (PC controls)
#endif
#ifdef __ANDROID__
    // The settings menu (controller: RB + Back) takes the pad while open: the game sees no input.
    if (auto* input = static_cast<rex::input::InputSystem*>(runtime() ? runtime()->input_system() : nullptr))
      input->SetActiveCallback([] { return !gtav::native::menu::IsOpen(); });
#endif
    const char* dir = std::getenv("GTAV_SHOT_DIR");
    if (!dir) return;
    const char* secs_env = std::getenv("GTAV_SHOT_SECS");
    int secs = secs_env ? std::atoi(secs_env) : 30;
    if (secs <= 0) secs = 30;
    std::thread([this, dir = std::string(dir), secs] {
      for (int t = secs;; t += secs) {
        std::this_thread::sleep_for(std::chrono::seconds(secs));
        if (shutting_down_) return;
        app_context().CallInUIThreadSynchronous([this, &dir, t] {
          auto* gs = runtime() ? runtime()->graphics_system() : nullptr;
          auto* presenter = gs ? gs->presenter() : nullptr;
          rex::ui::RawImage image;
          if (!presenter || !presenter->CaptureGuestOutput(image)) return;
          std::string path = dir + "/shot_" + std::to_string(t) + ".ppm";
          if (FILE* f = std::fopen(path.c_str(), "wb")) {
            std::fprintf(f, "P6\n%u %u\n255\n", image.width, image.height);
            for (uint32_t y = 0; y < image.height; ++y) {
              const uint8_t* row = image.data.data() + y * image.stride;
              for (uint32_t x = 0; x < image.width; ++x) std::fwrite(row + x * 4, 1, 3, f);
            }
            std::fclose(f);
          }
        });
      }
    }).detach();
  }
  void OnShutdown() override { shutting_down_ = true; }
#if defined(__APPLE__) && TARGET_OS_IPHONE
  // iPhone release: the game data is installed on the phone by the setup screen (ios_setup.h) into
  // Documents/data, the Mac install's layout. A dev build passes its own roots in args.txt.
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    if (!defaults.game_data_root.empty()) return defaults;
    rex::PathConfig paths = defaults;
    const auto data = gtav::ios_setup::DataRoot();
    paths.game_data_root = data / "game";
    paths.update_data_root = data / "update";
    paths.user_data_root = data / "user";
    // As the SDK derives it from --user_data_root (user/cache); its default under $HOME/.local is
    // outside the sandbox's writable Documents (create_directories threw: the app aborted).
    paths.cache_root = data / "user" / "cache";
    if (gtav::ios_setup::DataInstalled()) return paths;
    gtav::ios_setup::Show([paths, resume] { resume(paths); });
    return std::nullopt;
  }
#endif
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnConfigurePaths(rex::PathConfig& paths) override {}

 private:
  std::atomic<bool> shutting_down_{false};
};
