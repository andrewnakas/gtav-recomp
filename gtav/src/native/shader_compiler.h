// gtav - background compiler for shaders without precompiled SPIR-V (see shader_compiler.cpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace gtav::native {

// Called for every shader the game creates; queues the ones without SPIR-V.
void OnShaderCreated(const uint8_t* container, size_t size, uint64_t hash, bool pixel);

// Bumped whenever a new SPIR-V file landed: cached misses should be looked up again.
uint64_t CompileGeneration();

// The background compiler found or wrote <name>.spv (no disk access).
bool ShaderOnDisk(const std::string& name);

// Render thread: <name> (e.g. "ps_<HASH>") was created by the game but its SPIR-V is still being
// made: move it to the front of the queue and wait for it (up to timeout_ms). True once the .spv is
// on disk; false at once for shaders the compiler never saw or that failed.
// A first launch compiles every shader while the game boots; skipping draws then lost the ones the
// game makes only once (lighting stayed black for the whole session: issues #4, #10).
bool WaitForShader(const std::string& name, int timeout_ms);

// Shaders queued or compiling (the first-launch status line).
uint32_t PendingShaders();

}  // namespace gtav::native
