// gtav launcher - first-run install: the player's own discs and title update in, a playable
// folder out. Nothing retail ships with the launcher; everything below data\ and gtav_game.dll is
// made here from the player's files.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace loader {

struct InstallInputs {
  std::wstring disc1;  // Disc 1 (Install): ISO image or extracted folder
  std::wstring disc2;  // Disc 2 (Play): ISO image or extracted folder
  std::wstring tu;     // Title Update 27: the STFS package file or an extracted folder
};

// Callbacks into the window; called from the install thread.
class InstallUi {
 public:
  virtual ~InstallUi() = default;
  virtual void Stage(const std::wstring& text) = 0;   // one line: what is happening now
  virtual void Log(const std::wstring& text) = 0;     // appended to the log box and install.log
  virtual void Progress(double fraction) = 0;         // 0..1 for the whole install
  virtual bool Cancelled() = 0;
  virtual bool Ask(const std::wstring& question) = 0;  // yes/no, blocks the install thread
};

// Game data only (iPhone setup screen): Disc 2 files and TU27 into home/data/{game,update}, the
// Disc 1 install partitions into home/data/user, each checked against `expected` (expected.txt).
bool RunDataInstall(const std::filesystem::path& home, const std::filesystem::path& expected,
                    const InstallInputs& in, InstallUi& ui, std::wstring* error);
// Game files copied from a PC install into home/data/{game,update,user} (Android, instead of the
// discs): checks they are all there (sizes; the executable and patch hashed) and marks them installed.
bool AdoptCopiedData(const std::filesystem::path& home, const std::filesystem::path& expected, InstallUi& ui,
                     std::wstring* error);
bool IsDataInstalled(const std::filesystem::path& home);
// iPhone, on any desktop system: from the installed game, <home>/iphone/GTAV.ipa (the package's iOS
// shell with gtav_game.dylib built for iOS and the shaders translated for it; no game files: the
// phone installs those from the player's discs). macOS: root is the package, home the game folder;
// Windows / Linux: both the launcher's folder.
bool RunIphoneBuild(const std::filesystem::path& root, const std::filesystem::path& home, InstallUi& ui,
                    std::wstring* error);
// The Android build, from any desktop system: libgtav_game.so (aarch64 Android, against the APK's
// libgtav_fast.so), the shaders for Adreno and expected.txt in <home>/android/gtav-android-pack.zip.
// The APK (no game code) imports the pack and installs the game files from the discs itself.
bool RunAndroidBuild(const std::filesystem::path& root, const std::filesystem::path& home, InstallUi& ui,
                     std::wstring* error);

// root = the folder holding the launcher, gtav_fast.exe and tools\ (Linux: gtav_fast, tools/).
bool IsInstalled(const std::filesystem::path& root);
bool RunInstall(const std::filesystem::path& root, const InstallInputs& in, InstallUi& ui,
                std::wstring* error);
// macOS: root is the read-only package inside the app, home the game folder everything made goes
// to (gtav_game.dylib, data/). IsInstalled and LaunchGame then take home.
bool RunInstall(const std::filesystem::path& root, const std::filesystem::path& home,
                const InstallInputs& in, InstallUi& ui, std::wstring* error);
// extra: more game arguments (Linux: everything after "--" on the launcher's command line). On
// Linux the launcher process becomes the game, so this returns only on an error. macOS starts the
// game as a separate process (root = the game folder) and returns.
bool LaunchGame(const std::filesystem::path& root, std::wstring* error,
                const std::vector<std::wstring>& extra = {});
// Checks that can fail before anything is touched (CPU, runtime DLLs, bundled tools).
bool Preflight(const std::filesystem::path& root, std::wstring* error);
#ifdef __APPLE__
// The game folder can be created and is on a case-insensitive volume.
bool CheckGameFolder(const std::filesystem::path& home, std::wstring* error);
// Copies gtav_fast, the dylibs, vulkan/ and the shader tools (tools/shaders/* -> tools/*) from the
// package into the game folder when they changed. Run before every launch.
bool SyncRuntime(const std::filesystem::path& root, const std::filesystem::path& home,
                 std::wstring* error);
#endif

}  // namespace loader
