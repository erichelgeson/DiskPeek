/*
 * HFS Browser - Application implementation
 */

#include "app.h"
#include "imgui.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cerrno>
#include <climits>
#include <filesystem>
#include <functional>

#include "platform.h"
#include "encoding.h"
#include "faf.h"
#include "icon.h"
#include "appledouble.h"

namespace fs = std::filesystem;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wregister"
extern "C" {
#include "binhex.h"
#include "crc.h"
}
#pragma GCC diagnostic pop


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

// --- Resource fork parsing and icon decoding now in lib/hfsbrowse/icon.cpp ---

// (removed: find_resource, icon_from_icn/icl4/icl8, color tables, png_write_chunk
//  — all moved to libhfsbrowse)


// --- Type/creator detection: delegate to libhfsbrowse ---

bool App::detect_type_creator_ext(const char* f, TypeCreatorResult* o) { return hfsbrowse::detect_type_creator_ext(f, o); }
bool App::detect_type_creator_magic(const uint8_t* d, size_t l, TypeCreatorResult* o) { return hfsbrowse::detect_type_creator_magic(d, l, o); }

const TypeCreatorMap* App::lookup_type_creator(const char* filename) {
    static const TypeCreatorMap s_type_creator_map[] = {
        { ".txt",  "TEXT", "ttxt" }, { ".hqx",  "TEXT", "SITx" },
        { ".sit",  "SIT!", "SITx" }, { ".jpg",  "JPEG", "ogle" },
        { ".gif",  "GIFf", "ogle" }, { ".png",  "PNG ", "ogle" },
        { ".zip",  "ZIP ", "SITx" }, { ".pdf",  "PDF ", "CARO" },
        { ".sea",  "APPL", "????" }, { nullptr, nullptr, nullptr },
    };
    const char* dot = strrchr(filename, '.');
    if (!dot) return nullptr;
    for (const TypeCreatorMap* m = s_type_creator_map; m->ext; m++)
        if (strcasecmp(dot, m->ext) == 0) return m;
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

// --- Helpers ---

// --- App lifecycle ---

App::App() {}
App::~App() { shutdown(); }

void App::init() {
    status_text_ = "Open an HFS disk image to begin (or drag & drop)";

    const char* home = getenv("HOME");
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

// Helper: set up app state after a volume is successfully opened
void App::setup_volume(std::unique_ptr<hfsbrowse::Volume> v, const char* path) {
    vol_ = std::move(v);
    vol_type_ = (vol_->type() == hfsbrowse::VolumeType::HFS) ? VolumeType::HFS : VolumeType::HFSPLUS;
    image_path_ = path;
    volume_name_ = vol_->name();
    vol_total_bytes_ = (unsigned long)vol_->total_bytes();
    vol_free_bytes_ = (unsigned long)vol_->free_bytes();
    blessed_cnid_ = vol_->blessed_folder();
    current_path_ = volume_name_ + ":";
    current_cnid_ = 2;
    cnid_stack_.clear();

    const char* type_str = (vol_type_ == VolumeType::HFS) ? "HFS" : "HFS+";
    if (vol_->is_readonly())
        status_text_ = std::string("Opened ") + type_str + " (read-only): " + path;
    else
        status_text_ = std::string("Opened ") + type_str + ": " + path;

    refresh_listing();
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
    hfsvol* raw_hfs = nullptr;
    int mounted_pnum = -1;
    for (int t = 0; t < ntry && !raw_hfs; t++) {
        int pnum = partitions_to_try[t];
        raw_hfs = hfs_mount(path, pnum, HFS_MODE_RDWR);
        if (!raw_hfs) {
            raw_hfs = hfs_mount(path, pnum, HFS_MODE_RDONLY);
            if (raw_hfs) readonly = true;
        }
        fprintf(stderr, "hfsbrowser: tried HFS pnum=%d -> %s\n", pnum,
                raw_hfs ? "ok" : (hfs_error ? hfs_error : "failed"));
        if (raw_hfs) mounted_pnum = pnum;
    }

    // Collect APM partition offsets
    APMPartition apm_parts[16];
    int napm = read_apm_partitions(path, apm_parts, 16);

    // If HFS mounted, check for HFS+ wrapper
    if (raw_hfs) {
        uint64_t part_offset = 0;
        if (mounted_pnum > 0) {
            int hfs_idx = 0;
            for (int i = 0; i < napm; i++) {
                if (strstr(apm_parts[i].type, "Apple_HFS") != nullptr) {
                    hfs_idx++;
                    if (hfs_idx == mounted_pnum) { part_offset = apm_parts[i].offset; break; }
                }
            }
        }

        uint64_t embed_offset = detect_hfsplus_wrapper(path, part_offset);
        if (embed_offset > 0) {
            fprintf(stderr, "hfsbrowser: closing HFS wrapper, trying embedded HFS+\n");
            hfs_umount(raw_hfs);
            raw_hfs = nullptr;

            HFSPlusVolume* raw_plus = hfsplus_open(path, embed_offset, false);
            if (!raw_plus) { raw_plus = hfsplus_open(path, embed_offset, true); if (raw_plus) readonly = true; }

            if (raw_plus) {
                setup_volume(hfsbrowse::make_hfsplus_volume(raw_plus, readonly), path);
                return;
            }
            fprintf(stderr, "hfsbrowser: embedded HFS+ open failed\n");
        } else {
            // Genuine classic HFS
            setup_volume(hfsbrowse::make_hfs_volume(raw_hfs, readonly), path);
            return;
        }
    }

    // --- Try HFS+ ---
    fprintf(stderr, "hfsbrowser: trying HFS+\n");

    uint64_t offsets_to_try[17];
    int noffsets = 0;
    for (int i = 0; i < napm; i++) {
        if (strstr(apm_parts[i].type, "Apple_HFS") != nullptr) {
            uint64_t embed = detect_hfsplus_wrapper(path, apm_parts[i].offset);
            if (embed > 0) offsets_to_try[noffsets++] = embed;
            offsets_to_try[noffsets++] = apm_parts[i].offset;
        }
    }
    offsets_to_try[noffsets++] = 0;

    HFSPlusVolume* raw_plus = nullptr;
    for (int t = 0; t < noffsets && !raw_plus; t++) {
        uint64_t off = offsets_to_try[t];
        raw_plus = hfsplus_open(path, off, false);
        if (!raw_plus) { raw_plus = hfsplus_open(path, off, true); if (raw_plus) readonly = true; }
        fprintf(stderr, "hfsbrowser: tried HFS+ offset=%llu -> %s\n",
                (unsigned long long)off, raw_plus ? "ok" : "failed");
    }

    if (!raw_plus) {
        set_error("Failed to open image: not a valid HFS or HFS+ volume");
        return;
    }

    setup_volume(hfsbrowse::make_hfsplus_volume(raw_plus, readonly), path);
}

void App::close_image() {
    cleanup_icons();
    vol_.reset();
    vol_type_ = VolumeType::NONE;
    image_path_.clear();
    volume_name_.clear();
    vol_total_bytes_ = 0;
    vol_free_bytes_ = 0;
    blessed_cnid_ = 0;
    cut_path_.clear();
    cut_name_.clear();
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

    if (!vol_) return;

    // List directory via Volume interface
    std::vector<HBEntry> hb_entries;
    if (vol_type_ == VolumeType::HFSPLUS && current_cnid_ != 0)
        hb_entries = vol_->list_dir(current_cnid_);
    else
        hb_entries = vol_->list_dir_by_path(current_path_);

    if (hb_entries.empty() && vol_type_ == VolumeType::HFS) {
        // HFS might fail to open the dir
        // (empty is also valid, so only error if hfs_error is set)
    }

    for (auto& hb : hb_entries) {
        if (!show_hidden_ && (hb.fdflags & 0x4000))
            continue;
        HFSEntry e;
        e.name = std::move(hb.name);
        e.is_dir = hb.is_dir;
        e.cnid = hb.cnid;
        e.parent_cnid = hb.parent_cnid;
        e.fdflags = hb.fdflags;
        e.size = (unsigned long)hb.data_size;
        e.rsize = (unsigned long)hb.rsrc_size;
        memcpy(e.type, hb.type, 5);
        memcpy(e.creator, hb.creator, 5);
        entries_.push_back(std::move(e));
    }

    // Update cached volume stats
    vol_->refresh_stats();
    vol_total_bytes_ = (unsigned long)vol_->total_bytes();
    vol_free_bytes_ = (unsigned long)vol_->free_bytes();
    blessed_cnid_ = vol_->blessed_folder();

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

        if (cnid != 0)
            return vol_->read_fork(cnid, pcnid, 1);
        else
            return vol_->read_fork_by_path(hfs_path, 1);
    }

    // Classic HFS path
    return vol_->read_fork_by_path(hfs_path, 1);
}

GLuint App::create_icon_from_rsrc(const std::vector<uint8_t>& rsrc) {
    std::vector<uint8_t> rgba = hfsbrowse::icon::extract_rgba(rsrc);
    if (rgba.empty()) return 0;

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    return tex;
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
        if (vol_->chdir(new_path) != 0) {
            set_error("Failed to enter directory");
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
        if (vol_->chdir(parent) != 0) {
            set_error("Failed to navigate up");
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
    render_rename_popup();
    render_check_popup();
    render_about_popup();
    process_dialog_result();

    ImGui::End();
}

void App::render_toolbar() {
    if (ImGui::Button("Open Image")) {
        show_open_dialog();
    }
    ImGui::SameLine();
    if (ImGui::Button("About")) {
        show_about_ = true;
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
        if (ImGui::Button("Check")) {
            run_volume_check();
            show_check_ = true;
        }

        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();
        ImGui::Text("\"%s\"  (%s / %s free)",
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

    // Split path into clickable segments: "VolName:Foo:Bar:" → [VolName] [Foo] [Bar]
    ImGui::Text("Path:");
    ImGui::SameLine();

    std::string path = current_path_;
    // Remove trailing colon for splitting
    if (!path.empty() && path.back() == ':') path.pop_back();

    std::vector<std::string> segments;
    size_t start = 0;
    for (size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == ':') {
            segments.push_back(path.substr(start, i - start));
            start = i + 1;
        }
    }

    for (size_t s = 0; s < segments.size(); s++) {
        if (s > 0) {
            ImGui::SameLine(0, 0);
            ImGui::Text(":");
            ImGui::SameLine(0, 0);
        }

        // Build the path up to this segment
        std::string target;
        for (size_t j = 0; j <= s; j++) {
            target += segments[j];
            target += ":";
        }

        // Last segment is current dir — not clickable
        if (s == segments.size() - 1) {
            ImGui::Text("%s", segments[s].c_str());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.7f, 1.0f));
            char btn_id[256];
            snprintf(btn_id, sizeof(btn_id), "%s##path%d", segments[s].c_str(), (int)s);
            if (ImGui::SmallButton(btn_id)) {
                // Navigate to this path
                if (vol_type_ == VolumeType::HFS) {
                    if (vol_->chdir(target) != 0) {
                        set_error("Navigation failed");
                        ImGui::PopStyleColor();
                        return;
                    }
                } else if (vol_type_ == VolumeType::HFSPLUS) {
                    // Rebuild CNID stack by navigating from root
                    cnid_stack_.clear();
                    current_cnid_ = 2; // root
                    // Walk segments to find each folder CNID
                    for (size_t j = 1; j <= s; j++) {
                        auto dir_entries = vol_->list_dir((uint32_t)current_cnid_);
                        for (const auto& de : dir_entries) {
                            if (de.is_dir && segments[j] == de.name) {
                                cnid_stack_.push_back(current_cnid_);
                                current_cnid_ = de.cnid;
                                break;
                            }
                        }
                    }
                }
                current_path_ = target;
                refresh_listing();
            }
            ImGui::PopStyleColor();
        }
    }
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


    if (ImGui::BeginTable("files", 4,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoBordersInBodyUntilResize)) {

        float icon_sz = ImGui::GetTextLineHeight();

        ImGui::TableSetupColumn("##icon", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, icon_sz + 16);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type/Creator", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("DF/RF Size", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)entries_.size(); i++) {
            const HFSEntry& e = entries_[i];

            // Check if file is invisible (Finder flag kIsInvisible = 0x4000)
            bool is_hidden = (e.fdflags & 0x4000) != 0;

            // Label color (bits 1-3 of fdflags)
            int label = (e.fdflags >> 1) & 0x07;
            // System 7 Finder label colors (value 7=Essential at top of menu, 1=Project at bottom)
            // fdFlags bits 1-3 store label value in reverse menu order
            // System 7 Finder label colors — value 7 = top of Label menu (Essential)
            // Verified against real Mac OS 9 Label menu screenshot
            static const ImU32 label_colors[] = {
                0,                             // 0: None
                IM_COL32(140, 110, 40, 50),    // 1: Project 2 (brown)
                IM_COL32(60, 160, 60, 50),     // 2: Project 1 (green)
                IM_COL32(40, 60, 190, 50),     // 3: Personal (dark blue)
                IM_COL32(50, 190, 220, 50),    // 4: Cool (cyan)
                IM_COL32(210, 50, 190, 50),    // 5: In Progress (magenta/pink)
                IM_COL32(220, 30, 30, 50),     // 6: Hot (red)
                IM_COL32(255, 160, 50, 50),    // 7: Essential (orange)
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
            char sel_id[300];
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
            char ctx_id[32];
            snprintf(ctx_id, sizeof(ctx_id), "ctx##%d", i);
            if (selected_entry_ == i && ImGui::BeginPopupContextItem(ctx_id)) {
                std::string ctx_hfs_path = current_path_ + e.name;

                if (!e.is_dir) {
                    // Default export: BinHex if has rsrc fork, regular otherwise
                    if (e.rsize > 0) {
                        if (ImGui::MenuItem("Export as BinHex (.hqx)")) {
                            show_export_binhex_dialog();
                        }
                        if (ImGui::MenuItem("Export (data fork + AppleDouble)")) {
                            show_export_dialog();
                        }
                    } else {
                        if (ImGui::MenuItem("Export")) {
                            show_export_dialog();
                        }
                        if (ImGui::MenuItem("Export as BinHex (.hqx)")) {
                            show_export_binhex_dialog();
                        }
                    }

                    if (e.icon_tex) {
                        if (ImGui::MenuItem("Save Icon as PNG")) {
                            show_export_icon_dialog();
                        }
                        if (ImGui::MenuItem("Copy Icon")) {
                            // Read icon RGBA and convert to PNG in memory
                            std::vector<uint8_t> rsrc = read_rsrc_fork(ctx_hfs_path);
                            std::vector<uint8_t> rgba = hfsbrowse::icon::extract_rgba(rsrc);
                            if (!rgba.empty()) {
                                // Write PNG to temp file, read it back as bytes
                                std::string tmp = "/tmp/hfsbrowser_clip.png";
                                if (hfsbrowse::icon::write_png(tmp, rgba)) {
                                    FILE* pf = fopen(tmp.c_str(), "rb");
                                    if (pf) {
                                        fseek(pf, 0, SEEK_END);
                                        long psz = ftell(pf);
                                        fseek(pf, 0, SEEK_SET);
                                        // Store in a static buffer for SDL callback
                                        static std::vector<uint8_t> s_clip_png;
                                        s_clip_png.resize(psz);
                                        fread(s_clip_png.data(), 1, psz, pf);
                                        fclose(pf);
                                        remove(tmp.c_str());

                                        static const char* mime_types[] = { "image/png" };
                                        SDL_SetClipboardData(
                                            [](void*, const char* mime, size_t* len) -> const void* {
                                                if (strcmp(mime, "image/png") == 0) {
                                                    *len = s_clip_png.size();
                                                    return s_clip_png.data();
                                                }
                                                return nullptr;
                                            },
                                            nullptr, nullptr, mime_types, 1);
                                        status_text_ = "Icon copied to clipboard";
                                    }
                                }
                            }
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
                                std::vector<uint8_t> fdata;
                                if (vol_type_ == VolumeType::HFS)
                                    fdata = vol_->read_fork_by_path(ctx_hfs_path, 0);
                                else
                                    fdata = vol_->read_fork((uint32_t)e.cnid, (uint32_t)e.parent_cnid, 0);
                                if (!fdata.empty()) {
                                    size_t check = fdata.size() > 1024 ? 1024 : fdata.size();
                                    found = detect_type_creator_magic(fdata.data(), check, &tcr);
                                }
                            }

                            // Fall back to extension
                            if (!found)
                                found = detect_type_creator_ext(e.name.c_str(), &tcr);

                            if (found) {
                                vol_->set_type_creator(ctx_hfs_path, tcr.type, tcr.creator);
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
                        show_export_dialog();
                    }
                    if (ImGui::MenuItem("Export Folder as BinHex")) {
                        show_export_folder_binhex_dialog();
                    }
                    ImGui::Separator();
                    bool already_blessed = (blessed_cnid_ != 0 && e.cnid == blessed_cnid_);
                    if (ImGui::MenuItem("Bless as System Folder", nullptr, already_blessed)) {
                        unsigned long new_blessed = already_blessed ? 0 : e.cnid;
                        if (vol_->set_blessed((uint32_t)new_blessed) == 0)
                            blessed_cnid_ = new_blessed;
                        else
                            set_error("Failed to bless folder");
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
                if (ImGui::MenuItem("Rename")) {
                    snprintf(rename_buf_, sizeof(rename_buf_), "%s", e.name.c_str());
                    show_rename_ = true;
                }
                if (ImGui::MenuItem("Cut")) {
                    cut_path_ = ctx_hfs_path;
                    cut_name_ = e.name;
                    cut_is_dir_ = e.is_dir;
                    status_text_ = "Cut: " + e.name;
                }
                if (!cut_path_.empty()) {
                    if (ImGui::MenuItem("Paste Here")) {
                        std::string dest = current_path_ + cut_name_;
                        int rc = vol_->rename(cut_path_, dest);
                        if (rc != 0)
                            set_error("Move failed");
                        else {
                            status_text_ = "Moved: " + cut_name_;
                            cut_path_.clear();
                            cut_name_.clear();
                            refresh_listing();
                        }
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete")) {
                    confirm_text_ = "Delete \"" + e.name + "\"?";
                    confirm_target_ = ctx_hfs_path;
                    confirm_is_dir_ = e.is_dir;
                    show_confirm_ = true;
                }
                ImGui::Separator();
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

            // DF/RF Size
            ImGui::TableNextColumn();
            if (!e.is_dir) {
                ImGui::Text("%s/%s", format_size(e.size).c_str(),
                            format_size(e.rsize).c_str());
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
            show_import_dialog();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Import a file or folder from your computer into the image");

    ImGui::SameLine();

    if (!has_sel) ImGui::BeginDisabled();
    if (ImGui::Button("Extract")) {
        if (has_sel) {
            const HFSEntry& e = entries_[selected_entry_];
            if (e.is_dir) {
                show_export_dialog();
            } else {
                show_export_dialog();
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

    if (!has_vol) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Checkbox("Show Hidden", &show_hidden_)) {
        if (has_vol) refresh_listing();
    }

    if (!cut_path_.empty() && has_vol) {
        ImGui::SameLine();
        if (ImGui::Button("Paste")) {
            std::string dest = current_path_ + cut_name_;
            int rc = vol_->rename(cut_path_, dest);
            if (rc != 0)
                set_error("Move failed");
            if (rc == 0) {
                status_text_ = "Moved: " + cut_name_;
                cut_path_.clear();
                cut_name_.clear();
                refresh_listing();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", cut_name_.c_str());
    }
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
                set_error("Delete failed (try Force Delete for corrupt files): " + confirm_target_);
            }
            show_confirm_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (!confirm_is_dir_ && vol_) {
            if (ImGui::Button("Force Delete", ImVec2(120, 0))) {
                if (vol_->force_delete(confirm_target_) == 0) {
                    status_text_ = "Force deleted: " + confirm_target_ + " (disk blocks not freed)";
                    refresh_listing();
                } else {
                    set_error("Force delete also failed: " + confirm_target_);
                }
                show_confirm_ = false;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Remove catalog entry without freeing disk blocks.\nUse for files with corrupt extents.");
            ImGui::SameLine();
        }
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
            size_t max_len = (vol_type_ == VolumeType::HFSPLUS) ? 255 : HFS_MAX_FLEN;
            if (strlen(mkdir_name_) > 0 && strlen(mkdir_name_) <= max_len) {
                std::string full_path = current_path_ + mkdir_name_;
                int rc = vol_->mkdir(full_path);
                if (rc != 0)
                    set_error("mkdir failed");
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

            if (vol_->set_type_creator(hfs_path, new_type, new_creator) != 0)
                set_error("Failed to set type/creator");
            else {
                memcpy(e.type, new_type, 5);
                memcpy(e.creator, new_creator, 5);
                status_text_ = "Set type/creator on " + e.name;
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

void App::render_rename_popup() {
    if (show_rename_)
        ImGui::OpenPopup("Rename");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (selected_entry_ < 0 || selected_entry_ >= (int)entries_.size()) {
            show_rename_ = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        // Copy name before any operation that might invalidate entries_
        std::string entry_name = entries_[selected_entry_].name;
        ImGui::Text("Rename: %s", entry_name.c_str());
        ImGui::Text("New name:");
        bool enter = ImGui::InputText("##rename", rename_buf_, sizeof(rename_buf_),
            ImGuiInputTextFlags_EnterReturnsTrue);

        if (enter || ImGui::Button("Rename", ImVec2(100, 0))) {
            std::string new_name = rename_buf_;
            if (!new_name.empty() && new_name != entry_name) {
                std::string old_path = current_path_ + entry_name;
                std::string new_path = current_path_ + new_name;
                int rc = -1;

                rc = vol_->rename(old_path, new_path);
                if (rc != 0)
                    set_error("Rename failed");

                if (rc == 0) {
                    status_text_ = "Renamed: " + entry_name + " → " + new_name;
                    refresh_listing();
                }
            }
            show_rename_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            show_rename_ = false;
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
        const char* label_names[] = { "None", "Project 2", "Project 1", "Personal", "Cool", "In Progress", "Hot", "Essential" };
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

            {
                bool ok = true;
                if (!e.is_dir) {
                    if (vol_->set_type_creator(hfs_path, new_type, new_creator) != 0)
                        ok = false;
                }
                if (ok && vol_->set_finder_flags(hfs_path, info_fdflags_) != 0)
                    ok = false;

                if (!ok) {
                    set_error("Failed to update info");
                } else {
                    e.fdflags = info_fdflags_;
                    if (!e.is_dir) {
                        memcpy(e.type, new_type, 5);
                        memcpy(e.creator, new_creator, 5);
                    }
                    status_text_ = "Updated info for " + e.name;
                }
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

void App::run_volume_check() {
    check_log_.clear();

    check_log_ = vol_->check();
}

void App::render_about_popup() {
    if (show_about_)
        ImGui::OpenPopup("About HFS Browser");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("About HFS Browser", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("HFS Browser");
        ImGui::Text("A tool for browsing and editing classic Macintosh");
        ImGui::Text("HFS and HFS+ disk images.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Built with:");
        ImGui::Spacing();

        ImGui::Text("hfsutils");
        ImGui::TextDisabled("  Robert Leslie, 1996-1998");
        ImGui::TextDisabled("  HFS filesystem library (GPLv2)");
        ImGui::Spacing();

        ImGui::Text("libdmg-hfsplus");
        ImGui::TextDisabled("  planetbeing (David Wang)");
        ImGui::TextDisabled("  HFS+ filesystem library (GPLv3)");
        ImGui::Spacing();

        ImGui::Text("Dear ImGui");
        ImGui::TextDisabled("  Omar Cornut");
        ImGui::TextDisabled("  Immediate mode GUI (MIT)");
        ImGui::Spacing();

        ImGui::Text("SDL2");
        ImGui::TextDisabled("  Sam Lantinga / libsdl.org");
        ImGui::TextDisabled("  Cross-platform multimedia library (zlib)");
        ImGui::Spacing();

        ImGui::Text("Fix-A-Fork");
        ImGui::TextDisabled("  Eric Helgeson / BlueSCSI project");
        ImGui::TextDisabled("  Type/creator detection (portions re-licensed GPLv3)");
        ImGui::Spacing();

        ImGui::Text("zlib");
        ImGui::TextDisabled("  Jean-loup Gailly, Mark Adler");
        ImGui::TextDisabled("  Compression library (zlib)");
        ImGui::Spacing();

        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("https://github.com/erichelgeson/hfsutils");
        ImGui::Spacing();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            show_about_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::render_check_popup() {
    if (show_check_)
        ImGui::OpenPopup("Volume Check");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Volume Check", nullptr, ImGuiWindowFlags_None)) {
        ImGui::BeginChild("##checklog", ImVec2(0, -30), true);
        ImGui::TextUnformatted(check_log_.c_str());
        ImGui::EndChild();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            show_check_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// --- Native file dialogs (SDL3) ---

// SDL3 dialog callback — stores the result for processing next frame
static void dialog_callback(void* userdata, const char* const* filelist, int /*filter*/) {
    App* app = (App*)userdata;
    if (filelist && filelist[0]) {
        app->dialog_result_ = filelist[0];
    }
}

void App::show_open_dialog() {
    pending_op_ = DialogOp::OPEN_IMAGE;
    SDL_DialogFileFilter filters[] = { { "HFS Disk Images", "hda;img;dsk;iso;dmg;image" } };
    SDL_ShowOpenFileDialog(dialog_callback, this, SDL_GetKeyboardFocus(), filters, 1, nullptr, false);
}

void App::show_export_dialog() {
    pending_op_ = DialogOp::EXPORT_FILE;
    SDL_ShowOpenFolderDialog(dialog_callback, this, SDL_GetKeyboardFocus(), nullptr, false);
}

void App::show_import_dialog() {
    pending_op_ = DialogOp::IMPORT_FILE;
    SDL_ShowOpenFileDialog(dialog_callback, this, SDL_GetKeyboardFocus(), nullptr, 0, nullptr, true);
}

void App::show_export_binhex_dialog() {
    pending_op_ = DialogOp::EXPORT_BINHEX;
    SDL_ShowOpenFolderDialog(dialog_callback, this, SDL_GetKeyboardFocus(), nullptr, false);
}

void App::show_export_folder_binhex_dialog() {
    pending_op_ = DialogOp::EXPORT_FOLDER_BINHEX;
    SDL_ShowOpenFolderDialog(dialog_callback, this, SDL_GetKeyboardFocus(), nullptr, false);
}

void App::show_export_icon_dialog() {
    pending_op_ = DialogOp::EXPORT_ICON;
    SDL_ShowOpenFolderDialog(dialog_callback, this, SDL_GetKeyboardFocus(), nullptr, false);
}

void App::process_dialog_result() {
    if (dialog_result_.empty()) return;

    std::string path = dialog_result_;
    dialog_result_.clear();
    DialogOp op = pending_op_;
    pending_op_ = DialogOp::NONE;

    switch (op) {
    case DialogOp::OPEN_IMAGE:
        open_image(path.c_str());
        break;
    case DialogOp::EXPORT_FILE:
        copy_from_hfs_impl(path);
        break;
    case DialogOp::IMPORT_FILE:
        copy_to_hfs_impl(path);
        break;
    case DialogOp::EXPORT_BINHEX:
        if (selected_entry_ >= 0 && selected_entry_ < (int)entries_.size()) {
            const HFSEntry& e = entries_[selected_entry_];
            std::string hfs_path = current_path_ + e.name;
            std::string out = path + "/" + e.name + ".hqx";
            export_as_binhex(out, e, hfs_path);
        }
        break;
    case DialogOp::EXPORT_FOLDER_BINHEX:
        if (selected_entry_ >= 0 && selected_entry_ < (int)entries_.size()) {
            const HFSEntry& e = entries_[selected_entry_];
            std::string hfs_path = current_path_ + e.name + ":";
            std::string host_name = (vol_type_ == VolumeType::HFS)
                ? macroman_to_utf8(e.name) : e.name;
            show_progress_ = true;
            export_folder_binhex(hfs_path, path + "/" + host_name, e.cnid);
            show_progress_ = false;
            status_text_ = "Exported folder as BinHex: " + e.name;
        }
        break;
    case DialogOp::EXPORT_ICON:
        if (selected_entry_ >= 0 && selected_entry_ < (int)entries_.size()) {
            const HFSEntry& e = entries_[selected_entry_];
            std::string hfs_path = current_path_ + e.name;
            export_icon_png(path + "/" + e.name + ".png", e, hfs_path);
        }
        break;
    default:
        break;
    }
}

// Legacy stubs removed — old render_file_picker, picker_refresh, etc.
// Now using SDL3 native file dialogs above.

void App::copy_from_hfs() {
    if (!has_volume() || selected_entry_ < 0) return;
    show_export_dialog();
}

void App::copy_to_hfs() {
    if (!has_volume()) return;
    show_import_dialog();
}

// (deleted: render_file_picker, picker_refresh, picker_navigate,
//  open_file_picker_for_open, open_file_picker_for_export, open_file_picker_for_import)


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
    return hfsbrowse::write_appledouble(host_path, type, creator, fdflags, rsrc_data);
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

    {
        std::vector<uint8_t> fdata;
        if (vol_type_ == VolumeType::HFS)
            fdata = vol_->read_fork_by_path(hfs_path, 0);
        else
            fdata = vol_->read_fork((uint32_t)e.cnid, (uint32_t)e.parent_cnid, 0);

        FILE* out = fopen(out_path.c_str(), "wb");
        if (!out) return;

        if (!fdata.empty()) {
            ok = (fwrite(fdata.data(), 1, fdata.size(), out) == fdata.size());
            total = (unsigned long)fdata.size();
        } else {
            ok = true;
        }
        fclose(out);
        if (e.size > 0) progress_ = 1.0f;
    }

    if (!ok) return;

    // Export resource fork + Finder info as AppleDouble
    std::vector<uint8_t> rsrc;
    if (e.rsize > 0) {
        if (vol_type_ == VolumeType::HFSPLUS)
            rsrc = vol_->read_fork((uint32_t)e.cnid, (uint32_t)e.parent_cnid, 1);
        else
            rsrc = vol_->read_fork_by_path(hfs_path, 1);
    }

    if ((e.type[0] && strcmp(e.type, "????") != 0) || !rsrc.empty()) {
        hfsbrowse::write_forkinfo(out_path, e.type, e.creator, e.fdflags, rsrc);
    }

    fprintf(stderr, "hfsbrowser: exported %s (%lu bytes)\n", e.name.c_str(), total);
}

// Recursively export a folder from the image to a host directory
void App::export_folder(const std::string& hfs_dir_path, const std::string& host_dir,
                        unsigned long folder_cnid) {
    platform_mkdir(host_dir.c_str());

    std::vector<HBEntry> dir_entries;
    if (vol_type_ == VolumeType::HFSPLUS && folder_cnid != 0)
        dir_entries = vol_->list_dir((uint32_t)folder_cnid);
    else
        dir_entries = vol_->list_dir_by_path(hfs_dir_path);

    for (auto& hb : dir_entries) {
        HFSEntry e;
        e.name = std::move(hb.name);
        e.is_dir = hb.is_dir;
        e.cnid = hb.cnid;
        e.parent_cnid = hb.parent_cnid;
        e.fdflags = hb.fdflags;
        e.size = (unsigned long)hb.data_size;
        e.rsize = (unsigned long)hb.rsrc_size;
        memcpy(e.type, hb.type, 5);
        memcpy(e.creator, hb.creator, 5);

        std::string child_hfs = hfs_dir_path + e.name;
        std::string host_name = (vol_type_ == VolumeType::HFS)
            ? macroman_to_utf8(e.name) : e.name;
        if (e.is_dir)
            export_folder(child_hfs + ":", host_dir + "/" + host_name, e.cnid);
        else
            export_entry(e, child_hfs, host_dir);
    }
}

// Recursively export a folder, encoding every file as BinHex
void App::export_folder_binhex(const std::string& hfs_dir_path, const std::string& host_dir,
                               unsigned long folder_cnid) {
    platform_mkdir(host_dir.c_str());

    std::vector<HBEntry> dir_entries;
    if (vol_type_ == VolumeType::HFSPLUS && folder_cnid != 0)
        dir_entries = vol_->list_dir((uint32_t)folder_cnid);
    else
        dir_entries = vol_->list_dir_by_path(hfs_dir_path);

    for (auto& hb : dir_entries) {
        HFSEntry e;
        e.name = std::move(hb.name);
        e.is_dir = hb.is_dir;
        e.cnid = hb.cnid;
        e.parent_cnid = hb.parent_cnid;
        e.fdflags = hb.fdflags;
        e.size = (unsigned long)hb.data_size;
        e.rsize = (unsigned long)hb.rsrc_size;
        memcpy(e.type, hb.type, 5);
        memcpy(e.creator, hb.creator, 5);

        std::string child_hfs = hfs_dir_path + e.name;
        std::string host_name = (vol_type_ == VolumeType::HFS)
            ? macroman_to_utf8(e.name) : e.name;
        if (e.is_dir) {
            export_folder_binhex(child_hfs + ":", host_dir + "/" + host_name, e.cnid);
        } else {
            std::string out = host_dir + "/" + host_name + ".hqx";
            progress_text_ = e.name;
            export_as_binhex(out, e, child_hfs);
        }
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
    int mkdir_rc = vol_->mkdir(hfs_folder);
    if (mkdir_rc != 0) {
        fprintf(stderr, "hfsbrowser: failed to create folder %s\n", hfs_folder.c_str());
        return;
    }

    // Save and change current path
    std::string saved_path = current_path_;
    current_path_ = hfs_folder + ":";
    if (vol_type_ == VolumeType::HFS)
        vol_->chdir(current_path_);

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
        vol_->chdir(current_path_);
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

    {
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

        if (vol_type_ == VolumeType::HFS) {
            // Detect type/creator: try xattr FinderInfo, then magic bytes, then FAF extension
            TypeCreatorResult tcr;
            const char* type = "????";
            const char* creator = "????";

            // Try reading FinderInfo xattr from host file
            uint8_t fi_buf[32];
            bool got_xattr = false;
            if (platform_getxattr(host_path.c_str(), "com.apple.FinderInfo", fi_buf, 32) >= 32) {
                memcpy(tcr.type, fi_buf, 4); tcr.type[4] = '\0';
                memcpy(tcr.creator, fi_buf + 4, 4); tcr.creator[4] = '\0';
                if (tcr.type[0] && strcmp(tcr.type, "????") != 0) {
                    type = tcr.type;
                    creator = tcr.creator;
                    got_xattr = true;
                }
            }

            if (!got_xattr) {
                if (file_size > 0) {
                    size_t check = (size_t)file_size > 1024 ? 1024 : (size_t)file_size;
                    if (!detect_type_creator_magic(data.data(), check, &tcr)) {
                        detect_type_creator_ext(filename.c_str(), &tcr);
                    }
                    type = tcr.type;
                    creator = tcr.creator;
                } else if (detect_type_creator_ext(filename.c_str(), &tcr)) {
                    type = tcr.type;
                    creator = tcr.creator;
                }
            }

            if (vol_->create_file(hfs_path, type, creator, data.data(), data.size()) != 0) {
                set_error("Failed to create HFS file");
                show_progress_ = false;
                return;
            }

            // Try to import resource fork from xattr
            int rsrc_size = platform_getxattr(host_path.c_str(), "com.apple.ResourceFork", nullptr, 0);
            if (rsrc_size > 0) {
                std::vector<uint8_t> rsrc(rsrc_size);
                platform_getxattr(host_path.c_str(), "com.apple.ResourceFork", rsrc.data(), rsrc.size());
                vol_->write_rsrc_fork(hfs_path, rsrc.data(), rsrc.size());
            }
        } else {
            if (vol_->write_file(hfs_path, data.data(), data.size()) != 0) {
                set_error("Failed to write to volume: " + filename);
                show_progress_ = false;
                return;
            }
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
    {
        std::vector<uint8_t> fdata;
        if (vol_type_ == VolumeType::HFS)
            fdata = vol_->read_fork_by_path(hfs_path, 0);
        else
            fdata = vol_->read_fork((uint32_t)entry.cnid, (uint32_t)entry.parent_cnid, 0);
        if (!fdata.empty() && fdata.size() <= (size_t)INT_MAX)
            bh_insert(fdata.data(), (int)fdata.size());
    }
    bh_insertcrc();

    // Resource fork
    {
        std::vector<uint8_t> rdata;
        if (vol_type_ == VolumeType::HFS)
            rdata = vol_->read_fork_by_path(hfs_path, 1);
        else
            rdata = vol_->read_fork((uint32_t)entry.cnid, (uint32_t)entry.parent_cnid, 1);
        if (!rdata.empty() && rdata.size() <= (size_t)INT_MAX)
            bh_insert(rdata.data(), (int)rdata.size());
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

    // Sanitize embedded filename to prevent path traversal
    std::string hfs_name;
    if (vol_type_ == VolumeType::HFS)
        hfs_name = sanitize_hfs_name(name);
    else
        hfs_name = sanitize_hfsplus_name(name);

    std::string hfs_path = current_path_ + hfs_name;

    if (vol_type_ == VolumeType::HFS) {
        if (vol_->create_file(hfs_path, type, creator, data_fork.data(), dlen) != 0) {
            set_error("Failed to create file");
            return false;
        }

        if (rlen > 0)
            vol_->write_rsrc_fork(hfs_path, rsrc_fork.data(), rlen);
    } else if (vol_type_ == VolumeType::HFSPLUS) {
        if (vol_->write_file(hfs_path, data_fork.data(), dlen) != 0) {
            set_error("Failed to create file on volume");
            return false;
        }

        if (rlen > 0)
            vol_->write_rsrc_fork(hfs_path, rsrc_fork.data(), rlen);

        vol_->set_type_creator(hfs_path, type, creator);
    }

    status_text_ = "Imported BinHex: " + hfs_name + " (" + format_size(dlen) + " data, " + format_size(rlen) + " rsrc)";
    refresh_listing();
    return true;
}

bool App::export_icon_png(const std::string& out_path, const HFSEntry& /* entry */,
                          const std::string& hfs_path) {
    std::vector<uint8_t> rsrc = read_rsrc_fork(hfs_path);
    if (rsrc.empty()) return false;

    std::vector<uint8_t> rgba = hfsbrowse::icon::extract_rgba(rsrc);
    if (rgba.empty()) return false;

    if (!hfsbrowse::icon::write_png(out_path, rgba)) return false;

    status_text_ = "Saved icon: " + out_path;
    return true;
}

bool App::delete_recursive(const std::string& hfs_path, bool is_dir) {
    if (!is_dir)
        return vol_->delete_file(hfs_path) == 0;

    // Recursively delete directory contents first
    std::string dir_path = hfs_path;
    // Ensure trailing colon for HFS directory path
    if (!dir_path.empty() && dir_path.back() != ':')
        dir_path += ':';

    // Find the folder's CNID from current entries (for HFS+ efficiency)
    unsigned long folder_cnid = 0;
    for (const auto& ent : entries_) {
        std::string ent_path = current_path_ + ent.name;
        if (ent_path == hfs_path && ent.is_dir) {
            folder_cnid = ent.cnid;
            break;
        }
    }

    std::vector<HBEntry> dir_entries;
    if (vol_type_ == VolumeType::HFSPLUS && folder_cnid != 0)
        dir_entries = vol_->list_dir((uint32_t)folder_cnid);
    else
        dir_entries = vol_->list_dir_by_path(dir_path);

    for (auto& de : dir_entries) {
        std::string child_path = dir_path + de.name;
        if (!delete_recursive(child_path, de.is_dir))
            return false;
    }

    return vol_->rmdir(hfs_path) == 0;
}

void App::delete_selected() {}
void App::mkdir_selected() {}

// --- Encoding: delegate to libhfsbrowse ---
// Thin wrappers for backward compat with App:: method signatures

std::string App::macroman_to_utf8(const std::string& s) { return hfsbrowse::macroman_to_utf8(s); }
std::string App::utf8_to_macroman(const std::string& s) { return hfsbrowse::utf8_to_macroman(s); }
std::string App::sanitize_hfs_name(const std::string& s) { return hfsbrowse::sanitize_hfs_name(s); }
std::string App::sanitize_hfsplus_name(const std::string& s) { return hfsbrowse::sanitize_hfsplus_name(s); }

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
