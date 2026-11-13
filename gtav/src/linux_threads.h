// gtav - Linux desktop thread placement (--gtav_linux_pin). See linux_threads.cpp.
#pragma once

namespace gtav::linux_threads {

enum Kind { kMain = 0, kD3D, kRender, kCommandProcessor, kKinds };

// Called by the thread itself (cheap after its first call): remembers the thread id of this
// kind of hot thread so the placer can pin it. A no-op on other platforms.
void Note(Kind kind);

// CPU list ("8-11") for background work (shader compiler processes) when pinning is on, else "".
const char* BackgroundCpuList();

}  // namespace gtav::linux_threads
