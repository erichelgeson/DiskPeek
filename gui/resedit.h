#ifndef RESEDIT_H
#define RESEDIT_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"

typedef unsigned int GLuint;

#include "ResourceFile.hh"

struct ResEditWindow {
    std::string title;
    std::string filename;
    bool open = true;

    // Parsed resource fork
    std::unique_ptr<ResourceDASM::ResourceFile> rf;

    // Type list (sorted)
    struct TypeInfo {
        uint32_t type;
        char name[5];  // 4-char code + null
        size_t count;
    };
    std::vector<TypeInfo> types;
    int selected_type = -1;

    // Resource list for selected type
    struct ResInfo {
        int16_t id;
        std::string name;
        size_t size;
    };
    std::vector<ResInfo> resources;
    int selected_resource = -1;

    // Preview state
    GLuint preview_tex = 0;
    int preview_w = 0, preview_h = 0;
    std::string preview_text;
    enum class PreviewType { NONE, IMAGE, TEXT, HEX, SOUND, ERROR };
    PreviewType preview_type = PreviewType::NONE;
    std::string error_text;
    uint32_t last_preview_res_type = 0;
    int16_t last_preview_res_id = 0;

    // Hex dump cache
    std::string hex_dump;

    // Sound playback state
    std::string sound_wav;         // WAV file data
    uint32_t sound_sample_rate = 0;
    uint8_t sound_channels = 0;
    uint8_t sound_bits = 0;
    bool sound_playing = false;

    ResEditWindow() = default;
    ~ResEditWindow();
    ResEditWindow(const ResEditWindow&) = delete;
    ResEditWindow& operator=(const ResEditWindow&) = delete;

    // Open a resource fork from raw data
    bool open_rsrc(const std::string& name, const std::vector<uint8_t>& rsrc_data);

    // Render the window. Returns false if window should close.
    bool render();

    // Export all resources from raw rsrc fork data into a folder.
    // Each file is named {type}_{id}.{ext} with decoded formats where possible.
    static void dump_all_resources(const std::vector<uint8_t>& rsrc_data,
                                   const std::string& folder_path);

private:
    void populate_type_list();
    void populate_resource_list();
    void update_preview();
    void clear_preview();

    // Preview helpers
    bool try_preview_image(uint32_t type, int16_t id);
    bool try_preview_text(uint32_t type, int16_t id);
    bool try_preview_sound(uint32_t type, int16_t id);
    void make_hex_dump(const std::string& data);
    void copy_to_clipboard();
    void play_sound();
    void stop_sound();
    void save_all_of_type(const char* folder_path);
    static void save_resource_to_file(ResourceDASM::ResourceFile& rf,
                                      uint32_t type, int16_t id,
                                      const std::string& dir);

    // Current preview image (kept for clipboard PNG export)
    phosg::ImageRGBA8888N preview_image;

    // GL texture from RGBA data
    static GLuint upload_rgba_texture(const uint8_t* data, int w, int h);
};

#endif // RESEDIT_H
