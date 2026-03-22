/*
 * HFS Browser Library - AppleDouble format and xattr fork/info writing
 */

#include "appledouble.h"
#include "platform.h"
#include <cstdio>
#include <cstring>

namespace hfsbrowse {

static void write_u16be(uint8_t* p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF; p[1] = v & 0xFF;
}

static void write_u32be(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF; p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;  p[3] = v & 0xFF;
}

bool write_appledouble(const std::string& host_path,
                       const char* type, const char* creator,
                       int16_t fdflags,
                       const std::vector<uint8_t>& rsrc_data) {
    std::string dir, base;
    size_t slash = host_path.rfind('/');
    if (slash != std::string::npos) {
        dir = host_path.substr(0, slash + 1);
        base = host_path.substr(slash + 1);
    } else {
        base = host_path;
    }
    std::string ad_path = dir + "._" + base;

    const uint32_t header_size = 26;
    const uint32_t num_entries = 2;
    const uint32_t entry_desc_size = num_entries * 12;
    const uint32_t finder_info_offset = header_size + entry_desc_size;
    const uint32_t finder_info_len = 32;
    const uint32_t rsrc_offset = finder_info_offset + finder_info_len;
    const uint32_t rsrc_len = (uint32_t)rsrc_data.size();

    std::vector<uint8_t> ad(rsrc_offset + rsrc_len, 0);

    write_u32be(ad.data() + 0, 0x00051607);
    write_u32be(ad.data() + 4, 0x00020000);
    write_u16be(ad.data() + 24, num_entries);

    write_u32be(ad.data() + 26, 9);
    write_u32be(ad.data() + 30, finder_info_offset);
    write_u32be(ad.data() + 34, finder_info_len);

    write_u32be(ad.data() + 38, 2);
    write_u32be(ad.data() + 42, rsrc_offset);
    write_u32be(ad.data() + 46, rsrc_len);

    uint8_t* fi = ad.data() + finder_info_offset;
    if (type && strlen(type) == 4) memcpy(fi, type, 4);
    if (creator && strlen(creator) == 4) memcpy(fi + 4, creator, 4);
    write_u16be(fi + 8, (uint16_t)fdflags);

    if (rsrc_len > 0)
        memcpy(ad.data() + rsrc_offset, rsrc_data.data(), rsrc_len);

    FILE* f = fopen(ad_path.c_str(), "wb");
    if (!f) return false;

    bool ok = fwrite(ad.data(), 1, ad.size(), f) == ad.size();
    fclose(f);
    return ok;
}

void write_forkinfo(const std::string& host_path,
                    const char* type, const char* creator,
                    int16_t fdflags,
                    const std::vector<uint8_t>& rsrc_data) {
    bool has_tc = (type && type[0] && strcmp(type, "????") != 0);
    bool has_rsrc = (!rsrc_data.empty());
    if (!has_tc && !has_rsrc) return;

    // Try native xattrs first
    if (platform_write_xattr_forkinfo(host_path.c_str(), type, creator,
                                       fdflags, rsrc_data.data(), rsrc_data.size()))
        return;

    // Fall back to AppleDouble
    write_appledouble(host_path, type, creator, fdflags, rsrc_data);
}

} // namespace hfsbrowse
