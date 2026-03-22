/*
 * HFS Browser Library - Unified volume interface for HFS and HFS+
 * No UI dependencies.
 */

#ifndef HFSBROWSE_VOLUME_H
#define HFSBROWSE_VOLUME_H

#include "entry.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace hfsbrowse {

enum class VolumeType { NONE, HFS, HFSPLUS };

class Volume {
public:
    virtual ~Volume() = default;

    virtual VolumeType type() const = 0;
    virtual std::string name() const = 0;
    virtual uint64_t total_bytes() const = 0;
    virtual uint64_t free_bytes() const = 0;
    virtual uint32_t blessed_folder() const = 0;
    virtual bool is_readonly() const = 0;

    // Refresh cached volume stats (size, free space, blessed folder)
    virtual void refresh_stats() = 0;

    // Directory listing by folder CNID (HFS+ native; HFS returns empty)
    virtual std::vector<HBEntry> list_dir(uint32_t folder_cnid) = 0;

    // Directory listing by Mac-style path (HFS native; HFS+ uses path lookup)
    virtual std::vector<HBEntry> list_dir_by_path(const std::string& mac_path) = 0;

    // File data reading by CNID (HFS+ native; HFS returns empty)
    virtual std::vector<uint8_t> read_fork(uint32_t cnid, uint32_t parent_cnid, int fork) = 0;

    // File data reading by Mac-style path (HFS native; HFS+ uses path lookup)
    virtual std::vector<uint8_t> read_fork_by_path(const std::string& mac_path, int fork) = 0;

    // File writing
    virtual int write_file(const std::string& mac_path, const uint8_t* data, size_t size) = 0;
    virtual int write_rsrc_fork(const std::string& mac_path, const uint8_t* data, size_t size) = 0;

    // Create file with type/creator (HFS-specific; HFS+ ignores type/creator here)
    virtual int create_file(const std::string& mac_path, const char* type, const char* creator,
                            const uint8_t* data, size_t size) = 0;

    // Directory operations
    virtual int mkdir(const std::string& mac_path) = 0;
    virtual int rmdir(const std::string& mac_path) = 0;
    virtual int delete_file(const std::string& mac_path) = 0;
    virtual int rename(const std::string& old_path, const std::string& new_path) = 0;

    // HFS-specific: change current directory for relative path operations
    virtual int chdir(const std::string& mac_path) = 0;

    // Metadata
    virtual int set_type_creator(const std::string& mac_path, const char* type, const char* creator) = 0;
    virtual int set_finder_flags(const std::string& mac_path, int16_t flags) = 0;
    virtual int set_blessed(uint32_t folder_cnid) = 0;

    // Get file stat (for HFS: hfsdirent; returns fdflags, type, creator)
    virtual int get_finder_info(const std::string& mac_path, char* type, char* creator, int16_t* flags) = 0;

    // Check volume integrity
    virtual std::string check() = 0;
};

// Factory: create Volume from raw hfsvol* (takes ownership, will call hfs_umount)
std::unique_ptr<Volume> make_hfs_volume(void* hfsvol_ptr, bool readonly);

// Factory: create Volume from raw HFSPlusVolume* (takes ownership, will call hfsplus_close)
std::unique_ptr<Volume> make_hfsplus_volume(void* hfsplus_ptr, bool readonly);

} // namespace hfsbrowse

#endif // HFSBROWSE_VOLUME_H
