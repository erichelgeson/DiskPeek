/*
 * Platform abstraction for cross-platform compatibility
 * Handles differences between Linux, macOS, and Windows
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <cstdint>
#include <string>

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  include <direct.h>
#  define platform_mkdir(path) _mkdir(path)
#else
#  include <sys/stat.h>
#  include <sys/statvfs.h>
#  define platform_mkdir(path) mkdir(path, 0755)
#endif

// Get free disk space in bytes at the given path.
// Returns 0 on failure.
static inline uint64_t platform_free_space(const char* path) {
#ifdef _WIN32
    ULARGE_INTEGER free_bytes;
    if (GetDiskFreeSpaceExA(path, &free_bytes, nullptr, nullptr))
        return free_bytes.QuadPart;
    return 0;
#else
    struct statvfs vfs;
    if (statvfs(path, &vfs) == 0)
        return (uint64_t)vfs.f_bavail * vfs.f_frsize;
    return 0;
#endif
}

#endif // PLATFORM_H
