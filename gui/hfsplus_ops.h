/*
 * HFS+ operations wrapper around libdmg-hfsplus
 */

#ifndef HFSPLUS_OPS_H
#define HFSPLUS_OPS_H

#include <cstdint>
#include <cstddef>
#include <string>

struct HFSPlusVolume;

// Open an HFS+ volume from a flat file at the given byte offset.
// readonly=true opens for read-only access.
// Returns nullptr on failure.
HFSPlusVolume* hfsplus_open(const char* path, uint64_t partition_offset, bool readonly);

void hfsplus_close(HFSPlusVolume* vol);

// Volume info
const char* hfsplus_volume_name(HFSPlusVolume* vol);
uint64_t hfsplus_total_bytes(HFSPlusVolume* vol);
uint64_t hfsplus_free_bytes(HFSPlusVolume* vol);

// Directory entry from HFS+
struct HFSPlusDirEntry {
    char name[256];
    bool is_dir;
    uint32_t cnid;
    uint32_t parent_cnid;
    uint64_t data_size;
    uint64_t rsrc_size;
    char type[5];
    char creator[5];
    uint16_t finder_flags;
};

// List directory contents. path uses Mac-style ":" separators.
// Caller must free entries with hfsplus_free_entries().
// Returns 0 on success, -1 on failure.
int hfsplus_list_dir(HFSPlusVolume* vol, const char* path,
                     HFSPlusDirEntry** entries, int* count);

// List directory contents by folder CNID (avoids path lookup issues).
int hfsplus_list_dir_by_cnid(HFSPlusVolume* vol, uint32_t folder_cnid,
                              HFSPlusDirEntry** entries, int* count);

void hfsplus_free_entries(HFSPlusDirEntry* entries);

// Read file data. fork: 0=data, 1=resource.
// Caller must free *data with free().
// Returns 0 on success, -1 on failure.
int hfsplus_read_file(HFSPlusVolume* vol, const char* path,
                      uint8_t** data, size_t* size, int fork);

// Read file data by CNID (avoids path lookup issues with special characters).
// parent_cnid is the folder CNID containing the file (needed for catalog key lookup).
// Returns 0 on success, -1 on failure.
int hfsplus_read_file_by_cnid(HFSPlusVolume* vol, uint32_t cnid, uint32_t parent_cnid,
                               uint8_t** data, size_t* size, int fork);

// Write a file (data fork only). Creates or overwrites.
// Returns 0 on success, -1 on failure.
int hfsplus_write_file(HFSPlusVolume* vol, const char* path,
                       const uint8_t* data, size_t size);

// Delete a file or empty directory.
// Returns 0 on success, -1 on failure.
int hfsplus_delete(HFSPlusVolume* vol, const char* path);

// Create a directory.
// Returns 0 on success, -1 on failure.
int hfsplus_mkdir(HFSPlusVolume* vol, const char* path);

// Write resource fork data to an existing file.
// Returns 0 on success, -1 on failure.
int hfsplus_write_rsrc_fork(HFSPlusVolume* vol, const char* path,
                            const uint8_t* data, size_t size);

// Get/set the blessed (boot) system folder CNID.
uint32_t hfsplus_get_blessed(HFSPlusVolume* vol);
int hfsplus_set_blessed(HFSPlusVolume* vol, uint32_t folder_cnid);

// Check and repair free space:
// 1. Scans all file extents to find which blocks are actually in use
// 2. Compares with allocation bitmap and frees orphaned blocks
// 3. Fixes the volume header's freeBlocks count
// Returns the corrected free space in bytes, or 0 on failure.
uint64_t hfsplus_repair_free_count(HFSPlusVolume* vol, std::string* log);

// Force-delete a file by removing its catalog entry without freeing extents.
// Use when normal delete fails due to corrupt extents.
// Returns 0 on success, -1 on failure.
int hfsplus_force_delete(HFSPlusVolume* vol, const char* path);

// Rename/move a file or folder. Both paths are Mac-style (colon-separated).
// Returns 0 on success, -1 on failure.
int hfsplus_rename(HFSPlusVolume* vol, const char* old_path, const char* new_path);

// Set type and creator codes on a file.
// type and creator must be exactly 4 bytes each.
// Returns 0 on success, -1 on failure.
int hfsplus_set_type_creator(HFSPlusVolume* vol, const char* path,
                             const char* type, const char* creator);

#endif // HFSPLUS_OPS_H
