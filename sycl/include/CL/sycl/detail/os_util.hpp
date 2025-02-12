//===-- os_util.hpp - OS utilities -----------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Abstracts the operating system services.

#pragma once

#include <cstdint>
#include <stdlib.h>

#ifdef _WIN32
#define SYCL_RT_OS_WINDOWS
// Windows platform
#ifdef _WIN64
// 64-bit Windows platform
#else
// 32-bit Windows platform
#endif // _WIN64
#elif __linux__
// Linux platform
#define SYCL_RT_OS_LINUX
#else
#error "Unsupported compiler or OS"
#endif // _WIN32

#if defined(SYCL_RT_OS_WINDOWS)

#define DLL_LOCAL
// If SYCL headers are included to build SYCL library then the macro is used
// to set dllexport attribute for global variables/functions/classes.
// Otherwise, the macro is used used to set dllimport for the same global
// variables/functions/classes.
#if defined(__SYCL_BUILD_SYCL_DLL)
#define __SYCL_EXPORTED __declspec(dllexport)
#else
#define __SYCL_EXPORTED __declspec(dllimport)
#endif

#elif defined(SYCL_RT_OS_LINUX)

#define DLL_LOCAL __attribute__((visibility("hidden")))
#define __SYCL_EXPORTED

#endif

namespace cl {
namespace sycl {
namespace detail {

/// Uniquely identifies an operating system module (executable or a dynamic
/// library)
using OSModuleHandle = intptr_t;

/// Groups the OS-dependent services.
class OSUtil {
public:
  /// Returns a module enclosing given address or nullptr.
  static OSModuleHandle getOSModuleHandle(const void *VirtAddr);

  /// Module handle for the executable module - it is assumed there is always
  /// single one at most.
  static constexpr OSModuleHandle ExeModuleHandle = -1;

  /// Returns the amount of RAM available for the operating system.
  static size_t getOSMemSize();

  /// Allocates \p NumBytes bytes of uninitialized storage whose alignment
  /// is specified by \p Alignment.
  static void *alignedAlloc(size_t Alignment, size_t NumBytes);

  /// Deallocates the memory referenced by \p Ptr.
  static void alignedFree(void *Ptr);
};

} // namespace detail
} // namespace sycl
} // namespace cl
