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

// --- Extended attribute support ---
// Used to store resource forks and Finder info natively when the host
// filesystem supports it (macOS HFS+/APFS, Linux ext4/btrfs/xfs).

#ifdef _WIN32
// Windows NTFS has alternate data streams but we don't use them yet
static inline bool platform_has_xattr() { return false; }
static inline int platform_setxattr(const char*, const char*, const void*, size_t) { return -1; }
static inline int platform_getxattr(const char*, const char*, void*, size_t) { return -1; }
#elif defined(__APPLE__)
#include <sys/xattr.h>
static inline bool platform_has_xattr() { return true; }
static inline int platform_setxattr(const char* path, const char* name,
                                     const void* value, size_t size) {
    return setxattr(path, name, value, size, 0, 0);
}
static inline int platform_getxattr(const char* path, const char* name,
                                     void* value, size_t size) {
    ssize_t r = getxattr(path, name, value, size, 0, 0);
    return (r >= 0) ? (int)r : -1;
}
#else
// Linux
#include <sys/xattr.h>
static inline bool platform_has_xattr() {
    // Probe at runtime — try to list xattrs on /tmp
    // (cheaper than actually setting one)
    return true; // most modern Linux filesystems support user.* xattrs
}
static inline int platform_setxattr(const char* path, const char* name,
                                     const void* value, size_t size) {
    // Linux requires "user." prefix for user namespace xattrs
    // But com.apple.* is conventional, so prepend user. if needed
    std::string lname = name;
    if (lname.find("user.") != 0)
        lname = "user." + lname;
    return setxattr(path, lname.c_str(), value, size, 0);
}
static inline int platform_getxattr(const char* path, const char* name,
                                     void* value, size_t size) {
    std::string lname = name;
    if (lname.find("user.") != 0)
        lname = "user." + lname;
    ssize_t r = getxattr(path, lname.c_str(), value, size);
    return (r >= 0) ? (int)r : -1;
}
#endif

// Try to write resource fork + Finder info as xattrs.
// Returns true if successful (xattrs supported and written).
static inline bool platform_write_xattr_forkinfo(
        const char* path,
        const char* type, const char* creator, short fdflags,
        const uint8_t* rsrc_data, size_t rsrc_size) {
    if (!platform_has_xattr()) return false;

    // Build 32-byte FinderInfo: type(4) + creator(4) + flags(2) + location(4) + folder(2) + extended(16)
    uint8_t fi[32] = {};
    if (type && strlen(type) >= 4) memcpy(fi, type, 4);
    if (creator && strlen(creator) >= 4) memcpy(fi + 4, creator, 4);
    fi[8] = (uint8_t)((fdflags >> 8) & 0xFF);
    fi[9] = (uint8_t)(fdflags & 0xFF);

    bool has_fi = (type && type[0] && strcmp(type, "????") != 0);
    bool has_rsrc = (rsrc_data && rsrc_size > 0);

    if (!has_fi && !has_rsrc) return false;

    bool ok = true;
    if (has_fi) {
        if (platform_setxattr(path, "com.apple.FinderInfo", fi, 32) != 0)
            ok = false;
    }
    if (has_rsrc) {
        if (platform_setxattr(path, "com.apple.ResourceFork", rsrc_data, rsrc_size) != 0)
            ok = false;
    }
    return ok;
}

#endif // PLATFORM_H
