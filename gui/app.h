/*
 * HFS Browser - Application state and UI logic
 */

#ifndef APP_H
#define APP_H

#include <string>
#include <vector>

extern "C" {
#include "hfs.h"
}

struct HFSEntry {
    std::string name;
    bool is_dir;
    unsigned long cnid;
    unsigned long size;       // data fork size (files only)
    char type[5];             // file type (files only)
    char creator[5];          // file creator (files only)
    short fdflags;
};

// Type/creator lookup for common extensions
struct TypeCreatorMap {
    const char* ext;
    const char* type;
    const char* creator;
};

class App {
public:
    App();
    ~App();

    void init();
    void render();
    void shutdown();
    bool should_quit() const { return quit_requested_; }

    void open_image(const char* path);

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

    // HFS operations
    void close_image();
    void refresh_listing();
    void navigate_to(const char* path);
    void navigate_up();
    void copy_from_hfs();     // export selected file to host
    void copy_to_hfs();       // import host file to HFS
    void copy_from_hfs_impl(const std::string& host_path);
    void copy_to_hfs_impl(const std::string& host_path);
    void delete_selected();
    void mkdir_selected();

    // File picker helpers
    void open_file_picker_for_open();
    void open_file_picker_for_export();
    void open_file_picker_for_import();

    // Utility
    std::string format_size(unsigned long bytes);
    void set_error(const std::string& msg);
    static const TypeCreatorMap* lookup_type_creator(const char* filename);

    // State
    bool quit_requested_ = false;
    std::string status_text_;

    // HFS state
    hfsvol* vol_ = nullptr;
    std::string image_path_;
    std::string volume_name_;
    unsigned long vol_total_bytes_ = 0;
    unsigned long vol_free_bytes_ = 0;

    // Directory state
    std::string current_path_;
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
    char mkdir_name_[32] = {};

    // File picker state
    enum class PickerMode { NONE, OPEN_IMAGE, EXPORT_FILE, IMPORT_FILE };
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
