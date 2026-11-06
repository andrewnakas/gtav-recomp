#pragma once
// Copies memory that may be unmapped without faulting (false if any of it is unreadable):
// process_vm_readv on Linux/Android, mach_vm_read_overwrite on macOS, ReadProcessMemory on Windows.

#include <cstddef>
#include <cstdint>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <mach/mach.h>
#if !TARGET_OS_IPHONE
#include <mach/mach_vm.h>
#endif
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace gtav::native {
inline bool SafeReadMemory(void* dst, const void* src, size_t size) {
#if defined(_WIN32)
  SIZE_T got = 0;
  return ReadProcessMemory(GetCurrentProcess(), src, dst, size, &got) && got == size;
#elif defined(__APPLE__) && TARGET_OS_IPHONE
  // mach_vm.h is unsupported in the iOS SDK; vm_read_overwrite is the same call.
  vm_size_t got = 0;
  return vm_read_overwrite(mach_task_self(), vm_address_t(uintptr_t(src)), size,
                           vm_address_t(uintptr_t(dst)), &got) == KERN_SUCCESS &&
         got == size;
#elif defined(__APPLE__)
  mach_vm_size_t got = 0;
  return mach_vm_read_overwrite(mach_task_self(), mach_vm_address_t(uintptr_t(src)), size,
                                mach_vm_address_t(uintptr_t(dst)), &got) == KERN_SUCCESS &&
         got == size;
#else
  iovec local{dst, size}, remote{const_cast<void*>(src), size};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == ssize_t(size);
#endif
}
}  // namespace gtav::native
