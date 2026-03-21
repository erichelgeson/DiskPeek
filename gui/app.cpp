/*
 * HFS Browser - Application implementation
 */

#include "app.h"
#include "imgui.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cerrno>
#include <filesystem>

#include "platform.h"

namespace fs = std::filesystem;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wregister"
extern "C" {
#include "binhex.h"
#include "crc.h"
}
#pragma GCC diagnostic pop

#include <zlib.h>

// --- Big-endian helpers ---

static uint16_t read_u16be(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t read_u32be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void write_u16be(uint8_t* p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

static void write_u32be(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

// --- Resource fork parsing ---

// Find a resource by type code in a resource fork blob.
// Returns pointer to resource data and sets *out_len, or nullptr if not found.
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

            // 3-byte data offset at re+5
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

// Classic Mac 8-bit system color table (256 entries, the standard clut)
// This is the standard Mac OS 256-color palette used by icl8 resources.
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

// Convert ICN# (32x32 1-bit icon + mask, 256 bytes) to RGBA, pixel-doubled to 64x64.
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
            if (mask_set) {
                uint8_t c = icon_set ? 0 : 255;
                r = g = b = c; a = 255;
            } else {
                r = g = b = 0; a = 0;
            }

            // Pixel-double: write 2x2 block
            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    int idx = ((y*2+dy) * 64 + (x*2+dx)) * 4;
                    rgba[idx+0] = r; rgba[idx+1] = g;
                    rgba[idx+2] = b; rgba[idx+3] = a;
                }
            }
        }
    }
    return rgba;
}

// Convert icl8 (32x32 8-bit color icon, 1024 bytes) with ICN# mask to RGBA, pixel-doubled to 64x64.
static std::vector<uint8_t> icon_from_icl8(const uint8_t* color_data, uint32_t color_len,
                                             const uint8_t* mask_data, uint32_t mask_len) {
    if (color_len < 1024) return {};

    // mask is from ICN# (256 bytes: 128 icon + 128 mask), mask starts at offset 128
    const uint8_t* mask_bits = nullptr;
    if (mask_data && mask_len >= 256)
        mask_bits = mask_data + 128;

    std::vector<uint8_t> rgba(64 * 64 * 4);

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            uint8_t cidx = color_data[y * 32 + x];
            uint8_t r = mac_clut_rgb[cidx][0];
            uint8_t g = mac_clut_rgb[cidx][1];
            uint8_t b = mac_clut_rgb[cidx][2];
            uint8_t a = 255;

            if (mask_bits) {
                int byte_idx = y * 4 + x / 8;
                int bit = 7 - (x % 8);
                if (!((mask_bits[byte_idx] >> bit) & 1))
                    a = 0;
            } else if (cidx == 255) {
                // Index 255 = white, treat as transparent if no mask
                a = 0;
            }

            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    int idx = ((y*2+dy) * 64 + (x*2+dx)) * 4;
                    rgba[idx+0] = r; rgba[idx+1] = g;
                    rgba[idx+2] = b; rgba[idx+3] = a;
                }
            }
        }
    }
    return rgba;
}

// --- Fix-A-Fork type/creator extension table (from BlueSCSI SCSITransfer) ---

struct FAFExtEntry {
    const char* ext;
    const char type[5];
    const char creator[5];
};

static const FAFExtEntry s_faf_ext_table[] = {
    {"1st","TEXT","ttxt"},{"669","6669","SNPL"},{"8med","STrk","SCPL"},
    {"8svx","8SVX","SCPL"},{"aif","AIFF","SCPL"},{"aifc","AIFC","SCPL"},
    {"aiff","AIFF","SCPL"},{"al","ALAW","SCPL"},{"arc","mArc","SITx"},
    {"arj","BINA","DArj"},{"asc","TEXT","ttxt"},{"asm","TEXT","ttxt"},
    {"au","ULAW","TVOD"},{"avi","VfW ","TVOD"},{"bas","TEXT","ttxt"},
    {"bat","TEXT","ttxt"},{"bin","BINA","SITx"},{"bmp","BMPp","ogle"},
    {"bz","Bzp2","SITx"},{"c","TEXT","KAHL"},{"class","Clss","CWIE"},
    {"cmd","TEXT","ttxt"},{"com","PCFA","SWIN"},{"cpp","TEXT","CWIE"},
    {"cpt","PACT","SITx"},{"csv","TEXT","XCEL"},{"cur","CUR ","GKON"},
    {"cvs","drw2","DAD2"},{"cwj","CWSS","cwkj"},{"doc","WDBN","MSWD"},
    {"dot","sDBN","MSWD"},{"dsk","dimg","dCpy"},{"dvi","ODVI","xdvi"},
    {"dxf","TEXT","SWVL"},{"eps","EPSF","vgrd"},{"epsf","EPSF","vgrd"},
    {"exe","PCFA","SWIN"},{"faq","TEXT","ttxt"},{"fla","SPA ","MFL2"},
    {"flc","FLI ","TVOD"},{"fli","FLI ","TVOD"},{"fm","FMPR","FMPR"},
    {"gif","GIFf","ogle"},{"gz","SIT!","SITx"},{"h","TEXT","KAHL"},
    {"hqx","TEXT","SITx"},{"htm","TEXT","MOSS"},{"html","TEXT","MOSS"},
    {"ico","ICO ","GKON"},{"iff","ILBM","GKON"},{"img","dImg","ddsk"},
    {"ini","TEXT","ttxt"},{"iso","rodh","ddsk"},{"java","TEXT","CWIE"},
    {"jfif","JPEG","ogle"},{"jpeg","JPEG","ogle"},{"jpg","JPEG","ogle"},
    {"lha","LHA ","SITx"},{"lzh","LHA ","SITx"},{"mac","PICT","ogle"},
    {"mcw","WDBN","MSWD"},{"me","TEXT","ttxt"},{"mid","Midi","TVOD"},
    {"midi","Midi","TVOD"},{"mod","STrk","SCPL"},{"moov","MooV","TVOD"},
    {"mov","MooV","TVOD"},{"mp2","MPEG","TVOD"},{"mp3","MPG3","TVOD"},
    {"mpa","MPEG","TVOD"},{"mpeg","MPEG","TVOD"},{"mpg","MPEG","TVOD"},
    {"nfo","TEXT","ttxt"},{"p","TEXT","CWIE"},{"pas","TEXT","CWIE"},
    {"pbm","PPGM","GKON"},{"pct","PICT","ogle"},{"pcx","PCXx","GKON"},
    {"pdf","PDF ","CARO"},{"pgm","PPGM","GKON"},{"pic","PICT","ogle"},
    {"pict","PICT","ogle"},{"pit","PIT ","SITx"},{"pl","TEXT","McPL"},
    {"png","PNG ","ogle"},{"pntg","PNTG","ogle"},{"ppm","PPGM","GKON"},
    {"ps","TEXT","vgrd"},{"psd","8BPS","8BIM"},{"qt","MooV","TVOD"},
    {"qxd","XDOC","XPR3"},{"raw","rodh","ddsk"},{"readme","TEXT","ttxt"},
    {"rgb","SGI ","GKON"},{"rme","TEXT","ttxt"},{"rsrc","rsrc","RSED"},
    {"rtf","TEXT","MSWD"},{"s3m","S3M ","SNPL"},{"sea","APPL","????"},
    {"sgi",".SGI","ogle"},{"sit","SIT!","SITx"},{"snd","BINA","SCPL"},
    {"swf","SWFL","SWF2"},{"tar","TARF","SITx"},{"tex","TEXT","OTEX"},
    {"text","TEXT","ttxt"},{"tga","TPIC","GKON"},{"tgz","Gzip","SITx"},
    {"tif","TIFF","ogle"},{"tiff","TIFF","ogle"},{"toast","CDr3","GImg"},
    {"txt","TEXT","ttxt"},{"url","AURL","Arch"},{"uu","TEXT","SITx"},
    {"uue","TEXT","SITx"},{"voc","VOC ","SCPL"},{"wav","WAVE","TVOD"},
    {"wmf","WMF ","GKON"},{"wp","WP5 ","WPC2"},{"wri","WDBN","MSWD"},
    {"xbm","XBM ","GKON"},{"xlc","XLC ","XCEL"},{"xls","XLS ","XCEL"},
    {"xlw","XLW ","XCEL"},{"xm","XM  ","SNPL"},{"xpm","XPM ","GKON"},
    {"zip","ZIP ","SITx"},{"zoo","Zoo ","Booz"},
};

// Detect type/creator from file extension (FAF table)
bool App::detect_type_creator_ext(const char* filename, TypeCreatorResult* out) {
    const char* dot = strrchr(filename, '.');
    if (!dot || dot[1] == '\0') return false;
    const char* ext = dot + 1;

    for (size_t i = 0; i < sizeof(s_faf_ext_table)/sizeof(s_faf_ext_table[0]); i++) {
        if (strcasecmp(ext, s_faf_ext_table[i].ext) == 0) {
            memcpy(out->type, s_faf_ext_table[i].type, 5);
            memcpy(out->creator, s_faf_ext_table[i].creator, 5);
            return true;
        }
    }
    return false;
}

// Detect type/creator from file magic bytes
bool App::detect_type_creator_magic(const uint8_t* data, size_t len, TypeCreatorResult* out) {
    if (len < 4) return false;

    // BinHex 4.0
    if (len >= 45 && memcmp(data + 34, "BinHex 4.0", 10) == 0) {
        memcpy(out->type, "TEXT", 5); memcpy(out->creator, "SITx", 5); return true;
    }
    // StuffIt 5.x
    if (len >= 16 && memcmp(data, "StuffIt (c)1997", 15) == 0) {
        memcpy(out->type, "SITD", 5); memcpy(out->creator, "SIT!", 5); return true;
    }
    // StuffIt 1.5-4.5
    if (len >= 4 && memcmp(data, "SIT!", 4) == 0) {
        memcpy(out->type, "SIT!", 5); memcpy(out->creator, "SIT!", 5); return true;
    }
    // Zip
    if (len >= 2 && data[0] == 'P' && data[1] == 'K') {
        memcpy(out->type, "ZIP ", 5); memcpy(out->creator, "SITx", 5); return true;
    }
    // GIF
    if (len >= 4 && memcmp(data, "GIF8", 4) == 0) {
        memcpy(out->type, "GIFf", 5); memcpy(out->creator, "ogle", 5); return true;
    }
    // PNG
    if (len >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        memcpy(out->type, "PNG ", 5); memcpy(out->creator, "ogle", 5); return true;
    }
    // JPEG
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
        memcpy(out->type, "JPEG", 5); memcpy(out->creator, "ogle", 5); return true;
    }
    // PDF
    if (len >= 5 && memcmp(data, "%PDF-", 5) == 0) {
        memcpy(out->type, "PDF ", 5); memcpy(out->creator, "CARO", 5); return true;
    }
    // Disk Copy 4.2
    if (len >= 54 && data[52] == 0x01 && data[53] == 0x00) {
        memcpy(out->type, "dImg", 5); memcpy(out->creator, "dCpy", 5); return true;
    }

    return false;
}

// Legacy lookup (thin wrapper for old call sites)
const TypeCreatorMap* App::lookup_type_creator(const char* filename) {
    // Use a small static table for the legacy interface
    static const TypeCreatorMap s_type_creator_map[] = {
        { ".txt",  "TEXT", "ttxt" },
        { ".text", "TEXT", "ttxt" },
        { ".hqx",  "TEXT", "SITx" },
        { ".bin",  "BINA", "SITx" },
        { ".sit",  "SIT!", "SITx" },
        { ".jpg",  "JPEG", "ogle" },
        { ".jpeg", "JPEG", "ogle" },
        { ".gif",  "GIFf", "ogle" },
        { ".png",  "PNG ", "ogle" },
        { ".zip",  "ZIP ", "SITx" },
        { ".pdf",  "PDF ", "CARO" },
        { ".doc",  "WDBN", "MSWD" },
        { ".sea",  "APPL", "????" },
        { nullptr, nullptr, nullptr },
    };

    const char* dot = strrchr(filename, '.');
    if (!dot) return nullptr;

    for (const TypeCreatorMap* m = s_type_creator_map; m->ext; m++) {
        if (strcasecmp(dot, m->ext) == 0)
            return m;
    }
    return nullptr;
}

// --- Icon drawing ---

void App::draw_folder_icon(ImVec2 pos, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float s = size;

    // Classic Mac OS blue folder colors
    ImU32 body   = IM_COL32(100, 130, 200, 255);
    ImU32 tab    = IM_COL32(80, 110, 180, 255);
    ImU32 shadow = IM_COL32(50, 70, 130, 255);
    ImU32 hilite = IM_COL32(140, 170, 230, 255);

    // Tab (top-left)
    dl->AddRectFilled(
        ImVec2(pos.x + s * 0.05f, pos.y + s * 0.1f),
        ImVec2(pos.x + s * 0.45f, pos.y + s * 0.3f),
        tab);

    // Body
    dl->AddRectFilled(
        ImVec2(pos.x + s * 0.05f, pos.y + s * 0.25f),
        ImVec2(pos.x + s * 0.95f, pos.y + s * 0.9f),
        body);

    // Top highlight
    dl->AddLine(
        ImVec2(pos.x + s * 0.05f, pos.y + s * 0.25f),
        ImVec2(pos.x + s * 0.95f, pos.y + s * 0.25f),
        hilite);

    // Bottom/right shadow
    dl->AddLine(
        ImVec2(pos.x + s * 0.05f, pos.y + s * 0.9f),
        ImVec2(pos.x + s * 0.95f, pos.y + s * 0.9f),
        shadow);
    dl->AddLine(
        ImVec2(pos.x + s * 0.95f, pos.y + s * 0.25f),
        ImVec2(pos.x + s * 0.95f, pos.y + s * 0.9f),
        shadow);

    // Black outline
    dl->AddRect(
        ImVec2(pos.x + s * 0.05f, pos.y + s * 0.25f),
        ImVec2(pos.x + s * 0.95f, pos.y + s * 0.9f),
        IM_COL32(0, 0, 0, 200));
    dl->AddRect(
        ImVec2(pos.x + s * 0.05f, pos.y + s * 0.1f),
        ImVec2(pos.x + s * 0.45f, pos.y + s * 0.3f),
        IM_COL32(0, 0, 0, 200));
}

void App::draw_file_icon(ImVec2 pos, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float s = size;
    float fold = s * 0.2f;

    // Classic Mac document — white page with dog-ear, black outline
    ImU32 page   = IM_COL32(255, 255, 255, 255);
    ImU32 outline = IM_COL32(0, 0, 0, 220);
    ImU32 fold_bg = IM_COL32(210, 210, 210, 255);

    float x0 = pos.x + s * 0.2f;
    float x1 = pos.x + s * 0.8f;
    float xf = pos.x + s * 0.6f;   // fold start X
    float y0 = pos.y + s * 0.05f;
    float y1 = pos.y + s * 0.95f;

    // Page body (with corner cut)
    ImVec2 points[5] = {
        ImVec2(x0, y0),
        ImVec2(xf, y0),
        ImVec2(x1, y0 + fold),
        ImVec2(x1, y1),
        ImVec2(x0, y1),
    };
    dl->AddConvexPolyFilled(points, 5, page);
    dl->AddPolyline(points, 5, outline, ImDrawFlags_Closed, 1.0f);

    // Corner fold
    dl->AddTriangleFilled(
        ImVec2(xf, y0),
        ImVec2(x1, y0 + fold),
        ImVec2(xf, y0 + fold),
        fold_bg);
    dl->AddTriangle(
        ImVec2(xf, y0),
        ImVec2(x1, y0 + fold),
        ImVec2(xf, y0 + fold),
        outline, 1.0f);
}

// --- App lifecycle ---

App::App() {}
App::~App() { shutdown(); }

void App::init() {
    status_text_ = "Open an HFS disk image to begin (or drag & drop)";

    const char* home = getenv("HOME");
    picker_path_ = home ? home : "/";
}

void App::shutdown() {
    cleanup_icons();
    close_image();
}

// --- Image open/close ---

// --- APM partition reading ---

int App::read_apm_partitions(const char* path, APMPartition* parts, int max_parts) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    int count = 0;
    uint8_t block[512];

    // Read block 1 (first APM entry at offset 512)
    if (fseek(f, 512, SEEK_SET) != 0 || fread(block, 512, 1, f) != 1) {
        fclose(f);
        return 0;
    }

    // Check APM signature 'PM' (0x504D)
    if (block[0] != 0x50 || block[1] != 0x4D) {
        fclose(f);
        return 0;
    }

    uint32_t map_entries = ((uint32_t)block[4] << 24) | ((uint32_t)block[5] << 16) |
                           ((uint32_t)block[6] << 8) | block[7];

    for (uint32_t i = 0; i < map_entries && count < max_parts; i++) {
        if (i > 0) {
            if (fseek(f, 512 * (i + 1), SEEK_SET) != 0 || fread(block, 512, 1, f) != 1)
                break;
            if (block[0] != 0x50 || block[1] != 0x4D)
                break;
        }

        uint32_t pblock_start = ((uint32_t)block[8] << 24) | ((uint32_t)block[9] << 16) |
                                ((uint32_t)block[10] << 8) | block[11];

        parts[count].offset = (uint64_t)pblock_start * 512;
        memcpy(parts[count].type, block + 48, 32);
        parts[count].type[31] = '\0';
        count++;
    }

    fclose(f);
    return count;
}

// Check if an HFS volume is actually a wrapper around an embedded HFS+ volume.
// If so, returns the byte offset of the embedded HFS+ volume within the image.
// partition_offset is the byte offset of the HFS partition in the image.
// Returns 0 if no embedded HFS+ volume is found.
static uint64_t detect_hfsplus_wrapper(const char* path, uint64_t partition_offset) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    // Read the HFS Master Directory Block (at offset 1024 from partition start)
    uint8_t mdb[162];
    if (fseek(f, (long)(partition_offset + 1024), SEEK_SET) != 0 ||
        fread(mdb, sizeof(mdb), 1, f) != 1) {
        fclose(f);
        return 0;
    }
    fclose(f);

    // Check HFS signature at offset 0: 0x4244 ('BD')
    uint16_t sig = ((uint16_t)mdb[0] << 8) | mdb[1];
    if (sig != 0x4244) return 0;

    // Check drEmbedSigWord at offset 0x7C: 0x482B ('H+') or 0x4858 ('HX')
    uint16_t embed_sig = ((uint16_t)mdb[0x7C] << 8) | mdb[0x7D];
    if (embed_sig != 0x482B && embed_sig != 0x4858) return 0;

    // Read allocation block size (drAlBlkSiz) at offset 0x14 (4 bytes BE)
    uint32_t al_blk_siz = ((uint32_t)mdb[0x14] << 24) | ((uint32_t)mdb[0x15] << 16) |
                           ((uint32_t)mdb[0x16] << 8) | mdb[0x17];

    // Read first allocation block offset (drAlBlSt) at offset 0x1C (2 bytes BE)
    // This is in 512-byte blocks from the start of the partition
    uint16_t al_bl_st = ((uint16_t)mdb[0x1C] << 8) | mdb[0x1D];

    // Read embedded extent start block at offset 0x7E (2 bytes BE)
    uint16_t embed_start = ((uint16_t)mdb[0x7E] << 8) | mdb[0x7F];

    // The embedded HFS+ volume starts at:
    //   partition_offset + (drAlBlSt * 512) + (embedStartBlock * drAlBlkSiz)
    uint64_t hfsplus_offset = partition_offset +
                              (uint64_t)al_bl_st * 512 +
                              (uint64_t)embed_start * al_blk_siz;

    fprintf(stderr, "hfsbrowser: HFS wrapper detected — embedded HFS+ at offset %llu "
            "(alBlkSiz=%u, alBlSt=%u, embedStart=%u)\n",
            (unsigned long long)hfsplus_offset, al_blk_siz, al_bl_st, embed_start);

    return hfsplus_offset;
}

void App::open_image(const char* path) {
    close_image();

    fprintf(stderr, "hfsbrowser: opening %s\n", path);

    // --- Try classic HFS first (via libhfs) ---
    int nparts = hfs_nparts(path);
    fprintf(stderr, "hfsbrowser: nparts=%d\n", nparts);

    int partitions_to_try[16];
    int ntry = 0;
    for (int i = 1; i <= nparts && ntry < 15; i++)
        partitions_to_try[ntry++] = i;
    partitions_to_try[ntry++] = 0;

    bool readonly = false;
    int mounted_pnum = -1;
    for (int t = 0; t < ntry && !vol_; t++) {
        int pnum = partitions_to_try[t];
        vol_ = hfs_mount(path, pnum, HFS_MODE_RDWR);
        if (!vol_) {
            vol_ = hfs_mount(path, pnum, HFS_MODE_RDONLY);
            if (vol_) readonly = true;
        }
        fprintf(stderr, "hfsbrowser: tried HFS pnum=%d -> %s\n", pnum,
                vol_ ? "ok" : (hfs_error ? hfs_error : "failed"));
        if (vol_) mounted_pnum = pnum;
    }

    // Collect APM partition offsets (needed for both wrapper detection and HFS+ fallback)
    APMPartition apm_parts[16];
    int napm = read_apm_partitions(path, apm_parts, 16);

    // If HFS mounted, check for HFS+ wrapper
    if (vol_) {
        // Determine the byte offset of the partition we mounted.
        // libhfs pnum counts only Apple_HFS partitions (1-based), so we need
        // to find the Nth Apple_HFS entry in the APM.
        uint64_t part_offset = 0;
        if (mounted_pnum > 0) {
            int hfs_idx = 0;
            for (int i = 0; i < napm; i++) {
                if (strstr(apm_parts[i].type, "Apple_HFS") != nullptr) {
                    hfs_idx++;
                    if (hfs_idx == mounted_pnum) {
                        part_offset = apm_parts[i].offset;
                        break;
                    }
                }
            }
        }

        uint64_t embed_offset = detect_hfsplus_wrapper(path, part_offset);
        if (embed_offset > 0) {
            // This is an HFS wrapper — close HFS and try HFS+ at the embedded offset
            fprintf(stderr, "hfsbrowser: closing HFS wrapper, trying embedded HFS+ at offset %llu\n",
                    (unsigned long long)embed_offset);
            hfs_umount(vol_);
            vol_ = nullptr;

            hfsplus_vol_ = hfsplus_open(path, embed_offset, false);
            if (!hfsplus_vol_) {
                hfsplus_vol_ = hfsplus_open(path, embed_offset, true);
                if (hfsplus_vol_) readonly = true;
            }

            if (hfsplus_vol_) {
                vol_type_ = VolumeType::HFSPLUS;
                image_path_ = path;

                if (readonly)
                    status_text_ = "Opened HFS+ (read-only): " + std::string(path);
                else
                    status_text_ = "Opened HFS+: " + std::string(path);

                volume_name_ = hfsplus_volume_name(hfsplus_vol_);
                vol_total_bytes_ = (unsigned long)hfsplus_total_bytes(hfsplus_vol_);
                vol_free_bytes_ = (unsigned long)hfsplus_free_bytes(hfsplus_vol_);
                blessed_cnid_ = hfsplus_get_blessed(hfsplus_vol_);

                current_path_ = volume_name_ + ":";
                current_cnid_ = 2; // kHFSRootFolderID
                refresh_listing();
                return;
            }

            // HFS+ at embedded offset failed — fall through to general HFS+ search
            fprintf(stderr, "hfsbrowser: embedded HFS+ open failed, trying other offsets\n");
        } else {
            // Genuine classic HFS volume — use it
            vol_type_ = VolumeType::HFS;
            image_path_ = path;

            if (readonly)
                status_text_ = "Opened HFS (read-only): " + std::string(path);
            else
                status_text_ = "Opened HFS: " + std::string(path);

            hfsvolent vstat;
            if (hfs_vstat(vol_, &vstat) == 0) {
                volume_name_ = vstat.name;
                vol_total_bytes_ = vstat.totbytes;
                vol_free_bytes_ = vstat.freebytes;
                blessed_cnid_ = vstat.blessed;
            }

            current_path_ = volume_name_ + ":";
            refresh_listing();
            return;
        }
    }

    // --- Classic HFS failed (or was a wrapper). Try HFS+ ---
    fprintf(stderr, "hfsbrowser: trying HFS+\n");

    // Build list of offsets to try: APM Apple_HFS partitions, then offset 0
    uint64_t offsets_to_try[17];
    int noffsets = 0;

    for (int i = 0; i < napm; i++) {
        if (strstr(apm_parts[i].type, "Apple_HFS") != nullptr) {
            // Also check each APM partition for HFS wrapper with embedded HFS+
            uint64_t embed = detect_hfsplus_wrapper(path, apm_parts[i].offset);
            if (embed > 0) {
                offsets_to_try[noffsets++] = embed;
                fprintf(stderr, "hfsbrowser: APM partition %d has HFS wrapper, embedded HFS+ at offset %llu\n",
                        i, (unsigned long long)embed);
            }
            offsets_to_try[noffsets++] = apm_parts[i].offset;
            fprintf(stderr, "hfsbrowser: APM partition at offset %llu type=%s\n",
                    (unsigned long long)apm_parts[i].offset, apm_parts[i].type);
        }
    }
    offsets_to_try[noffsets++] = 0;  // Also try raw (no partition map)

    for (int t = 0; t < noffsets && !hfsplus_vol_; t++) {
        uint64_t off = offsets_to_try[t];
        hfsplus_vol_ = hfsplus_open(path, off, false);
        if (!hfsplus_vol_) {
            hfsplus_vol_ = hfsplus_open(path, off, true);
            if (hfsplus_vol_) readonly = true;
        }
        fprintf(stderr, "hfsbrowser: tried HFS+ offset=%llu -> %s\n",
                (unsigned long long)off, hfsplus_vol_ ? "ok" : "failed");
    }

    if (!hfsplus_vol_) {
        set_error(std::string("Failed to open image: not a valid HFS or HFS+ volume"));
        return;
    }

    vol_type_ = VolumeType::HFSPLUS;
    image_path_ = path;

    if (readonly)
        status_text_ = "Opened HFS+ (read-only): " + std::string(path);
    else
        status_text_ = "Opened HFS+: " + std::string(path);

    volume_name_ = hfsplus_volume_name(hfsplus_vol_);
    vol_total_bytes_ = (unsigned long)hfsplus_total_bytes(hfsplus_vol_);
    vol_free_bytes_ = (unsigned long)hfsplus_free_bytes(hfsplus_vol_);
    blessed_cnid_ = hfsplus_get_blessed(hfsplus_vol_);

    current_path_ = volume_name_ + ":";
    current_cnid_ = 2; // kHFSRootFolderID
    refresh_listing();
}

void App::close_image() {
    cleanup_icons();
    if (vol_) {
        hfs_umount(vol_);
        vol_ = nullptr;
    }
    if (hfsplus_vol_) {
        hfsplus_close(hfsplus_vol_);
        hfsplus_vol_ = nullptr;
    }
    vol_type_ = VolumeType::NONE;
    image_path_.clear();
    volume_name_.clear();
    vol_total_bytes_ = 0;
    vol_free_bytes_ = 0;
    blessed_cnid_ = 0;
    current_path_.clear();
    current_cnid_ = 0;
    cnid_stack_.clear();
    entries_.clear();
    selected_entry_ = -1;
    status_text_ = "Open an HFS disk image to begin";
}

// --- Directory listing ---

void App::refresh_listing() {
    cleanup_icons();
    entries_.clear();
    selected_entry_ = -1;

    if (vol_type_ == VolumeType::NONE) return;

    if (vol_type_ == VolumeType::HFS) {
        hfsdir* dir = hfs_opendir(vol_, current_path_.c_str());
        if (!dir) {
            set_error(std::string("Failed to open directory: ") + (hfs_error ? hfs_error : "unknown"));
            return;
        }

        hfsdirent ent;
        while (hfs_readdir(dir, &ent) == 0) {
            HFSEntry e;
            e.name = ent.name;
            e.is_dir = (ent.flags & HFS_ISDIR) != 0;
            e.cnid = ent.cnid;
            e.fdflags = ent.fdflags;

            if (e.is_dir) {
                e.size = 0;
                e.rsize = 0;
                memset(e.type, 0, sizeof(e.type));
                memset(e.creator, 0, sizeof(e.creator));
            } else {
                e.size = ent.u.file.dsize;
                e.rsize = ent.u.file.rsize;
                memcpy(e.type, ent.u.file.type, 5);
                memcpy(e.creator, ent.u.file.creator, 5);
            }

            entries_.push_back(e);
        }

        hfs_closedir(dir);

        // Update volume stats
        hfsvolent vstat;
        if (hfs_vstat(vol_, &vstat) == 0) {
            vol_total_bytes_ = vstat.totbytes;
            vol_free_bytes_ = vstat.freebytes;
            blessed_cnid_ = vstat.blessed;
        }
    } else if (vol_type_ == VolumeType::HFSPLUS) {
        HFSPlusDirEntry* plus_entries = nullptr;
        int plus_count = 0;

        int rc;
        if (current_cnid_ != 0)
            rc = hfsplus_list_dir_by_cnid(hfsplus_vol_, (uint32_t)current_cnid_, &plus_entries, &plus_count);
        else
            rc = hfsplus_list_dir(hfsplus_vol_, current_path_.c_str(), &plus_entries, &plus_count);

        if (rc != 0) {
            set_error("Failed to open directory");
            return;
        }

        for (int i = 0; i < plus_count; i++) {
            HFSEntry e;
            e.name = plus_entries[i].name;
            e.is_dir = plus_entries[i].is_dir;
            e.cnid = plus_entries[i].cnid;
            e.parent_cnid = plus_entries[i].parent_cnid;
            e.fdflags = plus_entries[i].finder_flags;
            e.size = (unsigned long)plus_entries[i].data_size;
            e.rsize = (unsigned long)plus_entries[i].rsrc_size;
            memcpy(e.type, plus_entries[i].type, 5);
            memcpy(e.creator, plus_entries[i].creator, 5);
            entries_.push_back(e);
        }

        hfsplus_free_entries(plus_entries);

        vol_total_bytes_ = (unsigned long)hfsplus_total_bytes(hfsplus_vol_);
        vol_free_bytes_ = (unsigned long)hfsplus_free_bytes(hfsplus_vol_);
    }

    // Sort: directories first, then alphabetical
    std::sort(entries_.begin(), entries_.end(), [](const HFSEntry& a, const HFSEntry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    // Load custom icons for files that have them
    load_entry_icons();
}

// --- Icon loading ---

std::vector<uint8_t> App::read_rsrc_fork(const std::string& hfs_path) {
    if (vol_type_ == VolumeType::HFSPLUS) {
        // Try to find the CNID from current entries to avoid path lookup issues
        uint32_t cnid = 0;
        uint32_t pcnid = 0;
        for (const auto& ent : entries_) {
            std::string entry_path = current_path_ + ent.name;
            if (entry_path == hfs_path) {
                cnid = (uint32_t)ent.cnid;
                pcnid = (uint32_t)ent.parent_cnid;
                break;
            }
        }

        uint8_t* data = nullptr;
        size_t size = 0;
        int rc;
        if (cnid != 0)
            rc = hfsplus_read_file_by_cnid(hfsplus_vol_, cnid, pcnid, &data, &size, 1);
        else
            rc = hfsplus_read_file(hfsplus_vol_, hfs_path.c_str(), &data, &size, 1);

        if (rc == 0 && data) {
            std::vector<uint8_t> result(data, data + size);
            free(data);
            return result;
        }
        return {};
    }

    // Classic HFS path
    hfsfile* f = hfs_open(vol_, hfs_path.c_str());
    if (!f) return {};

    hfs_setfork(f, 1);  // switch to resource fork

    // Get resource fork size by seeking to end
    unsigned long size = hfs_seek(f, 0, HFS_SEEK_END);
    if (size == 0 || size == (unsigned long)-1) {
        hfs_close(f);
        return {};
    }

    hfs_seek(f, 0, HFS_SEEK_SET);

    std::vector<uint8_t> data(size);
    unsigned long total = 0;
    while (total < size) {
        unsigned long n = hfs_read(f, data.data() + total, size - total);
        if (n == 0 || n == (unsigned long)-1) break;
        total += n;
    }

    hfs_close(f);
    data.resize(total);
    return data;
}

GLuint App::create_icon_from_rsrc(const std::vector<uint8_t>& rsrc) {
    static const int16_t ids_to_try[] = { 128, -16455, 0 };

    uint32_t ICN_TYPE = ('I' << 24) | ('C' << 16) | ('N' << 8) | '#';
    uint32_t ICL8_TYPE = ('i' << 24) | ('c' << 16) | ('l' << 8) | '8';

    for (int i = 0; ids_to_try[i] != 0 || i < 2; i++) {
        // Try icl8 (8-bit color) first with ICN# mask
        uint32_t color_len = 0, mask_len = 0;
        const uint8_t* color_data = find_resource(rsrc, ICL8_TYPE, ids_to_try[i], &color_len);
        const uint8_t* mask_data = find_resource(rsrc, ICN_TYPE, ids_to_try[i], &mask_len);

        std::vector<uint8_t> rgba;

        if (color_data && color_len >= 1024) {
            rgba = icon_from_icl8(color_data, color_len, mask_data, mask_len);
        } else if (mask_data && mask_len >= 256) {
            // Fall back to 1-bit ICN#
            rgba = icon_from_icn(mask_data, mask_len);
        }

        if (rgba.empty()) continue;

        // rgba is 64x64 pixel-doubled
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        return tex;
    }
    return 0;
}

void App::load_entry_icons() {
    if (!has_volume()) return;

    for (auto& e : entries_) {
        if (e.is_dir) continue;

        // Only try if file has a resource fork
        if (e.rsize == 0) continue;

        // Try to load custom icon (files with kHasCustomIcon flag)
        // Also try any file with a resource fork — many Mac apps
        // store ICN# resources
        std::string path = current_path_ + e.name;
        std::vector<uint8_t> rsrc = read_rsrc_fork(path);
        if (rsrc.empty()) continue;

        GLuint tex = create_icon_from_rsrc(rsrc);
        if (tex) {
            e.icon_tex = tex;
            fprintf(stderr, "hfsbrowser: loaded icon for %s\n", e.name.c_str());
        }
    }
}

void App::cleanup_icons() {
    for (auto& e : entries_) {
        if (e.icon_tex) {
            glDeleteTextures(1, &e.icon_tex);
            e.icon_tex = 0;
        }
    }
}

// --- Navigation ---

void App::navigate_to(const char* dirname) {
    std::string new_path = current_path_ + dirname + ":";

    if (vol_type_ == VolumeType::HFS) {
        if (hfs_chdir(vol_, new_path.c_str()) == -1) {
            set_error(std::string("Failed to enter directory: ") + (hfs_error ? hfs_error : "unknown"));
            return;
        }
    } else if (vol_type_ == VolumeType::HFSPLUS) {
        // Find the folder CNID from current entries
        for (const auto& e : entries_) {
            if (e.is_dir && e.name == dirname) {
                cnid_stack_.push_back(current_cnid_);
                current_cnid_ = e.cnid;
                break;
            }
        }
    }

    current_path_ = new_path;
    refresh_listing();
}

void App::navigate_up() {
    if (current_path_ == volume_name_ + ":") return;

    std::string path = current_path_;
    if (!path.empty() && path.back() == ':') path.pop_back();
    size_t pos = path.rfind(':');
    if (pos == std::string::npos) return;

    std::string parent = path.substr(0, pos + 1);

    if (vol_type_ == VolumeType::HFS) {
        if (hfs_chdir(vol_, parent.c_str()) == -1) {
            set_error(std::string("Failed to navigate up: ") + (hfs_error ? hfs_error : "unknown"));
            return;
        }
    } else if (vol_type_ == VolumeType::HFSPLUS) {
        if (!cnid_stack_.empty()) {
            current_cnid_ = cnid_stack_.back();
            cnid_stack_.pop_back();
        }
    }

    current_path_ = parent;
    refresh_listing();
}

// --- Rendering ---

void App::render() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("HFS Browser", nullptr, window_flags);

    render_toolbar();
    ImGui::Separator();
    render_path_bar();
    ImGui::Separator();
    render_file_list();
    ImGui::Separator();
    render_action_bar();

    ImGui::TextDisabled("%s", status_text_.c_str());

    render_progress_bar();
    render_error_popup();
    render_confirm_popup();
    render_mkdir_popup();
    render_type_creator_popup();
    render_info_popup();
    render_file_picker();

    ImGui::End();
}

void App::render_toolbar() {
    if (ImGui::Button("Open Image")) {
        open_file_picker_for_open();
    }

    ImGui::SameLine();

    bool close_disabled = !has_volume();
    if (close_disabled) ImGui::BeginDisabled();
    if (ImGui::Button("Close")) {
        close_image();
    }
    if (close_disabled) ImGui::EndDisabled();

    if (has_volume()) {
        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();
        ImGui::Text("Volume: \"%s\"  (%s / %s free)",
            volume_name_.c_str(),
            format_size(vol_total_bytes_).c_str(),
            format_size(vol_free_bytes_).c_str());
    }
}

void App::render_path_bar() {
    if (!has_volume()) {
        ImGui::TextDisabled("No image open");
        return;
    }

    ImGui::Text("Path: %s", current_path_.c_str());
}

void App::render_file_list() {
    float avail_h = ImGui::GetContentRegionAvail().y - 60.0f;
    if (avail_h < 100.0f) avail_h = 100.0f;

    ImGui::BeginChild("FileList", ImVec2(0, avail_h), false);

    if (!has_volume()) {
        ImGui::TextDisabled("Open an HFS disk image to browse files");
        ImGui::EndChild();
        return;
    }

    // Parent directory entry
    if (current_path_ != volume_name_ + ":") {
        if (ImGui::Selectable("  ..  (parent directory)", false, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                navigate_up();
            }
        }
    }

    if (ImGui::BeginTable("files", 5,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoBordersInBodyUntilResize)) {

        float icon_sz = ImGui::GetTextLineHeight();

        ImGui::TableSetupColumn("##icon", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, icon_sz + 16);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type/Creator", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Data Fork", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Rsrc Fork", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)entries_.size(); i++) {
            const HFSEntry& e = entries_[i];

            // Check if file is invisible (Finder flag kIsInvisible = 0x4000)
            bool is_hidden = (e.fdflags & 0x4000) != 0;

            // Label color (bits 1-3 of fdflags)
            int label = (e.fdflags >> 1) & 0x07;
            static const ImU32 label_colors[] = {
                0,                             // 0: None
                IM_COL32(255, 160, 50, 50),    // 1: Orange
                IM_COL32(230, 50, 50, 50),     // 2: Red
                IM_COL32(240, 120, 180, 50),   // 3: Pink
                IM_COL32(70, 120, 230, 50),    // 4: Blue
                IM_COL32(50, 200, 210, 50),    // 5: Cyan
                IM_COL32(60, 190, 60, 50),     // 6: Green
                IM_COL32(160, 160, 160, 50),   // 7: Gray
            };

            ImGui::TableNextRow();

            // Draw label color background across the whole row
            if (label > 0) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, label_colors[label]);
            }

            // Icon column
            ImGui::TableNextColumn();

            // Dim hidden files
            if (is_hidden)
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);

            ImVec2 icon_pos = ImGui::GetCursorScreenPos();

            if (e.icon_tex) {
                ImGui::Image((ImTextureID)(intptr_t)e.icon_tex,
                             ImVec2(icon_sz, icon_sz));
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Image((ImTextureID)(intptr_t)e.icon_tex, ImVec2(128, 128));
                    ImGui::EndTooltip();
                }
            } else if (e.is_dir) {
                draw_folder_icon(icon_pos, icon_sz);
                ImGui::Dummy(ImVec2(icon_sz, icon_sz));
            } else {
                draw_file_icon(icon_pos, icon_sz);
                ImGui::Dummy(ImVec2(icon_sz, icon_sz));
            }

            // Name column
            ImGui::TableNextColumn();

            // Selectable name — show blessed indicator for system folder
            bool is_blessed = (e.is_dir && blessed_cnid_ != 0 && e.cnid == blessed_cnid_);
            char sel_id[80];
            if (is_blessed)
                snprintf(sel_id, sizeof(sel_id), "%s (blessed)##%d", e.name.c_str(), i);
            else
                snprintf(sel_id, sizeof(sel_id), "%s##%d", e.name.c_str(), i);
            bool selected = (selected_entry_ == i);
            if (ImGui::Selectable(sel_id, selected,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                selected_entry_ = i;
                if (ImGui::IsMouseDoubleClicked(0) && e.is_dir) {
                    navigate_to(e.name.c_str());
                }
            }

            // Right-click context menu
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                selected_entry_ = i;
            }
            char ctx_id[64];
            snprintf(ctx_id, sizeof(ctx_id), "ctx##%d", i);
            if (selected_entry_ == i && ImGui::BeginPopupContextItem(ctx_id)) {
                std::string ctx_hfs_path = current_path_ + e.name;

                if (!e.is_dir) {
                    // Default export: BinHex if has rsrc fork, regular otherwise
                    if (e.rsize > 0) {
                        if (ImGui::MenuItem("Export as BinHex (.hqx)")) {
                            picker_mode_ = PickerMode::EXPORT_BINHEX;
                            picker_refresh();
                        }
                        if (ImGui::MenuItem("Export (data fork + AppleDouble)")) {
                            open_file_picker_for_export();
                        }
                    } else {
                        if (ImGui::MenuItem("Export")) {
                            open_file_picker_for_export();
                        }
                        if (ImGui::MenuItem("Export as BinHex (.hqx)")) {
                            picker_mode_ = PickerMode::EXPORT_BINHEX;
                            picker_refresh();
                        }
                    }

                    if (e.icon_tex) {
                        if (ImGui::MenuItem("Save Icon as PNG")) {
                            picker_mode_ = PickerMode::EXPORT_ICON;
                            picker_refresh();
                        }
                    }

                    // Fix-A-Fork: detect and set type/creator for files missing it
                    bool has_tc = (e.type[0] && strcmp(e.type, "????") != 0 &&
                                   strcmp(e.type, "\0\0\0\0") != 0);
                    if (!has_tc) {
                        if (ImGui::MenuItem("Fix-A-Fork")) {
                            TypeCreatorResult tcr = {};
                            bool found = false;

                            // Try magic bytes from data fork
                            if (e.size > 0) {
                                uint8_t* data = nullptr;
                                size_t dsize = 0;
                                int rc = -1;
                                if (vol_type_ == VolumeType::HFS) {
                                    hfsfile* hf = hfs_open(vol_, ctx_hfs_path.c_str());
                                    if (hf) {
                                        uint8_t mbuf[1024];
                                        unsigned long nr = hfs_read(hf, mbuf, sizeof(mbuf));
                                        hfs_close(hf);
                                        if (nr > 0)
                                            found = detect_type_creator_magic(mbuf, nr, &tcr);
                                    }
                                } else if (vol_type_ == VolumeType::HFSPLUS) {
                                    // Read just enough for magic detection
                                    rc = hfsplus_read_file_by_cnid(hfsplus_vol_,
                                            (uint32_t)e.cnid, (uint32_t)e.parent_cnid,
                                            &data, &dsize, 0);
                                    if (rc == 0 && data && dsize > 0) {
                                        size_t check = dsize > 1024 ? 1024 : dsize;
                                        found = detect_type_creator_magic(data, check, &tcr);
                                        free(data);
                                    }
                                }
                            }

                            // Fall back to extension
                            if (!found)
                                found = detect_type_creator_ext(e.name.c_str(), &tcr);

                            if (found) {
                                if (vol_type_ == VolumeType::HFS) {
                                    hfsdirent ent;
                                    if (hfs_stat(vol_, ctx_hfs_path.c_str(), &ent) == 0) {
                                        memcpy(ent.u.file.type, tcr.type, 5);
                                        memcpy(ent.u.file.creator, tcr.creator, 5);
                                        hfs_setattr(vol_, ctx_hfs_path.c_str(), &ent);
                                    }
                                } else if (vol_type_ == VolumeType::HFSPLUS) {
                                    hfsplus_set_type_creator(hfsplus_vol_, ctx_hfs_path.c_str(),
                                                             tcr.type, tcr.creator);
                                }
                                // Update the displayed entry
                                entries_[i].type[0] = tcr.type[0]; entries_[i].type[1] = tcr.type[1];
                                entries_[i].type[2] = tcr.type[2]; entries_[i].type[3] = tcr.type[3];
                                entries_[i].type[4] = '\0';
                                entries_[i].creator[0] = tcr.creator[0]; entries_[i].creator[1] = tcr.creator[1];
                                entries_[i].creator[2] = tcr.creator[2]; entries_[i].creator[3] = tcr.creator[3];
                                entries_[i].creator[4] = '\0';
                                status_text_ = "Fixed: " + e.name + " → " + tcr.type + "/" + tcr.creator;
                            } else {
                                status_text_ = "Could not detect type/creator for " + e.name;
                            }
                        }
                    }

                    ImGui::Separator();
                    if (ImGui::MenuItem("Set Type/Creator")) {
                        memcpy(edit_type_, e.type, 5);
                        memcpy(edit_creator_, e.creator, 5);
                        show_type_creator_ = true;
                    }
                } else {
                    // Directory context menu
                    if (ImGui::MenuItem("Export Folder")) {
                        picker_mode_ = PickerMode::EXPORT_FILE;
                        picker_refresh();
                    }
                    if (ImGui::MenuItem("Export Folder as BinHex")) {
                        picker_mode_ = PickerMode::EXPORT_FOLDER_BINHEX;
                        picker_refresh();
                    }
                    ImGui::Separator();
                    bool already_blessed = (blessed_cnid_ != 0 && e.cnid == blessed_cnid_);
                    if (ImGui::MenuItem("Bless as System Folder", nullptr, already_blessed)) {
                        unsigned long new_blessed = already_blessed ? 0 : e.cnid;
                        if (vol_type_ == VolumeType::HFS) {
                            hfsvolent vstat;
                            if (hfs_vstat(vol_, &vstat) == 0) {
                                vstat.blessed = new_blessed;
                                if (hfs_vsetattr(vol_, &vstat) == 0)
                                    blessed_cnid_ = new_blessed;
                                else
                                    set_error(std::string("Failed to bless: ") + (hfs_error ? hfs_error : "unknown"));
                            }
                        } else if (vol_type_ == VolumeType::HFSPLUS) {
                            if (hfsplus_set_blessed(hfsplus_vol_, (uint32_t)new_blessed) == 0)
                                blessed_cnid_ = new_blessed;
                            else
                                set_error("Failed to bless folder on HFS+ volume");
                        }
                        if (blessed_cnid_ == new_blessed)
                            status_text_ = already_blessed ? "Unblessed: " + e.name : "Blessed: " + e.name;
                    }
                }

                ImGui::Separator();
                // Get Info — available for both files and directories
                if (ImGui::MenuItem("Get Info")) {
                    info_entry_idx_ = i;
                    info_fdflags_ = e.fdflags;
                    memcpy(info_type_, e.type, 5);
                    memcpy(info_creator_, e.creator, 5);
                    show_info_ = true;
                }
                if (ImGui::MenuItem("Delete")) {
                    confirm_text_ = "Delete \"" + e.name + "\"?";
                    confirm_target_ = ctx_hfs_path;
                    confirm_is_dir_ = e.is_dir;
                    show_confirm_ = true;
                }
                if (ImGui::MenuItem("New Folder")) {
                    memset(mkdir_name_, 0, sizeof(mkdir_name_));
                    show_mkdir_ = true;
                }
                ImGui::EndPopup();
            }

            // Type/Creator
            ImGui::TableNextColumn();
            if (!e.is_dir && e.type[0]) {
                ImGui::Text("%s/%s", e.type, e.creator);
            }

            // Data Fork Size
            ImGui::TableNextColumn();
            if (!e.is_dir) {
                ImGui::Text("%s", format_size(e.size).c_str());
            }

            // Resource Fork Size
            ImGui::TableNextColumn();
            if (!e.is_dir && e.rsize > 0) {
                ImGui::Text("%s", format_size(e.rsize).c_str());
            }

            if (is_hidden)
                ImGui::PopStyleVar();
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
}

void App::render_action_bar() {
    bool has_vol = has_volume();
    bool has_sel = (selected_entry_ >= 0 && selected_entry_ < (int)entries_.size());

    if (!has_vol) ImGui::BeginDisabled();

    if (ImGui::Button("Import")) {
        if (has_vol)
            open_file_picker_for_import();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Import a file or folder from your computer into the image");

    ImGui::SameLine();

    if (!has_sel) ImGui::BeginDisabled();
    if (ImGui::Button("Extract")) {
        if (has_sel) {
            const HFSEntry& e = entries_[selected_entry_];
            if (e.is_dir) {
                picker_mode_ = PickerMode::EXPORT_FILE;
                picker_refresh();
            } else {
                open_file_picker_for_export();
            }
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Extract selected file or folder from image to your computer");
    if (!has_sel) ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button("New Folder")) {
        memset(mkdir_name_, 0, sizeof(mkdir_name_));
        show_mkdir_ = true;
        ImGui::OpenPopup("New Folder");
    }

    ImGui::SameLine();

    if (!has_sel) ImGui::BeginDisabled();
    if (ImGui::Button("Delete")) {
        if (has_sel) {
            const HFSEntry& e = entries_[selected_entry_];
            confirm_text_ = "Delete \"" + e.name + "\"?";
            confirm_target_ = current_path_ + e.name;
            confirm_is_dir_ = e.is_dir;
            show_confirm_ = true;
            ImGui::OpenPopup("Confirm Delete");
        }
    }
    if (!has_sel) ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button("Refresh")) {
        refresh_listing();
    }

    if (!has_vol) ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("(right-click for more options)");
}

void App::render_progress_bar() {
    if (!show_progress_) return;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::ProgressBar(progress_, ImVec2(avail.x, 0), progress_text_.c_str());
}

void App::render_error_popup() {
    if (show_error_)
        ImGui::OpenPopup("Error");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", error_text_.c_str());
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            show_error_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::render_confirm_popup() {
    if (show_confirm_)
        ImGui::OpenPopup("Confirm Delete");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", confirm_text_.c_str());
        ImGui::Spacing();

        if (ImGui::Button("Delete", ImVec2(100, 0))) {
            if (delete_recursive(confirm_target_, confirm_is_dir_)) {
                status_text_ = "Deleted: " + confirm_target_;
                refresh_listing();
            } else {
                set_error("Delete failed: " + confirm_target_);
            }
            show_confirm_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            show_confirm_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::render_mkdir_popup() {
    if (show_mkdir_)
        ImGui::OpenPopup("New Folder");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("New Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Folder name:");
        bool enter = ImGui::InputText("##dirname", mkdir_name_, sizeof(mkdir_name_),
            ImGuiInputTextFlags_EnterReturnsTrue);

        if (enter || ImGui::Button("Create", ImVec2(100, 0))) {
            if (strlen(mkdir_name_) > 0 && strlen(mkdir_name_) <= HFS_MAX_FLEN) {
                std::string full_path = current_path_ + mkdir_name_;
                int rc = -1;
                if (vol_type_ == VolumeType::HFS) {
                    rc = hfs_mkdir(vol_, full_path.c_str());
                    if (rc == -1)
                        set_error(std::string("mkdir failed: ") + (hfs_error ? hfs_error : "unknown"));
                } else if (vol_type_ == VolumeType::HFSPLUS) {
                    rc = hfsplus_mkdir(hfsplus_vol_, full_path.c_str());
                    if (rc != 0)
                        set_error("mkdir failed on HFS+ volume");
                }
                if (rc == 0) {
                    status_text_ = "Created folder: " + std::string(mkdir_name_);
                    refresh_listing();
                }
            }
            show_mkdir_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            show_mkdir_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::render_type_creator_popup() {
    if (show_type_creator_)
        ImGui::OpenPopup("Edit Type/Creator");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Edit Type/Creator", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Type (4 chars):");
        ImGui::InputText("##type", edit_type_, 5);
        ImGui::Text("Creator (4 chars):");
        ImGui::InputText("##creator", edit_creator_, 5);
        ImGui::Spacing();

        bool enter = ImGui::Button("Apply", ImVec2(100, 0));
        if (enter && selected_entry_ >= 0 && selected_entry_ < (int)entries_.size()) {
            HFSEntry& e = entries_[selected_entry_];
            std::string hfs_path = current_path_ + e.name;

            // Pad to 4 chars with spaces
            char new_type[5] = "    ";
            char new_creator[5] = "    ";
            for (int j = 0; j < 4 && edit_type_[j]; j++) new_type[j] = edit_type_[j];
            for (int j = 0; j < 4 && edit_creator_[j]; j++) new_creator[j] = edit_creator_[j];
            new_type[4] = '\0';
            new_creator[4] = '\0';

            if (vol_type_ == VolumeType::HFS) {
                hfsdirent ent;
                if (hfs_stat(vol_, hfs_path.c_str(), &ent) == 0) {
                    memcpy(ent.u.file.type, new_type, 5);
                    memcpy(ent.u.file.creator, new_creator, 5);
                    if (hfs_setattr(vol_, hfs_path.c_str(), &ent) == -1)
                        set_error(std::string("Failed to set type/creator: ") + (hfs_error ? hfs_error : "unknown"));
                    else {
                        memcpy(e.type, new_type, 5);
                        memcpy(e.creator, new_creator, 5);
                        status_text_ = "Set type/creator on " + e.name;
                    }
                }
            } else if (vol_type_ == VolumeType::HFSPLUS) {
                if (hfsplus_set_type_creator(hfsplus_vol_, hfs_path.c_str(),
                                             new_type, new_creator) != 0)
                    set_error("Failed to set type/creator on HFS+ volume");
                else {
                    memcpy(e.type, new_type, 5);
                    memcpy(e.creator, new_creator, 5);
                    status_text_ = "Set type/creator on " + e.name;
                }
            }

            show_type_creator_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            show_type_creator_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::render_info_popup() {
    if (show_info_)
        ImGui::OpenPopup("Get Info");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Get Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (info_entry_idx_ < 0 || info_entry_idx_ >= (int)entries_.size()) {
            show_info_ = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        HFSEntry& e = entries_[info_entry_idx_];
        std::string hfs_path = current_path_ + e.name;

        ImGui::Text("Name: %s", e.name.c_str());
        ImGui::Text("CNID: %lu", e.cnid);
        ImGui::Text("Kind: %s", e.is_dir ? "Folder" : "File");
        ImGui::Separator();

        if (!e.is_dir) {
            ImGui::Text("Data Fork: %s", format_size(e.size).c_str());
            ImGui::Text("Rsrc Fork: %s", format_size(e.rsize).c_str());
            ImGui::Separator();

            ImGui::Text("Type:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            ImGui::InputText("##infotype", info_type_, 5);
            ImGui::SameLine();
            ImGui::Text("Creator:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            ImGui::InputText("##infocreator", info_creator_, 5);
            ImGui::Separator();
        }

        ImGui::Text("Finder Flags:");

        // Finder flag checkboxes
        bool f;

        f = (info_fdflags_ & 0x4000) != 0;
        if (ImGui::Checkbox("Invisible", &f))
            info_fdflags_ = f ? (info_fdflags_ | 0x4000) : (info_fdflags_ & ~0x4000);

        f = (info_fdflags_ & 0x0400) != 0;
        if (ImGui::Checkbox("Has Custom Icon", &f))
            info_fdflags_ = f ? (info_fdflags_ | 0x0400) : (info_fdflags_ & ~0x0400);

        f = (info_fdflags_ & 0x2000) != 0;
        if (ImGui::Checkbox("Has Bundle (BNDL)", &f))
            info_fdflags_ = f ? (info_fdflags_ | 0x2000) : (info_fdflags_ & ~0x2000);

        f = (info_fdflags_ & 0x1000) != 0;
        if (ImGui::Checkbox("Name Locked", &f))
            info_fdflags_ = f ? (info_fdflags_ | 0x1000) : (info_fdflags_ & ~0x1000);

        f = (info_fdflags_ & 0x0800) != 0;
        if (ImGui::Checkbox("Stationery Pad", &f))
            info_fdflags_ = f ? (info_fdflags_ | 0x0800) : (info_fdflags_ & ~0x0800);

        f = (info_fdflags_ & 0x8000) != 0;
        if (ImGui::Checkbox("Is Alias", &f))
            info_fdflags_ = f ? (info_fdflags_ | 0x8000) : (info_fdflags_ & ~0x8000);

        f = (info_fdflags_ & 0x0100) != 0;
        if (ImGui::Checkbox("Has Been Inited", &f))
            info_fdflags_ = f ? (info_fdflags_ | 0x0100) : (info_fdflags_ & ~0x0100);

        f = (info_fdflags_ & 0x0040) != 0;
        if (ImGui::Checkbox("Shared (no write to rsrc)", &f))
            info_fdflags_ = f ? (info_fdflags_ | 0x0040) : (info_fdflags_ & ~0x0040);

        f = (info_fdflags_ & 0x0080) != 0;
        if (ImGui::Checkbox("Has No INITs", &f))
            info_fdflags_ = f ? (info_fdflags_ | 0x0080) : (info_fdflags_ & ~0x0080);

        // Label color (bits 1-3)
        int label = (info_fdflags_ >> 1) & 0x07;
        const char* label_names[] = { "None", "Orange", "Red", "Pink", "Blue", "Cyan", "Green", "Gray" };
        if (ImGui::Combo("Label", &label, label_names, 8)) {
            info_fdflags_ = (info_fdflags_ & ~0x000E) | ((label & 0x07) << 1);
        }

        ImGui::Text("Raw flags: 0x%04X", (unsigned)info_fdflags_ & 0xFFFF);
        ImGui::Spacing();

        if (ImGui::Button("Apply", ImVec2(100, 0))) {
            // Pad type/creator to 4 chars
            char new_type[5] = "    ";
            char new_creator[5] = "    ";
            for (int j = 0; j < 4 && info_type_[j]; j++) new_type[j] = info_type_[j];
            for (int j = 0; j < 4 && info_creator_[j]; j++) new_creator[j] = info_creator_[j];
            new_type[4] = '\0';
            new_creator[4] = '\0';

            if (vol_type_ == VolumeType::HFS) {
                hfsdirent ent;
                if (hfs_stat(vol_, hfs_path.c_str(), &ent) == 0) {
                    ent.fdflags = info_fdflags_;
                    if (!e.is_dir) {
                        memcpy(ent.u.file.type, new_type, 5);
                        memcpy(ent.u.file.creator, new_creator, 5);
                    }
                    if (hfs_setattr(vol_, hfs_path.c_str(), &ent) == -1)
                        set_error(std::string("Failed: ") + (hfs_error ? hfs_error : "unknown"));
                    else {
                        e.fdflags = info_fdflags_;
                        if (!e.is_dir) {
                            memcpy(e.type, new_type, 5);
                            memcpy(e.creator, new_creator, 5);
                        }
                        status_text_ = "Updated info for " + e.name;
                    }
                }
            } else if (vol_type_ == VolumeType::HFSPLUS) {
                // For HFS+ we can set type/creator; flags TODO
                if (!e.is_dir) {
                    hfsplus_set_type_creator(hfsplus_vol_, hfs_path.c_str(),
                                             new_type, new_creator);
                    memcpy(e.type, new_type, 5);
                    memcpy(e.creator, new_creator, 5);
                }
                e.fdflags = info_fdflags_;
                status_text_ = "Updated info for " + e.name;
            }

            show_info_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            show_info_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// --- File picker ---

void App::render_file_picker() {
    if (picker_mode_ == PickerMode::NONE) return;

    const char* title = "Open HFS Image";
    if (picker_mode_ == PickerMode::EXPORT_FILE || picker_mode_ == PickerMode::EXPORT_BINHEX ||
        picker_mode_ == PickerMode::EXPORT_FOLDER_BINHEX || picker_mode_ == PickerMode::EXPORT_ICON)
        title = "Save To";
    else if (picker_mode_ == PickerMode::IMPORT_FILE) title = "Select File to Import";

    if (!ImGui::IsPopupOpen(title)) {
        // Re-refresh entries when the popup is about to open for the first time
        picker_refresh();
    }
    ImGui::OpenPopup(title);

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_None)) {
        ImGui::Text("Path:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##path", picker_input_, sizeof(picker_input_),
                ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::error_code ec2;
            auto fstatus = fs::status(picker_input_, ec2);
            if (!ec2) {
                if (fs::is_directory(fstatus)) {
                    picker_navigate(picker_input_);
                } else {
                    std::string selected = picker_input_;
                    PickerMode mode = picker_mode_;
                    picker_mode_ = PickerMode::NONE;
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();

                    if (mode == PickerMode::OPEN_IMAGE) {
                        open_image(selected.c_str());
                    } else if (mode == PickerMode::IMPORT_FILE) {
                        copy_to_hfs_impl(selected);
                    }
                    return;
                }
            }
        }

        ImGui::Spacing();

        ImGui::BeginChild("PickerList", ImVec2(0, -30), true);

        if (ImGui::Selectable("  ..", false, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                std::string parent = picker_path_;
                size_t pos = parent.rfind('/');
                if (pos != std::string::npos && pos > 0)
                    picker_navigate(parent.substr(0, pos));
                else
                    picker_navigate("/");
            }
        }

        for (int i = 0; i < (int)picker_entries_.size(); i++) {
            const std::string& entry = picker_entries_[i];
            bool is_dir = (!entry.empty() && entry.back() == '/');

            bool selected = (picker_selected_ == i);
            if (ImGui::Selectable(entry.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                picker_selected_ = i;
                std::string full = picker_path_ + "/" + entry;
                if (is_dir) full.pop_back();
                snprintf(picker_input_, sizeof(picker_input_), "%s", full.c_str());

                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (is_dir) {
                        picker_navigate(full);
                    } else {
                        PickerMode mode = picker_mode_;
                        picker_mode_ = PickerMode::NONE;
                        ImGui::CloseCurrentPopup();
                        ImGui::EndChild();
                        ImGui::EndPopup();

                        if (mode == PickerMode::OPEN_IMAGE) {
                            open_image(full.c_str());
                        } else if (mode == PickerMode::IMPORT_FILE) {
                            copy_to_hfs_impl(full);
                        } else if (mode == PickerMode::EXPORT_FILE) {
                            copy_from_hfs_impl(full);
                        } else if (mode == PickerMode::EXPORT_BINHEX) {
                            if (selected_entry_ >= 0 && selected_entry_ < (int)entries_.size()) {
                                const HFSEntry& e = entries_[selected_entry_];
                                std::string hfs_path = current_path_ + e.name;
                                std::string out = full;
                                if (fs::is_directory(full))
                                    out = full + "/" + e.name + ".hqx";
                                export_as_binhex(out, e, hfs_path);
                            }
                        } else if (mode == PickerMode::EXPORT_FOLDER_BINHEX) {
                            if (selected_entry_ >= 0 && selected_entry_ < (int)entries_.size()) {
                                const HFSEntry& e = entries_[selected_entry_];
                                std::string hfs_path = current_path_ + e.name + ":";
                                std::string host_name = (vol_type_ == VolumeType::HFS)
                                    ? macroman_to_utf8(e.name) : e.name;
                                std::string dest = full;
                                if (fs::is_directory(full))
                                    dest = full + "/" + host_name;
                                show_progress_ = true;
                                export_folder_binhex(hfs_path, dest, e.cnid);
                                show_progress_ = false;
                                status_text_ = "Exported folder as BinHex: " + e.name;
                            }
                        } else if (mode == PickerMode::EXPORT_ICON) {
                            if (selected_entry_ >= 0 && selected_entry_ < (int)entries_.size()) {
                                const HFSEntry& e = entries_[selected_entry_];
                                std::string hfs_path = current_path_ + e.name;
                                std::string out = full;
                                if (fs::is_directory(full))
                                    out = full + "/" + e.name + ".png";
                                export_icon_png(out, e, hfs_path);
                            }
                        }
                        return;
                    }
                }
            }
        }

        ImGui::EndChild();

        if (ImGui::Button("Select", ImVec2(80, 0))) {
            std::string selected = picker_input_;
            PickerMode mode = picker_mode_;
            picker_mode_ = PickerMode::NONE;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();

            if (!selected.empty()) {
                if (mode == PickerMode::OPEN_IMAGE) {
                    open_image(selected.c_str());
                } else if (mode == PickerMode::IMPORT_FILE) {
                    copy_to_hfs_impl(selected);
                } else if (mode == PickerMode::EXPORT_FILE) {
                    copy_from_hfs_impl(selected);
                } else if (mode == PickerMode::EXPORT_BINHEX) {
                    if (selected_entry_ >= 0 && selected_entry_ < (int)entries_.size()) {
                        const HFSEntry& e = entries_[selected_entry_];
                        std::string hfs_path = current_path_ + e.name;
                        std::string out = selected;
                        if (fs::is_directory(selected))
                            out = selected + "/" + e.name + ".hqx";
                        export_as_binhex(out, e, hfs_path);
                    }
                } else if (mode == PickerMode::EXPORT_FOLDER_BINHEX) {
                    if (selected_entry_ >= 0 && selected_entry_ < (int)entries_.size()) {
                        const HFSEntry& e = entries_[selected_entry_];
                        std::string hfs_path = current_path_ + e.name + ":";
                        std::string host_name = (vol_type_ == VolumeType::HFS)
                            ? macroman_to_utf8(e.name) : e.name;
                        std::string dest = selected;
                        if (fs::is_directory(selected))
                            dest = selected + "/" + host_name;
                        show_progress_ = true;
                        export_folder_binhex(hfs_path, dest);
                        show_progress_ = false;
                        status_text_ = "Exported folder as BinHex: " + e.name;
                    }
                } else if (mode == PickerMode::EXPORT_ICON) {
                    if (selected_entry_ >= 0 && selected_entry_ < (int)entries_.size()) {
                        const HFSEntry& e = entries_[selected_entry_];
                        std::string hfs_path = current_path_ + e.name;
                        std::string out = selected;
                        if (fs::is_directory(selected))
                            out = selected + "/" + e.name + ".png";
                        export_icon_png(out, e, hfs_path);
                    }
                }
            }
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            picker_mode_ = PickerMode::NONE;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void App::picker_refresh() {
    picker_entries_.clear();
    picker_selected_ = -1;

    std::error_code ec;
    std::vector<std::string> dirs, files;
    for (const auto& entry : fs::directory_iterator(picker_path_, ec)) {
        if (ec) break;
        std::string name = entry.path().filename().string();
        if (name.empty() || name == "." || name == "..") continue;
        if (!picker_show_hidden_ && name[0] == '.') continue;

        std::error_code ec2;
        if (entry.is_directory(ec2)) {
            dirs.push_back(name + "/");
        } else if (!ec2) {
            files.push_back(name);
        }
    }

    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());

    picker_entries_.insert(picker_entries_.end(), dirs.begin(), dirs.end());
    picker_entries_.insert(picker_entries_.end(), files.begin(), files.end());

    snprintf(picker_input_, sizeof(picker_input_), "%s", picker_path_.c_str());
}

void App::picker_navigate(const std::string& path) {
    picker_path_ = path;
    picker_refresh();
}

void App::open_file_picker_for_open() {
    picker_mode_ = PickerMode::OPEN_IMAGE;
    picker_refresh();
}

void App::open_file_picker_for_export() {
    if (selected_entry_ < 0) return;
    picker_mode_ = PickerMode::EXPORT_FILE;
    picker_refresh();
}

void App::open_file_picker_for_import() {
    picker_mode_ = PickerMode::IMPORT_FILE;
    picker_refresh();
}

void App::copy_from_hfs() {
    if (!has_volume() || selected_entry_ < 0) return;
    open_file_picker_for_export();
}

void App::copy_to_hfs() {
    if (!has_volume()) return;
    open_file_picker_for_import();
}

void App::import_file(const char* path) {
    if (!has_volume()) return;
    copy_to_hfs_impl(path);
}

// --- AppleDouble export ---

/*
 * AppleDouble format (._filename):
 *   Header: magic(4) + version(4) + filler(16) + num_entries(2) = 26 bytes
 *   Entry descriptors: 2 * (id(4) + offset(4) + length(4)) = 24 bytes
 *   Finder Info: 32 bytes (FileInfo + ExtendedFileInfo)
 *   Resource fork: variable
 */
bool App::write_appledouble(const std::string& host_path,
                            const char* type, const char* creator,
                            short fdflags,
                            const std::vector<uint8_t>& rsrc_data) {
    // Build ._filename path
    std::string dir, base;
    size_t slash = host_path.rfind('/');
    if (slash != std::string::npos) {
        dir = host_path.substr(0, slash + 1);
        base = host_path.substr(slash + 1);
    } else {
        dir = "";
        base = host_path;
    }
    std::string ad_path = dir + "._" + base;

    // Layout
    const uint32_t header_size = 26;
    const uint32_t num_entries = 2;
    const uint32_t entry_desc_size = num_entries * 12;
    const uint32_t finder_info_offset = header_size + entry_desc_size; // 50
    const uint32_t finder_info_len = 32;
    const uint32_t rsrc_offset = finder_info_offset + finder_info_len; // 82
    const uint32_t rsrc_len = (uint32_t)rsrc_data.size();

    std::vector<uint8_t> ad(rsrc_offset + rsrc_len, 0);

    // Header
    write_u32be(ad.data() + 0, 0x00051607);  // AppleDouble magic
    write_u32be(ad.data() + 4, 0x00020000);  // Version 2.0
    // bytes 8-23: filler (zeros)
    write_u16be(ad.data() + 24, num_entries);

    // Entry 1: Finder Info (ID=9)
    write_u32be(ad.data() + 26, 9);
    write_u32be(ad.data() + 30, finder_info_offset);
    write_u32be(ad.data() + 34, finder_info_len);

    // Entry 2: Resource Fork (ID=2)
    write_u32be(ad.data() + 38, 2);
    write_u32be(ad.data() + 42, rsrc_offset);
    write_u32be(ad.data() + 46, rsrc_len);

    // Finder Info: type(4) + creator(4) + flags(2) + location(4) + folder(2) + extended(16) = 32
    uint8_t* fi = ad.data() + finder_info_offset;
    if (type && strlen(type) == 4)
        memcpy(fi + 0, type, 4);
    if (creator && strlen(creator) == 4)
        memcpy(fi + 4, creator, 4);
    write_u16be(fi + 8, (uint16_t)fdflags);
    // location and extended: zeros

    // Resource fork data
    if (rsrc_len > 0) {
        memcpy(ad.data() + rsrc_offset, rsrc_data.data(), rsrc_len);
    }

    FILE* f = fopen(ad_path.c_str(), "wb");
    if (!f) {
        fprintf(stderr, "hfsbrowser: failed to create AppleDouble file: %s\n", ad_path.c_str());
        return false;
    }

    bool ok = fwrite(ad.data(), 1, ad.size(), f) == ad.size();
    fclose(f);

    if (ok) {
        fprintf(stderr, "hfsbrowser: wrote AppleDouble: %s (%zu bytes, rsrc=%u)\n",
                ad_path.c_str(), ad.size(), rsrc_len);
    }
    return ok;
}

// --- Copy operations ---

// Export a single file entry to a host directory
void App::export_entry(const HFSEntry& e, const std::string& hfs_path,
                       const std::string& host_dir) {
    // Convert MacRoman filename to UTF-8 for the host filesystem
    std::string host_name = (vol_type_ == VolumeType::HFS)
        ? macroman_to_utf8(e.name) : e.name;
    std::string out_path = host_dir + "/" + host_name;

    progress_text_ = e.name;

    // Check host disk free space
    if (e.size > 0) {
        uint64_t host_free = platform_free_space(host_dir.c_str());
        if (host_free > 0 && (uint64_t)e.size > host_free) {
            set_error("Not enough disk space: need " + format_size(e.size) +
                      ", free " + format_size((unsigned long)host_free));
            return;
        }
    }

    unsigned long total = 0;
    bool ok = false;

    if (vol_type_ == VolumeType::HFS) {
        hfsfile* f = hfs_open(vol_, hfs_path.c_str());
        if (!f) {
            fprintf(stderr, "hfsbrowser: export failed: %s\n", hfs_path.c_str());
            return;
        }

        FILE* out = fopen(out_path.c_str(), "wb");
        if (!out) { hfs_close(f); return; }

        char buf[8192];
        unsigned long n;
        ok = true;
        while ((n = hfs_read(f, buf, sizeof(buf))) > 0) {
            if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
            total += n;
            if (e.size > 0) progress_ = (float)total / (float)e.size;
        }
        fclose(out);
        hfs_close(f);
    } else if (vol_type_ == VolumeType::HFSPLUS) {
        uint8_t* data = nullptr;
        size_t size = 0;
        if (hfsplus_read_file_by_cnid(hfsplus_vol_, (uint32_t)e.cnid, (uint32_t)e.parent_cnid, &data, &size, 0) != 0)
            return;

        FILE* out = fopen(out_path.c_str(), "wb");
        if (!out) { free(data); return; }

        if (size > 0 && data) {
            ok = (fwrite(data, 1, size, out) == size);
            total = (unsigned long)size;
        } else {
            ok = true;
        }
        fclose(out);
        free(data);
    }

    if (!ok) return;

    // Export resource fork + Finder info as AppleDouble
    std::vector<uint8_t> rsrc;
    if (e.rsize > 0) {
        if (vol_type_ == VolumeType::HFSPLUS) {
            uint8_t* rdata = nullptr;
            size_t rsize = 0;
            if (hfsplus_read_file_by_cnid(hfsplus_vol_, (uint32_t)e.cnid, (uint32_t)e.parent_cnid, &rdata, &rsize, 1) == 0 && rdata) {
                rsrc.assign(rdata, rdata + rsize);
                free(rdata);
            }
        } else {
            rsrc = read_rsrc_fork(hfs_path);
        }
    }

    if ((e.type[0] && strcmp(e.type, "????") != 0) || !rsrc.empty())
        write_appledouble(out_path, e.type, e.creator, e.fdflags, rsrc);

    fprintf(stderr, "hfsbrowser: exported %s (%lu bytes)\n", e.name.c_str(), total);
}

// Recursively export a folder from the image to a host directory
void App::export_folder(const std::string& hfs_dir_path, const std::string& host_dir,
                        unsigned long folder_cnid) {
    platform_mkdir(host_dir.c_str());

    if (vol_type_ == VolumeType::HFS) {
        hfsdir* dir = hfs_opendir(vol_, hfs_dir_path.c_str());
        if (!dir) return;

        hfsdirent ent;
        while (hfs_readdir(dir, &ent) == 0) {
            HFSEntry e;
            e.name = ent.name;
            e.is_dir = (ent.flags & HFS_ISDIR) != 0;
            e.cnid = ent.cnid;
            e.fdflags = ent.fdflags;
            if (!e.is_dir) {
                e.size = ent.u.file.dsize;
                e.rsize = ent.u.file.rsize;
                memcpy(e.type, ent.u.file.type, 5);
                memcpy(e.creator, ent.u.file.creator, 5);
            } else {
                e.size = 0; e.rsize = 0;
                memset(e.type, 0, 5); memset(e.creator, 0, 5);
            }

            std::string child_hfs = hfs_dir_path + e.name;
            std::string host_name = macroman_to_utf8(e.name);
            if (e.is_dir)
                export_folder(child_hfs + ":", host_dir + "/" + host_name);
            else
                export_entry(e, child_hfs, host_dir);
        }
        hfs_closedir(dir);
    } else if (vol_type_ == VolumeType::HFSPLUS) {
        HFSPlusDirEntry* plus_entries = nullptr;
        int plus_count = 0;
        int rc = (folder_cnid != 0)
            ? hfsplus_list_dir_by_cnid(hfsplus_vol_, (uint32_t)folder_cnid, &plus_entries, &plus_count)
            : hfsplus_list_dir(hfsplus_vol_, hfs_dir_path.c_str(), &plus_entries, &plus_count);
        if (rc != 0) return;

        for (int i = 0; i < plus_count; i++) {
            HFSEntry e;
            e.name = plus_entries[i].name;
            e.is_dir = plus_entries[i].is_dir;
            e.cnid = plus_entries[i].cnid;
            e.parent_cnid = plus_entries[i].parent_cnid;
            e.fdflags = plus_entries[i].finder_flags;
            e.size = (unsigned long)plus_entries[i].data_size;
            e.rsize = (unsigned long)plus_entries[i].rsrc_size;
            memcpy(e.type, plus_entries[i].type, 5);
            memcpy(e.creator, plus_entries[i].creator, 5);

            std::string child_hfs = hfs_dir_path + e.name;
            if (e.is_dir)
                export_folder(child_hfs + ":", host_dir + "/" + e.name, e.cnid);
            else
                export_entry(e, child_hfs, host_dir);
        }
        hfsplus_free_entries(plus_entries);
    }
}

// Recursively export a folder, encoding every file as BinHex
void App::export_folder_binhex(const std::string& hfs_dir_path, const std::string& host_dir,
                               unsigned long folder_cnid) {
    platform_mkdir(host_dir.c_str());

    if (vol_type_ == VolumeType::HFS) {
        hfsdir* dir = hfs_opendir(vol_, hfs_dir_path.c_str());
        if (!dir) return;

        hfsdirent ent;
        while (hfs_readdir(dir, &ent) == 0) {
            HFSEntry e;
            e.name = ent.name;
            e.is_dir = (ent.flags & HFS_ISDIR) != 0;
            e.cnid = ent.cnid;
            e.fdflags = ent.fdflags;
            if (!e.is_dir) {
                e.size = ent.u.file.dsize;
                e.rsize = ent.u.file.rsize;
                memcpy(e.type, ent.u.file.type, 5);
                memcpy(e.creator, ent.u.file.creator, 5);
            } else {
                e.size = 0; e.rsize = 0;
                memset(e.type, 0, 5); memset(e.creator, 0, 5);
            }

            std::string child_hfs = hfs_dir_path + e.name;
            std::string host_name = macroman_to_utf8(e.name);
            if (e.is_dir) {
                export_folder_binhex(child_hfs + ":", host_dir + "/" + host_name);
            } else {
                std::string out = host_dir + "/" + host_name + ".hqx";
                progress_text_ = e.name;
                export_as_binhex(out, e, child_hfs);
            }
        }
        hfs_closedir(dir);
    } else if (vol_type_ == VolumeType::HFSPLUS) {
        HFSPlusDirEntry* plus_entries = nullptr;
        int plus_count = 0;
        int rc = (folder_cnid != 0)
            ? hfsplus_list_dir_by_cnid(hfsplus_vol_, (uint32_t)folder_cnid, &plus_entries, &plus_count)
            : hfsplus_list_dir(hfsplus_vol_, hfs_dir_path.c_str(), &plus_entries, &plus_count);
        if (rc != 0) return;

        for (int i = 0; i < plus_count; i++) {
            HFSEntry e;
            e.name = plus_entries[i].name;
            e.is_dir = plus_entries[i].is_dir;
            e.cnid = plus_entries[i].cnid;
            e.parent_cnid = plus_entries[i].parent_cnid;
            e.fdflags = plus_entries[i].finder_flags;
            e.size = (unsigned long)plus_entries[i].data_size;
            e.rsize = (unsigned long)plus_entries[i].rsrc_size;
            memcpy(e.type, plus_entries[i].type, 5);
            memcpy(e.creator, plus_entries[i].creator, 5);

            std::string child_hfs = hfs_dir_path + e.name;
            if (e.is_dir) {
                export_folder_binhex(child_hfs + ":", host_dir + "/" + e.name, e.cnid);
            } else {
                std::string out = host_dir + "/" + e.name + ".hqx";
                progress_text_ = e.name;
                export_as_binhex(out, e, child_hfs);
            }
        }
        hfsplus_free_entries(plus_entries);
    }
}

// Recursively import a host directory into the current HFS directory
void App::import_host_dir(const std::string& host_dir) {
    std::error_code ec;
    if (!fs::is_directory(host_dir, ec)) return;

    // Get directory name and sanitize for the target filesystem
    std::string raw_dirname = fs::path(host_dir).filename().string();

    std::string dirname;
    if (vol_type_ == VolumeType::HFS)
        dirname = sanitize_hfs_name(raw_dirname);
    else
        dirname = sanitize_hfsplus_name(raw_dirname);

    // Create folder on the image
    std::string hfs_folder = current_path_ + dirname;
    if (vol_type_ == VolumeType::HFS)
        hfs_mkdir(vol_, hfs_folder.c_str());
    else if (vol_type_ == VolumeType::HFSPLUS)
        hfsplus_mkdir(hfsplus_vol_, hfs_folder.c_str());

    // Save and change current path
    std::string saved_path = current_path_;
    current_path_ = hfs_folder + ":";
    if (vol_type_ == VolumeType::HFS)
        hfs_chdir(vol_, current_path_.c_str());

    for (const auto& entry : fs::directory_iterator(host_dir, ec)) {
        if (ec) break;
        std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') continue; // Skip hidden/AppleDouble files

        if (entry.is_directory(ec)) {
            import_host_dir(entry.path().string());
        } else if (entry.is_regular_file(ec)) {
            copy_to_hfs_impl(entry.path().string());
        }
    }

    // Restore path
    current_path_ = saved_path;
    if (vol_type_ == VolumeType::HFS)
        hfs_chdir(vol_, current_path_.c_str());
}

void App::copy_from_hfs_impl(const std::string& host_path) {
    if (!has_volume() || selected_entry_ < 0 || selected_entry_ >= (int)entries_.size()) return;
    const HFSEntry& e = entries_[selected_entry_];

    std::string hfs_path = current_path_ + e.name;

    // Determine output directory
    std::string out_dir = host_path;
    if (!fs::is_directory(host_path)) {
        size_t slash = host_path.rfind('/');
        if (slash != std::string::npos)
            out_dir = host_path.substr(0, slash);
        else
            out_dir = ".";
    }

    show_progress_ = true;
    progress_ = 0.0f;

    if (e.is_dir) {
        progress_text_ = "Exporting " + e.name + "...";
        std::string host_name = (vol_type_ == VolumeType::HFS)
            ? macroman_to_utf8(e.name) : e.name;
        export_folder(hfs_path + ":", out_dir + "/" + host_name, e.cnid);
        show_progress_ = false;
        status_text_ = "Exported folder: " + e.name;
        return;
    }

    export_entry(e, hfs_path, out_dir);
    show_progress_ = false;

    std::string msg = "Exported: " + e.name + " (" + format_size(e.size) + ")";
    if (e.rsize > 0)
        msg += " + resource fork (" + format_size(e.rsize) + ")";
    status_text_ = msg;
}

void App::copy_to_hfs_impl(const std::string& host_path) {
    if (!has_volume()) return;

    show_progress_ = true;
    progress_ = 0.0f;
    progress_text_ = host_path;

    // Check if this is a directory — import recursively
    if (fs::is_directory(host_path)) {
        import_host_dir(host_path);
        show_progress_ = false;
        refresh_listing();
        status_text_ = "Imported folder: " + host_path;
        return;
    }

    std::string raw_filename;
    size_t pos = host_path.rfind('/');
    if (pos != std::string::npos)
        raw_filename = host_path.substr(pos + 1);
    else
        raw_filename = host_path;

    // Convert UTF-8 filename to MacRoman and sanitize for the target filesystem
    std::string filename;
    if (vol_type_ == VolumeType::HFS)
        filename = sanitize_hfs_name(raw_filename);
    else
        filename = sanitize_hfsplus_name(raw_filename);

    std::string hfs_path = current_path_ + filename;

    // Check volume free space before writing
    {
        std::error_code ec3;
        auto fsize = fs::file_size(host_path, ec3);
        if (!ec3 && fsize > 0) {
            unsigned long need = (unsigned long)fsize;
            if (need > vol_free_bytes_) {
                show_progress_ = false;
                set_error("Not enough space on image: need " + format_size(need) +
                          ", free " + format_size(vol_free_bytes_));
                return;
            }
        }
    }

    // Check if this is a BinHex file — decode it directly instead of raw copy
    // Use raw_filename (before truncation) so long names don't lose the .hqx extension
    {
        const char* dot = strrchr(raw_filename.c_str(), '.');
        if (dot && strcasecmp(dot, ".hqx") == 0) {
            if (import_from_binhex(host_path)) {
                show_progress_ = false;
                return;
            }
            // If BinHex decode failed, fall through to raw import
        }
    }

    if (vol_type_ == VolumeType::HFS) {
        FILE* in = fopen(host_path.c_str(), "rb");
        if (!in) {
            set_error("Failed to open file: " + host_path);
            return;
        }

        // Detect type/creator: try magic bytes first, then FAF extension table
        TypeCreatorResult tcr;
        const char* type = "????";
        const char* creator = "????";

        // Read first 1024 bytes for magic detection
        uint8_t magic_buf[1024];
        size_t magic_read = fread(magic_buf, 1, sizeof(magic_buf), in);
        fseek(in, 0, SEEK_SET);

        if (magic_read > 0 && detect_type_creator_magic(magic_buf, magic_read, &tcr)) {
            type = tcr.type;
            creator = tcr.creator;
        } else if (detect_type_creator_ext(filename.c_str(), &tcr)) {
            type = tcr.type;
            creator = tcr.creator;
        }

        hfsfile* f = hfs_create(vol_, hfs_path.c_str(), type, creator);
        if (!f) {
            fclose(in);
            set_error(std::string("Failed to create HFS file: ") + (hfs_error ? hfs_error : "unknown"));
            return;
        }

        char buf[8192];
        size_t n;
        bool ok = true;
        unsigned long total = 0;

        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            unsigned long written = hfs_write(f, buf, (unsigned long)n);
            if (written != (unsigned long)n) {
                ok = false;
                break;
            }
            total += written;
        }

        hfs_close(f);
        fclose(in);

        if (ok) {
            status_text_ = "Imported: " + filename + " (" + format_size(total) + ")";
            refresh_listing();
        } else {
            set_error("Write error while importing " + filename);
        }
    } else if (vol_type_ == VolumeType::HFSPLUS) {
        // Read file into memory then write to HFS+
        FILE* in = fopen(host_path.c_str(), "rb");
        if (!in) {
            set_error("Failed to open file: " + host_path);
            return;
        }

        fseek(in, 0, SEEK_END);
        long file_size = ftell(in);
        fseek(in, 0, SEEK_SET);

        if (file_size < 0) {
            fclose(in);
            set_error("Failed to get file size: " + host_path);
            return;
        }

        std::vector<uint8_t> data((size_t)file_size);
        if (file_size > 0) {
            if (fread(data.data(), 1, (size_t)file_size, in) != (size_t)file_size) {
                fclose(in);
                set_error("Failed to read file: " + host_path);
                return;
            }
        }
        fclose(in);

        if (hfsplus_write_file(hfsplus_vol_, hfs_path.c_str(), data.data(), data.size()) != 0) {
            set_error("Failed to write to HFS+ volume: " + filename);
            return;
        }

        status_text_ = "Imported: " + filename + " (" + format_size((unsigned long)file_size) + ")";
        refresh_listing();
    }

    show_progress_ = false;
}

// --- BinHex export ---

bool App::export_as_binhex(const std::string& out_path, const HFSEntry& entry,
                           const std::string& hfs_path) {
    FILE* outf = fopen(out_path.c_str(), "wb");
    if (!outf) {
        set_error("Failed to create output file: " + out_path);
        return false;
    }

    int fd = fileno(outf);
    if (bh_start(fd) == -1) {
        fclose(outf);
        set_error(std::string("BinHex start failed: ") + bh_error);
        return false;
    }

    // BinHex header: name_len(1) + name + version(1) + type(4) + creator(4)
    //                + flags(2) + dfork_len(4) + rfork_len(4)
    uint8_t namelen = (uint8_t)entry.name.length();
    bh_insert(&namelen, 1);
    bh_insert(entry.name.c_str(), namelen);
    uint8_t version = 0;
    bh_insert(&version, 1);
    bh_insert(entry.type, 4);
    bh_insert(entry.creator, 4);
    uint8_t flags[2] = { (uint8_t)((entry.fdflags >> 8) & 0xFF),
                         (uint8_t)(entry.fdflags & 0xFF) };
    bh_insert(flags, 2);

    // Data fork length (big-endian 32-bit)
    uint8_t dlen[4];
    write_u32be(dlen, (uint32_t)entry.size);
    bh_insert(dlen, 4);

    // Resource fork length
    uint8_t rlen[4];
    write_u32be(rlen, (uint32_t)entry.rsize);
    bh_insert(rlen, 4);

    // Header CRC
    bh_insertcrc();

    // Data fork
    if (vol_type_ == VolumeType::HFS) {
        hfsfile* f = hfs_open(vol_, hfs_path.c_str());
        if (f) {
            char buf[8192];
            unsigned long n;
            while ((n = hfs_read(f, buf, sizeof(buf))) > 0)
                bh_insert(buf, (int)n);
            hfs_close(f);
        }
    } else if (vol_type_ == VolumeType::HFSPLUS) {
        uint8_t* data = nullptr;
        size_t size = 0;
        int rc = hfsplus_read_file_by_cnid(hfsplus_vol_, (uint32_t)entry.cnid, (uint32_t)entry.parent_cnid, &data, &size, 0);
        if (rc == 0 && data && size > 0) {
            bh_insert(data, (int)size);
            free(data);
        }
    }
    bh_insertcrc();

    // Resource fork
    if (vol_type_ == VolumeType::HFS) {
        hfsfile* f = hfs_open(vol_, hfs_path.c_str());
        if (f) {
            hfs_setfork(f, 1);
            char buf[8192];
            unsigned long n;
            while ((n = hfs_read(f, buf, sizeof(buf))) > 0)
                bh_insert(buf, (int)n);
            hfs_close(f);
        }
    } else if (vol_type_ == VolumeType::HFSPLUS) {
        uint8_t* data = nullptr;
        size_t size = 0;
        int rc = hfsplus_read_file_by_cnid(hfsplus_vol_, (uint32_t)entry.cnid, (uint32_t)entry.parent_cnid, &data, &size, 1);
        if (rc == 0 && data && size > 0) {
            bh_insert(data, (int)size);
            free(data);
        }
    }
    bh_insertcrc();

    bh_end();
    fclose(outf);
    return true;
}

// --- BinHex import ---

bool App::import_from_binhex(const std::string& host_path) {
    FILE* inf = fopen(host_path.c_str(), "rb");
    if (!inf) {
        set_error("Failed to open file: " + host_path);
        return false;
    }

    int fd = fileno(inf);
    if (bh_open(fd) == -1) {
        fclose(inf);
        set_error(std::string("Not a valid BinHex file: ") + bh_error);
        return false;
    }

    // Read header
    uint8_t namelen;
    if (bh_read(&namelen, 1) < 1) { bh_close(); fclose(inf); return false; }

    char name[256] = {};
    if (namelen > 0 && bh_read(name, namelen) < namelen) { bh_close(); fclose(inf); return false; }
    name[namelen] = '\0';

    uint8_t version;
    bh_read(&version, 1);

    char type[5] = {}, creator[5] = {};
    bh_read(type, 4); type[4] = '\0';
    bh_read(creator, 4); creator[4] = '\0';

    uint8_t flags_buf[2];
    bh_read(flags_buf, 2);

    uint8_t dlen_buf[4], rlen_buf[4];
    bh_read(dlen_buf, 4);
    bh_read(rlen_buf, 4);

    uint32_t dlen = read_u32be(dlen_buf);
    uint32_t rlen = read_u32be(rlen_buf);

    if (bh_readcrc() == -1) {
        set_error(std::string("BinHex header CRC failed: ") + bh_error);
        bh_close(); fclose(inf);
        return false;
    }

    // Read data fork
    std::vector<uint8_t> data_fork(dlen);
    if (dlen > 0)
        bh_read(data_fork.data(), (int)dlen);

    if (bh_readcrc() == -1) {
        set_error(std::string("BinHex data CRC failed: ") + bh_error);
        bh_close(); fclose(inf);
        return false;
    }

    // Read resource fork
    std::vector<uint8_t> rsrc_fork(rlen);
    if (rlen > 0)
        bh_read(rsrc_fork.data(), (int)rlen);

    bh_readcrc();
    bh_close();
    fclose(inf);

    // Truncate name for HFS
    std::string hfs_name = name;
    if (hfs_name.length() > HFS_MAX_FLEN)
        hfs_name = hfs_name.substr(0, HFS_MAX_FLEN);

    std::string hfs_path = current_path_ + hfs_name;

    if (vol_type_ == VolumeType::HFS) {
        hfsfile* f = hfs_create(vol_, hfs_path.c_str(), type, creator);
        if (!f) {
            set_error(std::string("Failed to create file: ") + (hfs_error ? hfs_error : "unknown"));
            return false;
        }

        if (dlen > 0)
            hfs_write(f, data_fork.data(), dlen);
        hfs_close(f);

        if (rlen > 0) {
            f = hfs_open(vol_, hfs_path.c_str());
            if (f) {
                hfs_setfork(f, 1);
                hfs_write(f, rsrc_fork.data(), rlen);
                hfs_close(f);
            }
        }
    } else if (vol_type_ == VolumeType::HFSPLUS) {
        if (hfsplus_write_file(hfsplus_vol_, hfs_path.c_str(),
                               data_fork.data(), dlen) != 0) {
            set_error("Failed to create file on HFS+ volume");
            return false;
        }

        if (rlen > 0) {
            hfsplus_write_rsrc_fork(hfsplus_vol_, hfs_path.c_str(),
                                    rsrc_fork.data(), rlen);
        }

        hfsplus_set_type_creator(hfsplus_vol_, hfs_path.c_str(), type, creator);
    }

    status_text_ = "Imported BinHex: " + hfs_name + " (" + format_size(dlen) + " data, " + format_size(rlen) + " rsrc)";
    refresh_listing();
    return true;
}

// --- Minimal PNG writer (using zlib for deflate) ---

static uint32_t crc32_png(const uint8_t* data, size_t len) {
    return (uint32_t)crc32(0, data, (uInt)len);
}

static void png_write_chunk(FILE* f, const char* type, const uint8_t* data, uint32_t len) {
    uint8_t hdr[4];
    write_u32be(hdr, len);
    fwrite(hdr, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len > 0) fwrite(data, 1, len, f);

    // CRC over type + data
    uint32_t c = crc32_png((const uint8_t*)type, 4);
    if (len > 0) c = (uint32_t)crc32(c, data, (uInt)len);
    write_u32be(hdr, c);
    fwrite(hdr, 1, 4, f);
}

bool App::export_icon_png(const std::string& out_path, const HFSEntry& /* entry */,
                          const std::string& hfs_path) {
    // Read resource fork and create icon RGBA data (64x64 pixel-doubled)
    std::vector<uint8_t> rsrc = read_rsrc_fork(hfs_path);
    if (rsrc.empty()) return false;

    // Re-use the icon creation logic to get 64x64 RGBA
    uint32_t ICN_TYPE = ('I' << 24) | ('C' << 16) | ('N' << 8) | '#';
    uint32_t ICL8_TYPE = ('i' << 24) | ('c' << 16) | ('l' << 8) | '8';
    static const int16_t ids_to_try[] = { 128, -16455, 0 };

    std::vector<uint8_t> rgba;
    for (int i = 0; ids_to_try[i] != 0 || i < 2; i++) {
        uint32_t color_len = 0, mask_len = 0;
        const uint8_t* color_data = find_resource(rsrc, ICL8_TYPE, ids_to_try[i], &color_len);
        const uint8_t* mask_data = find_resource(rsrc, ICN_TYPE, ids_to_try[i], &mask_len);

        if (color_data && color_len >= 1024)
            rgba = icon_from_icl8(color_data, color_len, mask_data, mask_len);
        else if (mask_data && mask_len >= 256)
            rgba = icon_from_icn(mask_data, mask_len);

        if (!rgba.empty()) break;
    }

    if (rgba.empty()) return false;

    const int width = 64, height = 64;

    // Build raw PNG image data: filter byte (0) + RGBA row for each row
    std::vector<uint8_t> raw;
    raw.reserve(height * (1 + width * 4));
    for (int y = 0; y < height; y++) {
        raw.push_back(0); // filter: none
        raw.insert(raw.end(), rgba.begin() + y * width * 4,
                   rgba.begin() + (y + 1) * width * 4);
    }

    // Deflate
    uLongf compressed_size = compressBound((uLong)raw.size());
    std::vector<uint8_t> compressed(compressed_size);
    if (compress2(compressed.data(), &compressed_size, raw.data(), (uLong)raw.size(), 9) != Z_OK)
        return false;
    compressed.resize(compressed_size);

    FILE* f = fopen(out_path.c_str(), "wb");
    if (!f) return false;

    // PNG signature
    const uint8_t png_sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(png_sig, 1, 8, f);

    // IHDR
    uint8_t ihdr[13];
    write_u32be(ihdr + 0, width);
    write_u32be(ihdr + 4, height);
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 6;  // color type: RGBA
    ihdr[10] = 0; // compression
    ihdr[11] = 0; // filter
    ihdr[12] = 0; // interlace
    png_write_chunk(f, "IHDR", ihdr, 13);

    // IDAT
    png_write_chunk(f, "IDAT", compressed.data(), (uint32_t)compressed.size());

    // IEND
    png_write_chunk(f, "IEND", nullptr, 0);

    fclose(f);

    status_text_ = "Saved icon: " + out_path;
    fprintf(stderr, "hfsbrowser: saved icon PNG: %s\n", out_path.c_str());
    return true;
}

bool App::delete_recursive(const std::string& hfs_path, bool is_dir) {
    if (!is_dir) {
        if (vol_type_ == VolumeType::HFS)
            return hfs_delete(vol_, hfs_path.c_str()) == 0;
        else if (vol_type_ == VolumeType::HFSPLUS)
            return hfsplus_delete(hfsplus_vol_, hfs_path.c_str()) == 0;
        return false;
    }

    // Recursively delete directory contents first
    std::string dir_path = hfs_path;
    // Ensure trailing colon for HFS directory path
    if (!dir_path.empty() && dir_path.back() != ':')
        dir_path += ':';

    if (vol_type_ == VolumeType::HFS) {
        hfsdir* dir = hfs_opendir(vol_, dir_path.c_str());
        if (!dir) return false;

        // Collect entries first (can't delete while iterating)
        std::vector<std::pair<std::string, bool>> children;
        hfsdirent ent;
        while (hfs_readdir(dir, &ent) == 0) {
            bool child_is_dir = (ent.flags & HFS_ISDIR) != 0;
            children.push_back({dir_path + ent.name, child_is_dir});
        }
        hfs_closedir(dir);

        for (auto& [child_path, child_is_dir] : children) {
            if (!delete_recursive(child_path, child_is_dir))
                return false;
        }

        return hfs_rmdir(vol_, hfs_path.c_str()) == 0;
    } else if (vol_type_ == VolumeType::HFSPLUS) {
        // Find the folder's CNID from current entries
        unsigned long folder_cnid = 0;
        for (const auto& ent : entries_) {
            std::string ent_path = current_path_ + ent.name;
            if (ent_path == hfs_path && ent.is_dir) {
                folder_cnid = ent.cnid;
                break;
            }
        }

        HFSPlusDirEntry* plus_entries = nullptr;
        int plus_count = 0;
        int rc = (folder_cnid != 0)
            ? hfsplus_list_dir_by_cnid(hfsplus_vol_, (uint32_t)folder_cnid, &plus_entries, &plus_count)
            : hfsplus_list_dir(hfsplus_vol_, dir_path.c_str(), &plus_entries, &plus_count);
        if (rc != 0) return false;

        bool ok = true;
        for (int i = 0; i < plus_count && ok; i++) {
            std::string child_path = dir_path + plus_entries[i].name;
            ok = delete_recursive(child_path, plus_entries[i].is_dir);
        }
        hfsplus_free_entries(plus_entries);

        if (!ok) return false;
        return hfsplus_delete(hfsplus_vol_, hfs_path.c_str()) == 0;
    }
    return false;
}

void App::delete_selected() {}
void App::mkdir_selected() {}

// --- MacRoman <-> UTF-8 conversion ---

// MacRoman high bytes (0x80-0xFF) to Unicode code points
static const uint16_t macroman_to_unicode[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
    0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

std::string App::macroman_to_utf8(const std::string& macroman) {
    std::string result;
    result.reserve(macroman.size() * 2);
    for (unsigned char c : macroman) {
        if (c < 0x80) {
            result += (char)c;
        } else {
            uint16_t u = macroman_to_unicode[c - 0x80];
            if (u < 0x80) {
                result += (char)u;
            } else if (u < 0x800) {
                result += (char)(0xC0 | (u >> 6));
                result += (char)(0x80 | (u & 0x3F));
            } else {
                result += (char)(0xE0 | (u >> 12));
                result += (char)(0x80 | ((u >> 6) & 0x3F));
                result += (char)(0x80 | (u & 0x3F));
            }
        }
    }
    return result;
}

std::string App::utf8_to_macroman(const std::string& utf8) {
    std::string result;
    result.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        unsigned char c = utf8[i];
        uint32_t cp = 0;
        if (c < 0x80) {
            cp = c;
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            cp = (c & 0x1F) << 6;
            if (i + 1 < utf8.size()) cp |= (utf8[i+1] & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = (c & 0x0F) << 12;
            if (i + 1 < utf8.size()) cp |= (utf8[i+1] & 0x3F) << 6;
            if (i + 2 < utf8.size()) cp |= (utf8[i+2] & 0x3F);
            i += 3;
        } else {
            i++; // skip 4-byte sequences
            result += '?';
            continue;
        }

        if (cp < 0x80) {
            result += (char)cp;
        } else {
            // Search MacRoman table
            bool found = false;
            for (int j = 0; j < 128; j++) {
                if (macroman_to_unicode[j] == cp) {
                    result += (char)(0x80 + j);
                    found = true;
                    break;
                }
            }
            if (!found) result += '_'; // Unmappable character
        }
    }
    return result;
}

std::string App::sanitize_hfs_name(const std::string& name) {
    std::string mr = utf8_to_macroman(name);
    // HFS max filename: 31 chars
    if (mr.length() > 31) mr.resize(31);
    // Replace ':' (HFS path separator) with '-'
    for (char& c : mr)
        if (c == ':') c = '-';
    return mr;
}

std::string App::sanitize_hfsplus_name(const std::string& name) {
    // HFS+ max filename: 255 UTF-16 code units — we limit to 255 bytes of UTF-8
    std::string result = name;
    if (result.length() > 255) result.resize(255);
    // Replace ':' (path separator in our Mac-style paths) with '-'
    for (char& c : result)
        if (c == ':') c = '-';
    return result;
}

std::string App::format_size(unsigned long bytes) {
    char buf[32];
    if (bytes >= 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    } else {
        snprintf(buf, sizeof(buf), "%lu B", bytes);
    }
    return buf;
}

void App::set_error(const std::string& msg) {
    error_text_ = msg;
    show_error_ = true;
    status_text_ = "Error occurred";
}
