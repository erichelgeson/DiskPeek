/*
 * HFS Browser Library - Icon extraction and PNG export
 */

#include "icon.h"
#include <cstring>
#include <cstdio>
#include <zlib.h>

// --- Big-endian helpers ---

static uint16_t read_u16be(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t read_u32be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void write_u32be(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF; p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;  p[3] = v & 0xFF;
}

// --- Resource fork parsing ---

static const uint8_t* find_resource(const std::vector<uint8_t>& rsrc,
                                     uint32_t res_type, int16_t res_id,
                                     uint32_t* out_len) {
    if (rsrc.size() < 16) return nullptr;
    const uint8_t* base = rsrc.data();
    size_t total = rsrc.size();

    uint32_t data_offset = read_u32be(base + 0);
    uint32_t map_offset  = read_u32be(base + 4);

    if (map_offset + 30 > total) return nullptr;

    uint16_t type_list_off = read_u16be(base + map_offset + 24);
    uint32_t type_list_abs = map_offset + type_list_off;

    if (type_list_abs + 2 > total) return nullptr;

    uint16_t num_types = read_u16be(base + type_list_abs) + 1;

    for (uint16_t t = 0; t < num_types; t++) {
        uint32_t te = type_list_abs + 2 + t * 8;
        if (te + 8 > total) break;

        uint32_t this_type = read_u32be(base + te);
        uint16_t count     = read_u16be(base + te + 4) + 1;
        uint16_t ref_off   = read_u16be(base + te + 6);

        if (this_type != res_type) continue;

        uint32_t ref_abs = type_list_abs + ref_off;
        for (uint16_t r = 0; r < count; r++) {
            uint32_t re = ref_abs + r * 12;
            if (re + 12 > total) break;

            int16_t rid = (int16_t)read_u16be(base + re);
            if (rid != res_id) continue;

            uint32_t d_off = ((uint32_t)base[re + 5] << 16) |
                             ((uint32_t)base[re + 6] << 8) |
                             base[re + 7];
            uint32_t d_abs = data_offset + d_off;
            if (d_abs + 4 > total) return nullptr;

            uint32_t d_len = read_u32be(base + d_abs);
            if (d_abs + 4 + d_len > total) return nullptr;

            *out_len = d_len;
            return base + d_abs + 4;
        }
    }
    return nullptr;
}

// --- Color palettes ---

static const uint8_t mac_clut4_rgb[16][3] = {
    {255,255,255},{252,243,5},{255,100,2},{221,8,6},
    {242,8,132},{71,0,165},{0,0,212},{2,171,234},
    {31,183,20},{0,100,18},{86,44,5},{144,113,58},
    {192,192,192},{128,128,128},{64,64,64},{0,0,0},
};

static const uint8_t mac_clut_rgb[256][3] = {
    {255,255,255},{255,255,204},{255,255,153},{255,255,102},{255,255,51},{255,255,0},
    {255,204,255},{255,204,204},{255,204,153},{255,204,102},{255,204,51},{255,204,0},
    {255,153,255},{255,153,204},{255,153,153},{255,153,102},{255,153,51},{255,153,0},
    {255,102,255},{255,102,204},{255,102,153},{255,102,102},{255,102,51},{255,102,0},
    {255,51,255},{255,51,204},{255,51,153},{255,51,102},{255,51,51},{255,51,0},
    {255,0,255},{255,0,204},{255,0,153},{255,0,102},{255,0,51},{255,0,0},
    {204,255,255},{204,255,204},{204,255,153},{204,255,102},{204,255,51},{204,255,0},
    {204,204,255},{204,204,204},{204,204,153},{204,204,102},{204,204,51},{204,204,0},
    {204,153,255},{204,153,204},{204,153,153},{204,153,102},{204,153,51},{204,153,0},
    {204,102,255},{204,102,204},{204,102,153},{204,102,102},{204,102,51},{204,102,0},
    {204,51,255},{204,51,204},{204,51,153},{204,51,102},{204,51,51},{204,51,0},
    {204,0,255},{204,0,204},{204,0,153},{204,0,102},{204,0,51},{204,0,0},
    {153,255,255},{153,255,204},{153,255,153},{153,255,102},{153,255,51},{153,255,0},
    {153,204,255},{153,204,204},{153,204,153},{153,204,102},{153,204,51},{153,204,0},
    {153,153,255},{153,153,204},{153,153,153},{153,153,102},{153,153,51},{153,153,0},
    {153,102,255},{153,102,204},{153,102,153},{153,102,102},{153,102,51},{153,102,0},
    {153,51,255},{153,51,204},{153,51,153},{153,51,102},{153,51,51},{153,51,0},
    {153,0,255},{153,0,204},{153,0,153},{153,0,102},{153,0,51},{153,0,0},
    {102,255,255},{102,255,204},{102,255,153},{102,255,102},{102,255,51},{102,255,0},
    {102,204,255},{102,204,204},{102,204,153},{102,204,102},{102,204,51},{102,204,0},
    {102,153,255},{102,153,204},{102,153,153},{102,153,102},{102,153,51},{102,153,0},
    {102,102,255},{102,102,204},{102,102,153},{102,102,102},{102,102,51},{102,102,0},
    {102,51,255},{102,51,204},{102,51,153},{102,51,102},{102,51,51},{102,51,0},
    {102,0,255},{102,0,204},{102,0,153},{102,0,102},{102,0,51},{102,0,0},
    {51,255,255},{51,255,204},{51,255,153},{51,255,102},{51,255,51},{51,255,0},
    {51,204,255},{51,204,204},{51,204,153},{51,204,102},{51,204,51},{51,204,0},
    {51,153,255},{51,153,204},{51,153,153},{51,153,102},{51,153,51},{51,153,0},
    {51,102,255},{51,102,204},{51,102,153},{51,102,102},{51,102,51},{51,102,0},
    {51,51,255},{51,51,204},{51,51,153},{51,51,102},{51,51,51},{51,51,0},
    {51,0,255},{51,0,204},{51,0,153},{51,0,102},{51,0,51},{51,0,0},
    {0,255,255},{0,255,204},{0,255,153},{0,255,102},{0,255,51},{0,255,0},
    {0,204,255},{0,204,204},{0,204,153},{0,204,102},{0,204,51},{0,204,0},
    {0,153,255},{0,153,204},{0,153,153},{0,153,102},{0,153,51},{0,153,0},
    {0,102,255},{0,102,204},{0,102,153},{0,102,102},{0,102,51},{0,102,0},
    {0,51,255},{0,51,204},{0,51,153},{0,51,102},{0,51,51},{0,51,0},
    {0,0,255},{0,0,204},{0,0,153},{0,0,102},{0,0,51},
    {238,0,0},{221,0,0},{187,0,0},{170,0,0},{136,0,0},
    {119,0,0},{85,0,0},{68,0,0},{34,0,0},{17,0,0},
    {0,238,0},{0,221,0},{0,187,0},{0,170,0},{0,136,0},
    {0,119,0},{0,85,0},{0,68,0},{0,34,0},{0,17,0},
    {0,0,238},{0,0,221},{0,0,187},{0,0,170},{0,0,136},
    {0,0,119},{0,0,85},{0,0,68},{0,0,34},{0,0,17},
    {238,238,238},{221,221,221},{187,187,187},{170,170,170},{136,136,136},
    {119,119,119},{85,85,85},{68,68,68},{34,34,34},{17,17,17},
    {0,0,0},
};

// --- Icon decoders (all output 64x64 RGBA pixel-doubled) ---

static std::vector<uint8_t> icon_from_icn(const uint8_t* data, uint32_t len) {
    if (len < 256) return {};
    const uint8_t* icon_bits = data;
    const uint8_t* mask_bits = data + 128;
    std::vector<uint8_t> rgba(64 * 64 * 4);
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int byte_idx = y * 4 + x / 8;
            int bit = 7 - (x % 8);
            bool icon_set = (icon_bits[byte_idx] >> bit) & 1;
            bool mask_set = (mask_bits[byte_idx] >> bit) & 1;
            uint8_t r, g, b, a;
            if (mask_set) { uint8_t c = icon_set ? 0 : 255; r = g = b = c; a = 255; }
            else { r = g = b = 0; a = 0; }
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++) {
                    int idx = ((y*2+dy) * 64 + (x*2+dx)) * 4;
                    rgba[idx] = r; rgba[idx+1] = g; rgba[idx+2] = b; rgba[idx+3] = a;
                }
        }
    }
    return rgba;
}

static std::vector<uint8_t> icon_from_icl4(const uint8_t* color_data, uint32_t color_len,
                                            const uint8_t* mask_data, uint32_t mask_len) {
    if (color_len < 512) return {};
    const uint8_t* mask_bits = (mask_data && mask_len >= 256) ? mask_data + 128 : nullptr;
    std::vector<uint8_t> rgba(64 * 64 * 4);
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int byte_off = y * 16 + x / 2;
            uint8_t cidx = (x & 1) ? (color_data[byte_off] & 0x0F) : (color_data[byte_off] >> 4);
            uint8_t r = mac_clut4_rgb[cidx][0], g = mac_clut4_rgb[cidx][1], b = mac_clut4_rgb[cidx][2];
            uint8_t a = 255;
            if (mask_bits) {
                int mb = y * 4 + x / 8;
                if (!((mask_bits[mb] >> (7 - (x % 8))) & 1)) a = 0;
            } else if (cidx == 0) a = 0;
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++) {
                    int idx = ((y*2+dy) * 64 + (x*2+dx)) * 4;
                    rgba[idx] = r; rgba[idx+1] = g; rgba[idx+2] = b; rgba[idx+3] = a;
                }
        }
    }
    return rgba;
}

static std::vector<uint8_t> icon_from_icl8(const uint8_t* color_data, uint32_t color_len,
                                             const uint8_t* mask_data, uint32_t mask_len) {
    if (color_len < 1024) return {};
    const uint8_t* mask_bits = (mask_data && mask_len >= 256) ? mask_data + 128 : nullptr;
    std::vector<uint8_t> rgba(64 * 64 * 4);
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            uint8_t cidx = color_data[y * 32 + x];
            uint8_t r = mac_clut_rgb[cidx][0], g = mac_clut_rgb[cidx][1], b = mac_clut_rgb[cidx][2];
            uint8_t a = 255;
            if (mask_bits) {
                int mb = y * 4 + x / 8;
                if (!((mask_bits[mb] >> (7 - (x % 8))) & 1)) a = 0;
            } else if (cidx == 255) a = 0;
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++) {
                    int idx = ((y*2+dy) * 64 + (x*2+dx)) * 4;
                    rgba[idx] = r; rgba[idx+1] = g; rgba[idx+2] = b; rgba[idx+3] = a;
                }
        }
    }
    return rgba;
}

// --- Public API ---

namespace hfsbrowse {
namespace icon {

std::vector<uint8_t> extract_rgba(const std::vector<uint8_t>& rsrc_fork) {
    static const int16_t ids_to_try[] = { 128, -16455, 0 };
    uint32_t ICN_TYPE  = ('I' << 24) | ('C' << 16) | ('N' << 8) | '#';
    uint32_t ICL8_TYPE = ('i' << 24) | ('c' << 16) | ('l' << 8) | '8';
    uint32_t ICL4_TYPE = ('i' << 24) | ('c' << 16) | ('l' << 8) | '4';

    for (int i = 0; ids_to_try[i] != 0 || i < 2; i++) {
        uint32_t c8_len = 0, c4_len = 0, mask_len = 0;
        const uint8_t* c8 = find_resource(rsrc_fork, ICL8_TYPE, ids_to_try[i], &c8_len);
        const uint8_t* c4 = find_resource(rsrc_fork, ICL4_TYPE, ids_to_try[i], &c4_len);
        const uint8_t* mask = find_resource(rsrc_fork, ICN_TYPE, ids_to_try[i], &mask_len);

        std::vector<uint8_t> rgba;
        if (c8 && c8_len >= 1024)      rgba = icon_from_icl8(c8, c8_len, mask, mask_len);
        else if (c4 && c4_len >= 512)  rgba = icon_from_icl4(c4, c4_len, mask, mask_len);
        else if (mask && mask_len >= 256) rgba = icon_from_icn(mask, mask_len);
        if (!rgba.empty()) return rgba;
    }
    return {};
}

static void png_write_chunk(FILE* f, const char* type, const uint8_t* data, uint32_t len) {
    uint8_t hdr[4];
    write_u32be(hdr, len);
    fwrite(hdr, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len > 0) fwrite(data, 1, len, f);
    uint32_t c = (uint32_t)crc32(0, (const uint8_t*)type, 4);
    if (len > 0) c = (uint32_t)crc32(c, data, (uInt)len);
    write_u32be(hdr, c);
    fwrite(hdr, 1, 4, f);
}

bool write_png(const std::string& path, const std::vector<uint8_t>& rgba) {
    if (rgba.size() != 64 * 64 * 4) return false;

    std::vector<uint8_t> raw;
    raw.reserve(64 * (1 + 64 * 4));
    for (int y = 0; y < 64; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba.begin() + y * 64 * 4, rgba.begin() + (y + 1) * 64 * 4);
    }

    uLongf csz = compressBound((uLong)raw.size());
    std::vector<uint8_t> comp(csz);
    if (compress2(comp.data(), &csz, raw.data(), (uLong)raw.size(), 9) != Z_OK)
        return false;
    comp.resize(csz);

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    const uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);

    uint8_t ihdr[13];
    write_u32be(ihdr, 64); write_u32be(ihdr + 4, 64);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_write_chunk(f, "IHDR", ihdr, 13);
    png_write_chunk(f, "IDAT", comp.data(), (uint32_t)comp.size());
    png_write_chunk(f, "IEND", nullptr, 0);

    fclose(f);
    return true;
}

} // namespace icon
} // namespace hfsbrowse
