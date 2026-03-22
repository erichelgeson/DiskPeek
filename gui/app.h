/*
 * HFS Browser - Application state and UI logic
 */

#ifndef APP_H
#define APP_H

#include <string>
#include <vector>
#include <cstdint>

#include "imgui.h"

extern "C" {
#include "hfs.h"
}

#include "hfsplus_ops.h"

typedef unsigned int GLuint;

struct HFSEntry {
    std::string name;
    bool is_dir;
    unsigned long cnid;
    unsigned long parent_cnid = 0;  // HFS+ parent folder CNID
    unsigned long size;       // data fork size (files only)
    unsigned long rsize;      // resource fork size (files only)
    char type[5];             // file type (files only)
    char creator[5];          // file creator (files only)
    short fdflags;
    GLuint icon_tex = 0;      // OpenGL texture for custom icon (0 = none)
};

// Type/creator lookup for common extensions
struct TypeCreatorMap {
    const char* ext;
    const char* type;
    const char* creator;
};

enum class VolumeType { NONE, HFS, HFSPLUS };

class App {
public:
    App();
    ~App();

    void init();
    void render();
    void shutdown();
    bool should_quit() const { return quit_requested_; }

    void open_image(const char* path);
    void import_file(const char* path);
    bool has_volume() const { return vol_type_ != VolumeType::NONE; }

private:
    // UI rendering
    void render_toolbar();
    void render_path_bar();
    void render_file_list();
    void render_action_bar();
    void render_error_popup();
    void render_confirm_popup();
    void render_mkdir_popup();
    void render_file_picker();
    void render_type_creator_popup();
    void render_info_popup();
    void render_rename_popup();
    void render_check_popup();
    void render_about_popup();
    void run_volume_check();
    bool show_about_ = false;
    void render_progress_bar();

    // HFS operations
    void close_image();
    void refresh_listing();
    void navigate_to(const char* path);
    void navigate_up();
    void copy_from_hfs();
    void copy_to_hfs();
    void copy_from_hfs_impl(const std::string& host_path);
    void copy_to_hfs_impl(const std::string& host_path);
    void export_entry(const HFSEntry& entry, const std::string& hfs_path,
                      const std::string& host_dir);
    void export_folder(const std::string& hfs_dir_path, const std::string& host_dir,
                       unsigned long folder_cnid = 0);
    void export_folder_binhex(const std::string& hfs_dir_path, const std::string& host_dir,
                              unsigned long folder_cnid = 0);
    void import_host_dir(const std::string& host_dir);
    void delete_selected();
    void mkdir_selected();
    bool delete_recursive(const std::string& hfs_path, bool is_dir);

    // Icon support
    void load_entry_icons();
    void cleanup_icons();
    std::vector<uint8_t> read_rsrc_fork(const std::string& hfs_path);
    static GLuint create_icon_from_rsrc(const std::vector<uint8_t>& rsrc);
    static void draw_folder_icon(ImVec2 pos, float size);
    static void draw_file_icon(ImVec2 pos, float size);

    // BinHex support
    bool export_as_binhex(const std::string& out_path, const HFSEntry& entry,
                          const std::string& hfs_path);
    bool import_from_binhex(const std::string& host_path);

    // Icon export
    bool export_icon_png(const std::string& out_path, const HFSEntry& entry,
                         const std::string& hfs_path);

    // Type/creator detection (magic bytes + FAF extension table)
    struct TypeCreatorResult { char type[5]; char creator[5]; };
    static bool detect_type_creator_magic(const uint8_t* data, size_t len,
                                          TypeCreatorResult* out);
    static bool detect_type_creator_ext(const char* filename, TypeCreatorResult* out);

    // AppleDouble export
    static bool write_appledouble(const std::string& host_path,
                                  const char* type, const char* creator,
                                  short fdflags,
                                  const std::vector<uint8_t>& rsrc_data);

    // File picker helpers
    void open_file_picker_for_open();
    void open_file_picker_for_export();
    void open_file_picker_for_import();

    // Filename encoding conversion
    static std::string macroman_to_utf8(const std::string& macroman);
    static std::string utf8_to_macroman(const std::string& utf8);
    static std::string sanitize_hfs_name(const std::string& name);
    static std::string sanitize_hfsplus_name(const std::string& name);

    // Progress tracking
    float progress_ = 0.0f;
    std::string progress_text_;
    bool show_progress_ = false;

    // Utility
    std::string format_size(unsigned long bytes);
    void set_error(const std::string& msg);
    static const TypeCreatorMap* lookup_type_creator(const char* filename);

    // State
    bool quit_requested_ = false;
    std::string status_text_;

    // Volume state (dual-backend)
    VolumeType vol_type_ = VolumeType::NONE;
    hfsvol* vol_ = nullptr;
    HFSPlusVolume* hfsplus_vol_ = nullptr;
    std::string image_path_;
    std::string volume_name_;
    unsigned long vol_total_bytes_ = 0;
    unsigned long vol_free_bytes_ = 0;
    unsigned long blessed_cnid_ = 0;  // CNID of blessed System Folder

    // APM partition detection helper
    struct APMPartition {
        uint64_t offset;
        char type[32];
    };
    static int read_apm_partitions(const char* path, APMPartition* parts, int max_parts);

    // Directory state
    bool show_hidden_ = false;
    std::string current_path_;
    unsigned long current_cnid_ = 0;  // HFS+ folder CNID for current directory
    std::vector<unsigned long> cnid_stack_;  // HFS+ parent CNID stack for navigate_up
    std::vector<HFSEntry> entries_;
    int selected_entry_ = -1;

    // Popups
    bool show_error_ = false;
    std::string error_text_;

    bool show_confirm_ = false;
    std::string confirm_text_;
    std::string confirm_target_;
    bool confirm_is_dir_ = false;

    bool show_mkdir_ = false;
    char mkdir_name_[256] = {};

    bool show_rename_ = false;
    char rename_buf_[256] = {};

    bool show_check_ = false;
    std::string check_log_;

    // Cut/paste for move operations
    std::string cut_path_;         // full Mac-style path of cut entry
    std::string cut_name_;         // display name
    bool cut_is_dir_ = false;

    bool show_type_creator_ = false;
    char edit_type_[5] = {};
    char edit_creator_[5] = {};

    bool show_info_ = false;
    int info_entry_idx_ = -1;
    short info_fdflags_ = 0;
    char info_type_[5] = {};
    char info_creator_[5] = {};

    // File picker state
    enum class PickerMode { NONE, OPEN_IMAGE, EXPORT_FILE, EXPORT_BINHEX, EXPORT_FOLDER_BINHEX, EXPORT_ICON, IMPORT_FILE, IMPORT_FOLDER };
    PickerMode picker_mode_ = PickerMode::NONE;
    std::string picker_path_;
    char picker_input_[1024] = {};
    std::vector<std::string> picker_entries_;
    int picker_selected_ = -1;
    bool picker_show_hidden_ = false;

    void picker_refresh();
    void picker_navigate(const std::string& path);
};

#endif // APP_H
