/*
 * HFS Browser - Application implementation
 */

#include "app.h"
#include "imgui.h"

#include <SDL2/SDL.h>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cerrno>

static const TypeCreatorMap s_type_creator_map[] = {
    { ".txt",  "TEXT", "ttxt" },
    { ".text", "TEXT", "ttxt" },
    { ".c",    "TEXT", "ttxt" },
    { ".h",    "TEXT", "ttxt" },
    { ".cpp",  "TEXT", "ttxt" },
    { ".htm",  "TEXT", "MSIE" },
    { ".html", "TEXT", "MSIE" },
    { ".rtf",  "TEXT", "MSWD" },
    { ".sit",  "SIT!", "SIT!" },
    { ".hqx",  "TEXT", "SITx" },
    { ".bin",  "SIT!", "SIT!" },
    { ".jpg",  "JPEG", "ogle" },
    { ".jpeg", "JPEG", "ogle" },
    { ".gif",  "GIFf", "ogle" },
    { ".png",  "PNGf", "ogle" },
    { ".tif",  "TIFF", "ogle" },
    { ".tiff", "TIFF", "ogle" },
    { ".bmp",  "BMPf", "ogle" },
    { ".pict", "PICT", "ttxt" },
    { ".pdf",  "PDF ", "CARO" },
    { ".ps",   "TEXT", "vgrd" },
    { ".eps",  "EPSF", "vgrd" },
    { ".doc",  "W8BN", "MSWD" },
    { ".xls",  "XLS8", "XCEL" },
    { ".ppt",  "SLD8", "PPT3" },
    { ".wav",  "WAVE", "TVOD" },
    { ".aif",  "AIFF", "TVOD" },
    { ".aiff", "AIFF", "TVOD" },
    { ".mp3",  "MPG3", "TVOD" },
    { ".mov",  "MooV", "TVOD" },
    { ".avi",  "VfW ", "TVOD" },
    { ".zip",  "ZIP ", "SITx" },
    { ".sea",  "APPL", "????" },
    { ".dsk",  "dImg", "dCpy" },
    { ".img",  "dImg", "dCpy" },
    { ".dmg",  "dImg", "dCpy" },
    { nullptr, nullptr, nullptr },
};

const TypeCreatorMap* App::lookup_type_creator(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot) return nullptr;

    for (const TypeCreatorMap* m = s_type_creator_map; m->ext; m++) {
        if (strcasecmp(dot, m->ext) == 0)
            return m;
    }
    return nullptr;
}

App::App() {}
App::~App() { shutdown(); }

void App::init() {
    status_text_ = "Open an HFS disk image to begin (or drag & drop)";

    // Start picker in home directory
    const char* home = getenv("HOME");
    picker_path_ = home ? home : "/";
}

void App::shutdown() {
    close_image();
}

void App::open_image(const char* path) {
    close_image();

    fprintf(stderr, "hfsbrowser: opening %s\n", path);

    // Detect partition layout — pnum=0 means no partition map (raw HFS),
    // pnum>=1 means Apple partition map entry
    int nparts = hfs_nparts(path);
    fprintf(stderr, "hfsbrowser: nparts=%d\n", nparts);

    // Try each partition, then fall back to raw (pnum=0)
    int partitions_to_try[16];
    int ntry = 0;
    for (int i = 1; i <= nparts && ntry < 15; i++)
        partitions_to_try[ntry++] = i;
    partitions_to_try[ntry++] = 0;  // raw fallback

    bool readonly = false;
    for (int t = 0; t < ntry && !vol_; t++) {
        int pnum = partitions_to_try[t];
        vol_ = hfs_mount(path, pnum, HFS_MODE_RDWR);
        if (!vol_) {
            vol_ = hfs_mount(path, pnum, HFS_MODE_RDONLY);
            if (vol_) readonly = true;
        }
        fprintf(stderr, "hfsbrowser: tried pnum=%d -> %s\n", pnum,
                vol_ ? "ok" : (hfs_error ? hfs_error : "failed"));
    }

    if (!vol_) {
        set_error(std::string("Failed to open image: ") + (hfs_error ? hfs_error : "unknown error"));
        return;
    }

    if (readonly)
        status_text_ = "Opened (read-only): " + std::string(path);
    else
        status_text_ = "Opened: " + std::string(path);

    image_path_ = path;

    hfsvolent vstat;
    if (hfs_vstat(vol_, &vstat) == 0) {
        volume_name_ = vstat.name;
        vol_total_bytes_ = vstat.totbytes;
        vol_free_bytes_ = vstat.freebytes;
    }

    current_path_ = volume_name_ + ":";
    refresh_listing();
}

void App::close_image() {
    if (vol_) {
        hfs_umount(vol_);
        vol_ = nullptr;
    }
    image_path_.clear();
    volume_name_.clear();
    vol_total_bytes_ = 0;
    vol_free_bytes_ = 0;
    current_path_.clear();
    entries_.clear();
    selected_entry_ = -1;
    status_text_ = "Open an HFS disk image to begin";
}

void App::refresh_listing() {
    entries_.clear();
    selected_entry_ = -1;

    if (!vol_) return;

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
            memset(e.type, 0, sizeof(e.type));
            memset(e.creator, 0, sizeof(e.creator));
        } else {
            e.size = ent.u.file.dsize;
            memcpy(e.type, ent.u.file.type, 5);
            memcpy(e.creator, ent.u.file.creator, 5);
        }

        entries_.push_back(e);
    }

    hfs_closedir(dir);

    // Sort: directories first, then alphabetical
    std::sort(entries_.begin(), entries_.end(), [](const HFSEntry& a, const HFSEntry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    // Update volume stats
    hfsvolent vstat;
    if (hfs_vstat(vol_, &vstat) == 0) {
        vol_total_bytes_ = vstat.totbytes;
        vol_free_bytes_ = vstat.freebytes;
    }
}

void App::navigate_to(const char* dirname) {
    std::string new_path = current_path_ + dirname + ":";
    if (hfs_chdir(vol_, new_path.c_str()) == -1) {
        set_error(std::string("Failed to enter directory: ") + (hfs_error ? hfs_error : "unknown"));
        return;
    }
    current_path_ = new_path;
    refresh_listing();
}

void App::navigate_up() {
    // Find the parent by removing the last component
    // "Volume:Folder:SubFolder:" -> "Volume:Folder:"
    if (current_path_ == volume_name_ + ":") return; // already at root

    // Remove trailing colon, find previous colon
    std::string path = current_path_;
    if (!path.empty() && path.back() == ':') path.pop_back();
    size_t pos = path.rfind(':');
    if (pos == std::string::npos) return;

    std::string parent = path.substr(0, pos + 1);
    if (hfs_chdir(vol_, parent.c_str()) == -1) {
        set_error(std::string("Failed to navigate up: ") + (hfs_error ? hfs_error : "unknown"));
        return;
    }
    current_path_ = parent;
    refresh_listing();
}

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

    // Status bar
    ImGui::TextDisabled("%s", status_text_.c_str());

    // Popups
    render_error_popup();
    render_confirm_popup();
    render_mkdir_popup();
    render_file_picker();

    ImGui::End();
}

void App::render_toolbar() {
    if (ImGui::Button("Open Image")) {
        open_file_picker_for_open();
    }

    ImGui::SameLine();

    if (!vol_) ImGui::BeginDisabled();
    if (ImGui::Button("Close")) {
        close_image();
    }
    if (!vol_) ImGui::EndDisabled();

    if (vol_) {
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
    if (!vol_) {
        ImGui::TextDisabled("No image open");
        return;
    }

    ImGui::Text("Path: %s", current_path_.c_str());
}

void App::render_file_list() {
    float avail_h = ImGui::GetContentRegionAvail().y - 60.0f; // room for action bar + status
    if (avail_h < 100.0f) avail_h = 100.0f;

    ImGui::BeginChild("FileList", ImVec2(0, avail_h), true);

    if (!vol_) {
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

    // Column headers
    if (ImGui::BeginTable("files", 4,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type/Creator", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)entries_.size(); i++) {
            const HFSEntry& e = entries_[i];

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // Icon + name
            char label[256];
            snprintf(label, sizeof(label), "%s %s",
                e.is_dir ? "\xF0\x9F\x93\x81" : "\xF0\x9F\x93\x84",  // folder/file emoji
                e.name.c_str());

            bool selected = (selected_entry_ == i);
            if (ImGui::Selectable(label, selected,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                selected_entry_ = i;
                if (ImGui::IsMouseDoubleClicked(0) && e.is_dir) {
                    navigate_to(e.name.c_str());
                }
            }

            // Type/Creator
            ImGui::TableNextColumn();
            if (!e.is_dir && e.type[0]) {
                ImGui::Text("%s/%s", e.type, e.creator);
            }

            // Size
            ImGui::TableNextColumn();
            if (!e.is_dir) {
                ImGui::Text("%s", format_size(e.size).c_str());
            }

            // Kind
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", e.is_dir ? "folder" : "file");
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
}

void App::render_action_bar() {
    bool has_vol = (vol_ != nullptr);
    bool has_sel = (selected_entry_ >= 0 && selected_entry_ < (int)entries_.size());
    bool sel_is_file = has_sel && !entries_[selected_entry_].is_dir;


    if (!has_vol) ImGui::BeginDisabled();

    if (ImGui::Button("Copy to Mac")) {
        if (has_vol)
            open_file_picker_for_import();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Import a file from your computer into the HFS image");

    ImGui::SameLine();

    if (!sel_is_file) ImGui::BeginDisabled();
    if (ImGui::Button("Copy from Mac")) {
        if (sel_is_file)
            open_file_picker_for_export();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Export selected file from HFS image to your computer");
    if (!sel_is_file) ImGui::EndDisabled();

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

    if (ImGui::Button("New Folder")) {
        memset(mkdir_name_, 0, sizeof(mkdir_name_));
        show_mkdir_ = true;
        ImGui::OpenPopup("New Folder");
    }

    ImGui::SameLine();

    if (ImGui::Button("Refresh")) {
        refresh_listing();
    }

    if (!has_vol) ImGui::EndDisabled();
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
            int rc;
            if (confirm_is_dir_) {
                rc = hfs_rmdir(vol_, confirm_target_.c_str());
            } else {
                rc = hfs_delete(vol_, confirm_target_.c_str());
            }
            if (rc == -1) {
                set_error(std::string("Delete failed: ") + (hfs_error ? hfs_error : "unknown"));
            } else {
                status_text_ = "Deleted: " + confirm_target_;
                refresh_listing();
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
                if (hfs_mkdir(vol_, full_path.c_str()) == -1) {
                    set_error(std::string("mkdir failed: ") + (hfs_error ? hfs_error : "unknown"));
                } else {
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

// Simple ImGui-based file picker (avoids nativefiledialog dependency)
void App::render_file_picker() {
    if (picker_mode_ == PickerMode::NONE) return;

    const char* title = "Open HFS Image";
    if (picker_mode_ == PickerMode::EXPORT_FILE) title = "Save File To";
    else if (picker_mode_ == PickerMode::IMPORT_FILE) title = "Select File to Import";

    ImGui::OpenPopup(title);

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_None)) {
        // Path input
        ImGui::Text("Path:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##path", picker_input_, sizeof(picker_input_),
                ImGuiInputTextFlags_EnterReturnsTrue)) {
            struct stat st;
            if (stat(picker_input_, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    picker_navigate(picker_input_);
                } else {
                    // Selected a file
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

        // File list
        ImGui::BeginChild("PickerList", ImVec2(0, -30), true);

        // Parent directory
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
                if (is_dir) {
                    // Remove trailing /
                    full.pop_back();
                }
                snprintf(picker_input_, sizeof(picker_input_), "%s", full.c_str());

                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (is_dir) {
                        picker_navigate(full);
                    } else {
                        // File selected on double-click
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
                        }
                        return;
                    }
                }
            }
        }

        ImGui::EndChild();

        // Bottom buttons
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

    DIR* d = opendir(picker_path_.c_str());
    if (!d) return;

    std::vector<std::string> dirs, files;
    struct dirent* de;
    while ((de = readdir(d)) != nullptr) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (!picker_show_hidden_ && de->d_name[0] == '.')
            continue;

        std::string full = picker_path_ + "/" + de->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            dirs.push_back(std::string(de->d_name) + "/");
        } else {
            files.push_back(de->d_name);
        }
    }
    closedir(d);

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
    if (!vol_ || selected_entry_ < 0) return;
    open_file_picker_for_export();
}

void App::copy_to_hfs() {
    if (!vol_) return;
    open_file_picker_for_import();
}

void App::copy_from_hfs_impl(const std::string& host_path) {
    if (!vol_ || selected_entry_ < 0 || selected_entry_ >= (int)entries_.size()) return;
    const HFSEntry& e = entries_[selected_entry_];
    if (e.is_dir) return;

    std::string hfs_path = current_path_ + e.name;

    // Determine output path - if host_path is a directory, append filename
    std::string out_path = host_path;
    struct stat st;
    if (stat(host_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        out_path = host_path + "/" + e.name;
    }

    hfsfile* f = hfs_open(vol_, hfs_path.c_str());
    if (!f) {
        set_error(std::string("Failed to open HFS file: ") + (hfs_error ? hfs_error : "unknown"));
        return;
    }

    FILE* out = fopen(out_path.c_str(), "wb");
    if (!out) {
        hfs_close(f);
        set_error("Failed to create output file: " + out_path);
        return;
    }

    char buf[8192];
    unsigned long n;
    bool ok = true;
    unsigned long total = 0;

    while ((n = hfs_read(f, buf, sizeof(buf))) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
        total += n;
    }

    fclose(out);
    hfs_close(f);

    if (ok) {
        status_text_ = "Exported: " + e.name + " (" + format_size(total) + ")";
    } else {
        set_error("Write error while exporting " + e.name);
    }
}

void App::copy_to_hfs_impl(const std::string& host_path) {
    if (!vol_) return;

    FILE* in = fopen(host_path.c_str(), "rb");
    if (!in) {
        set_error("Failed to open file: " + host_path);
        return;
    }

    // Extract filename from path
    std::string filename;
    size_t pos = host_path.rfind('/');
    if (pos != std::string::npos)
        filename = host_path.substr(pos + 1);
    else
        filename = host_path;

    // Truncate to HFS max filename length
    if (filename.length() > HFS_MAX_FLEN) {
        filename = filename.substr(0, HFS_MAX_FLEN);
    }

    // Lookup type/creator from extension
    const char* type = "????";
    const char* creator = "????";
    const TypeCreatorMap* tc = lookup_type_creator(filename.c_str());
    if (tc) {
        type = tc->type;
        creator = tc->creator;
    }

    std::string hfs_path = current_path_ + filename;

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
}

void App::delete_selected() {
    // Handled by confirm popup
}

void App::mkdir_selected() {
    // Handled by mkdir popup
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
