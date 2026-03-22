/*
 * HFS Browser Library - File/folder entry representation
 * No UI dependencies — usable from CLI or any GUI toolkit.
 */

#ifndef HFSBROWSE_ENTRY_H
#define HFSBROWSE_ENTRY_H

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

struct HBEntry {
    std::string name;
    bool is_dir = false;
    uint32_t cnid = 0;
    uint32_t parent_cnid = 0;
    uint64_t data_size = 0;
    uint64_t rsrc_size = 0;
    char type[5] = {};
    char creator[5] = {};
    int16_t fdflags = 0;
    time_t crdate = 0;   // creation date
    time_t mddate = 0;   // modification date
    std::vector<uint8_t> icon_rgba;  // 64x64 RGBA pixel data (empty if no icon)
};

#endif // HFSBROWSE_ENTRY_H
