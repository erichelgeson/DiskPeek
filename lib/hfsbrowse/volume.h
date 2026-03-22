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
#include <functional>

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

    // Directory listing by folder CNID (2 = root)
    virtual std::vector<HBEntry> list_dir(uint32_t folder_cnid) = 0;

    // File data reading
    virtual std::vector<uint8_t> read_fork(uint32_t cnid, uint32_t parent_cnid, int fork) = 0;

    // File writing
    virtual int write_file(const std::string& mac_path, const uint8_t* data, size_t size) = 0;
    virtual int write_rsrc_fork(const std::string& mac_path, const uint8_t* data, size_t size) = 0;

    // Directory operations
    virtual int mkdir(const std::string& mac_path) = 0;
    virtual int delete_entry(const std::string& mac_path) = 0;
    virtual int rename(const std::string& old_path, const std::string& new_path) = 0;

    // Metadata
    virtual int set_type_creator(const std::string& mac_path, const char* type, const char* creator) = 0;
    virtual int set_finder_flags(const std::string& mac_path, int16_t flags) = 0;
    virtual int set_blessed(uint32_t folder_cnid) = 0;

    // Check
    virtual std::string check() = 0;

    // Open a volume from an image file. Tries HFS first, then HFS+.
    static std::unique_ptr<Volume> open(const char* path);
};

} // namespace hfsbrowse

#endif // HFSBROWSE_VOLUME_H
