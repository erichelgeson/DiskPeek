#include "resedit.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "imgui.h"
#include "fonts.h"
#include <SDL3/SDL.h>

#include "ResourceFile.hh"
#include "IndexFormats/Formats.hh"

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

// Static buffer for clipboard PNG data (must outlive the SDL callback)
static std::string s_clip_png;

using namespace ResourceDASM;

// Helper: convert 4-byte resource type to display string
static void type_to_str(uint32_t type, char out[5]) {
    out[0] = (type >> 24) & 0xFF;
    out[1] = (type >> 16) & 0xFF;
    out[2] = (type >> 8) & 0xFF;
    out[3] = type & 0xFF;
    out[4] = '\0';
}

ResEditWindow::~ResEditWindow() {
    clear_preview();
}

void ResEditWindow::clear_preview() {
    if (preview_tex) {
        glDeleteTextures(1, &preview_tex);
        preview_tex = 0;
    }
    stop_sound();
    preview_w = preview_h = 0;
    preview_text.clear();
    hex_dump.clear();
    sound_wav.clear();
    sound_sample_rate = 0;
    sound_channels = 0;
    sound_bits = 0;
    preview_type = PreviewType::NONE;
    preview_image = phosg::ImageRGBA8888N(0, 0);
    last_preview_res_type = 0;
    last_preview_res_id = 0;
}

void ResEditWindow::stop_sound() {
    if (sound_playing) {
        // SDL3 audio streams are cleaned up automatically, but we can
        // clear the playing flag. Streams are fire-and-forget with
        // SDL_OpenAudioDeviceStream.
        sound_playing = false;
    }
}

void ResEditWindow::play_sound() {
    if (sound_wav.empty()) return;

    // Load WAV from memory
    SDL_IOStream* rw = SDL_IOFromConstMem(sound_wav.data(), sound_wav.size());
    if (!rw) return;

    SDL_AudioSpec spec;
    Uint8* audio_buf = nullptr;
    Uint32 audio_len = 0;
    if (!SDL_LoadWAV_IO(rw, true, &spec, &audio_buf, &audio_len)) {
        fprintf(stderr, "SDL_LoadWAV_IO failed: %s\n", SDL_GetError());
        return;
    }

    // Open a stream and queue the data
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream) {
        fprintf(stderr, "SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
        SDL_free(audio_buf);
        return;
    }

    SDL_PutAudioStreamData(stream, audio_buf, audio_len);
    SDL_FlushAudioStream(stream);
    SDL_ResumeAudioStreamDevice(stream);
    SDL_free(audio_buf);
    sound_playing = true;
}

bool ResEditWindow::try_preview_sound(uint32_t type, int16_t id) {
    if (!rf) return false;

    try {
        ResourceFile::DecodedSoundResource snd;
        bool decoded = false;

        if (type == resource_type("snd ")) {
            snd = rf->decode_snd(id, type);
            decoded = true;
        } else if (type == resource_type("csnd")) {
            snd = rf->decode_csnd(id, type);
            decoded = true;
        } else if (type == resource_type("esnd")) {
            snd = rf->decode_esnd(id, type);
            decoded = true;
        } else if (type == resource_type("ESnd")) {
            snd = rf->decode_ESnd(id, type);
            decoded = true;
        }

        if (decoded && !snd.data.empty() && !snd.is_mp3) {
            clear_preview();
            sound_wav = std::move(snd.data);
            sound_sample_rate = snd.sample_rate;
            sound_channels = snd.num_channels;
            sound_bits = snd.bits_per_sample;
            preview_type = PreviewType::SOUND;
            return true;
        }
    } catch (const std::exception& e) {
        char name[5];
        type_to_str(type, name);
        error_text = std::string("Sound decode failed for ") + name + " " +
                     std::to_string(id) + ": " + e.what();
    }
    return false;
}

void ResEditWindow::copy_to_clipboard() {
    if (preview_type == PreviewType::IMAGE && preview_image.get_width() > 0) {
        s_clip_png = preview_image.serialize(phosg::ImageFormat::PNG);
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
    } else if (preview_type == PreviewType::TEXT && !preview_text.empty()) {
        SDL_SetClipboardText(preview_text.c_str());
    } else if (preview_type == PreviewType::HEX && !hex_dump.empty()) {
        SDL_SetClipboardText(hex_dump.c_str());
    }
}

GLuint ResEditWindow::upload_rgba_texture(const uint8_t* data, int w, int h) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // phosg stores pixels as uint32_t with R in MSB: 0xRRGGBBAA
    // GL_UNSIGNED_INT_8_8_8_8 reads the uint32 value directly, matching this layout
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, data);
    return tex;
}

bool ResEditWindow::open_rsrc(const std::string& name, const std::vector<uint8_t>& rsrc_data) {
    filename = name;
    title = "Resources: \"" + name + "\"";

    try {
        std::string data_str(reinterpret_cast<const char*>(rsrc_data.data()), rsrc_data.size());
        rf = std::make_unique<ResourceFile>(parse_resource_fork(data_str));
        populate_type_list();
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to parse resource fork: %s\n", e.what());
        return false;
    }
}

void ResEditWindow::populate_type_list() {
    types.clear();
    if (!rf) return;

    auto all_types = rf->all_resource_types();
    for (uint32_t t : all_types) {
        TypeInfo ti;
        ti.type = t;
        type_to_str(t, ti.name);
        ti.count = rf->count_resources_of_type(t);
        types.push_back(ti);
    }
    // Sort alphabetically by type name
    std::sort(types.begin(), types.end(), [](const TypeInfo& a, const TypeInfo& b) {
        return strcmp(a.name, b.name) < 0;
    });
}

void ResEditWindow::populate_resource_list() {
    resources.clear();
    if (!rf || selected_type < 0 || selected_type >= (int)types.size()) return;

    uint32_t type = types[selected_type].type;
    auto ids = rf->all_resources_of_type(type);
    for (int16_t id : ids) {
        ResInfo ri;
        ri.id = id;
        try {
            ri.name = rf->get_resource_name(type, id);
        } catch (...) {}
        try {
            auto res = rf->get_resource(type, id);
            ri.size = res->data.size();
        } catch (...) {
            ri.size = 0;
        }
        resources.push_back(ri);
    }
}

void ResEditWindow::make_hex_dump(const std::string& data) {
    hex_dump.clear();
    // Limit to first 4KB for display
    size_t len = std::min(data.size(), (size_t)4096);
    hex_dump.reserve(len * 5);

    for (size_t i = 0; i < len; i += 16) {
        char line[80];
        int pos = snprintf(line, sizeof(line), "%04X  ", (unsigned)i);

        // Hex bytes
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len) {
                pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", (uint8_t)data[i + j]);
            } else {
                pos += snprintf(line + pos, sizeof(line) - pos, "   ");
            }
            if (j == 7) line[pos++] = ' ';
        }

        // ASCII
        pos += snprintf(line + pos, sizeof(line) - pos, " |");
        for (size_t j = 0; j < 16 && (i + j) < len; j++) {
            char c = data[i + j];
            line[pos++] = (c >= 0x20 && c < 0x7F) ? c : '.';
        }
        line[pos++] = '|';
        line[pos++] = '\n';
        line[pos] = '\0';
        hex_dump += line;
    }
    if (data.size() > 4096) {
        char tail[64];
        snprintf(tail, sizeof(tail), "\n... (%zu more bytes)", data.size() - 4096);
        hex_dump += tail;
    }
}

// Helper to convert any Image to RGBA8888N for GL upload
static phosg::ImageRGBA8888N to_rgba(const phosg::ImageG1& src) {
    return src.convert_monochrome_to_color<phosg::PixelFormat::RGBA8888_NATIVE>(
        0xFFFFFFFF, 0x000000FF);
}
static phosg::ImageRGBA8888N to_rgba(const phosg::ImageGA11& src) {
    return src.convert_monochrome_to_color<phosg::PixelFormat::RGBA8888_NATIVE>(
        0x00000000, 0x000000FF);
}
static phosg::ImageRGBA8888N to_rgba(const phosg::ImageRGB888& src) {
    phosg::ImageRGBA8888N ret(src.get_width(), src.get_height());
    ret.copy_from(src, 0, 0, src.get_width(), src.get_height(), 0, 0);
    return ret;
}

bool ResEditWindow::try_preview_image(uint32_t type, int16_t id) {
    if (!rf) return false;

    try {
        phosg::ImageRGBA8888N img(0, 0);
        bool decoded = false;

        if (type == resource_type("cicn")) {
            auto dec = ResourceFile::decode_cicn(rf->get_resource(type, id));
            img = std::move(dec.image);
            decoded = true;
        } else if (type == resource_type("icl8")) {
            img = rf->decode_icl8(id, type);
            decoded = true;
        } else if (type == resource_type("icl4")) {
            img = rf->decode_icl4(id, type);
            decoded = true;
        } else if (type == resource_type("ics8")) {
            img = rf->decode_ics8(id, type);
            decoded = true;
        } else if (type == resource_type("ics4")) {
            img = rf->decode_ics4(id, type);
            decoded = true;
        } else if (type == resource_type("ICN#")) {
            auto dec = ResourceFile::decode_ICNN(rf->get_resource(type, id));
            if (!dec.composite.empty()) {
                img = to_rgba(dec.composite);
                decoded = true;
            }
        } else if (type == resource_type("ICON")) {
            auto mono = ResourceFile::decode_ICON(rf->get_resource(type, id));
            img = to_rgba(mono);
            decoded = true;
        } else if (type == resource_type("SICN")) {
            auto icons = ResourceFile::decode_SICN(rf->get_resource(type, id));
            if (!icons.empty()) {
                img = to_rgba(icons[0]);
                decoded = true;
            }
        } else if (type == resource_type("PICT")) {
            auto dec = rf->decode_PICT(id, type, false);
            img = std::move(dec.image);
            decoded = true;
        } else if (type == resource_type("icns")) {
            auto dec = ResourceFile::decode_icns(rf->get_resource(type, id));
            for (auto& [t, image] : dec.type_to_composite_image) {
                if (image.get_width() > img.get_width()) {
                    img = std::move(image);
                    decoded = true;
                }
            }
            if (!decoded) {
                for (auto& [t, image] : dec.type_to_image) {
                    if (image.get_width() > img.get_width()) {
                        img = std::move(image);
                        decoded = true;
                    }
                }
            }
        } else if (type == resource_type("CURS")) {
            auto dec = ResourceFile::decode_CURS(rf->get_resource(type, id));
            img = to_rgba(dec.bitmap);
            decoded = true;
        } else if (type == resource_type("crsr")) {
            auto dec = ResourceFile::decode_crsr(rf->get_resource(type, id));
            img = std::move(dec.image);
            decoded = true;
        } else if (type == resource_type("ppat")) {
            auto dec = ResourceFile::decode_ppat(rf->get_resource(type, id));
            img = to_rgba(dec.pattern);
            decoded = true;
        } else if (type == resource_type("PAT ")) {
            auto mono = ResourceFile::decode_PAT(rf->get_resource(type, id));
            img = to_rgba(mono);
            decoded = true;
        }

        if (decoded && img.get_width() > 0 && img.get_height() > 0) {
            clear_preview();
            preview_w = img.get_width();
            preview_h = img.get_height();
            preview_tex = upload_rgba_texture(
                reinterpret_cast<const uint8_t*>(img.get_data()),
                preview_w, preview_h);
            preview_image = std::move(img);
            preview_type = PreviewType::IMAGE;
            return true;
        }
    } catch (const std::exception& e) {
        char name[5];
        type_to_str(type, name);
        error_text = std::string("Decode failed for ") + name + " " +
                     std::to_string(id) + ": " + e.what();
    }
    return false;
}

bool ResEditWindow::try_preview_text(uint32_t type, int16_t id) {
    if (!rf) return false;

    try {
        std::string text;

        if (type == resource_type("STR ")) {
            auto dec = ResourceFile::decode_STR(rf->get_resource(type, id));
            text = dec.str;
        } else if (type == resource_type("STR#")) {
            auto dec = ResourceFile::decode_STRN(rf->get_resource(type, id));
            for (size_t i = 0; i < dec.strs.size(); i++) {
                text += "[" + std::to_string(i) + "] " + dec.strs[i] + "\n";
            }
        } else if (type == resource_type("TEXT")) {
            text = ResourceFile::decode_TEXT(rf->get_resource(type, id));
        } else if (type == resource_type("vers")) {
            auto dec = ResourceFile::decode_vers(rf->get_resource(type, id));
            text = "Version: " + dec.version_number + "\n" + dec.version_message;
        } else if (type == resource_type("MENU")) {
            auto dec = ResourceFile::decode_MENU(rf->get_resource(type, id));
            text = "Menu: " + dec.title + "\n";
            for (size_t i = 0; i < dec.items.size(); i++) {
                auto& item = dec.items[i];
                if (item.name == "-") {
                    text += "  --------\n";
                } else {
                    text += "  " + item.name;
                    if (item.key_equivalent) {
                        text += std::string("  Cmd+") + item.key_equivalent;
                    }
                    text += "\n";
                }
            }
        } else if (type == resource_type("DLOG")) {
            auto dec = ResourceFile::decode_DLOG(rf->get_resource(type, id));
            text = "Dialog: " + dec.title + "\n";
            text += "Bounds: " + std::to_string(dec.bounds.x1) + "," +
                    std::to_string(dec.bounds.y1) + " - " +
                    std::to_string(dec.bounds.x2) + "," +
                    std::to_string(dec.bounds.y2) + "\n";
            text += "Visible: " + std::string(dec.visible ? "yes" : "no") + "\n";
        } else if (type == resource_type("WIND")) {
            auto dec = ResourceFile::decode_WIND(rf->get_resource(type, id));
            text = "Window: " + dec.title + "\n";
            text += "Bounds: " + std::to_string(dec.bounds.x1) + "," +
                    std::to_string(dec.bounds.y1) + " - " +
                    std::to_string(dec.bounds.x2) + "," +
                    std::to_string(dec.bounds.y2) + "\n";
        } else if (type == resource_type("DITL")) {
            auto items = ResourceFile::decode_DITL(rf->get_resource(type, id));
            text = "Dialog Items (" + std::to_string(items.size()) + "):\n";
            for (size_t i = 0; i < items.size(); i++) {
                auto& item = items[i];
                const char* type_names[] = {
                    "Button", "Checkbox", "Radio", "Control", "Help",
                    "Text", "EditText", "Icon", "Picture", "Custom", "Unknown"
                };
                int ti = std::min((int)item.type, 10);
                text += "  [" + std::to_string(i) + "] " + type_names[ti];
                if (!item.text.empty()) {
                    text += ": " + item.text;
                }
                text += "\n";
            }
        } else if (type == resource_type("CNTL")) {
            auto dec = ResourceFile::decode_CNTL(rf->get_resource(type, id));
            text = "Control: " + dec.title + "\n";
            text += "Value: " + std::to_string(dec.value) +
                    " Min: " + std::to_string(dec.min) +
                    " Max: " + std::to_string(dec.max) + "\n";
        } else if (type == resource_type("SIZE")) {
            auto dec = ResourceFile::decode_SIZE(rf->get_resource(type, id));
            text = "Preferred size: " + std::to_string(dec.size) + " bytes\n";
            text += "Minimum size: " + std::to_string(dec.min_size) + " bytes\n";
            text += "Can background: " + std::string(dec.can_background ? "yes" : "no") + "\n";
            text += "32-bit compatible: " + std::string(dec.clean_addressing ? "yes" : "no") + "\n";
        } else {
            return false;
        }

        if (!text.empty()) {
            clear_preview();
            preview_text = std::move(text);
            preview_type = PreviewType::TEXT;
            return true;
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "Text decode failed for type %.4s id %d: %s\n",
                (char*)&type, id, e.what());
    }
    return false;
}

void ResEditWindow::save_resource_to_file(ResourceFile& rf, uint32_t type, int16_t id, const std::string& dir) {
    char name[5];
    type_to_str(type, name);
    std::string base = dir + "/" + std::string(name) + "_" + std::to_string(id);

    try {
        // Sound types → WAV
        if (type == resource_type("snd ") || type == resource_type("csnd") ||
            type == resource_type("esnd") || type == resource_type("ESnd")) {
            ResourceFile::DecodedSoundResource snd;
            if (type == resource_type("snd ")) snd = rf.decode_snd(id, type);
            else if (type == resource_type("csnd")) snd = rf.decode_csnd(id, type);
            else if (type == resource_type("esnd")) snd = rf.decode_esnd(id, type);
            else snd = rf.decode_ESnd(id, type);
            if (!snd.data.empty() && !snd.is_mp3) {
                std::string path = base + ".wav";
                FILE* f = fopen(path.c_str(), "wb");
                if (f) { fwrite(snd.data.data(), 1, snd.data.size(), f); fclose(f); }
                return;
            }
        }

        // Image types → PNG
        phosg::ImageRGBA8888N img(0, 0);
        bool is_image = false;

        if (type == resource_type("cicn")) {
            img = ResourceFile::decode_cicn(rf.get_resource(type, id)).image;
            is_image = true;
        } else if (type == resource_type("icl8")) {
            img = rf.decode_icl8(id, type); is_image = true;
        } else if (type == resource_type("icl4")) {
            img = rf.decode_icl4(id, type); is_image = true;
        } else if (type == resource_type("ics8")) {
            img = rf.decode_ics8(id, type); is_image = true;
        } else if (type == resource_type("ics4")) {
            img = rf.decode_ics4(id, type); is_image = true;
        } else if (type == resource_type("ICN#")) {
            auto dec = ResourceFile::decode_ICNN(rf.get_resource(type, id));
            if (!dec.composite.empty()) { img = to_rgba(dec.composite); is_image = true; }
        } else if (type == resource_type("ICON")) {
            img = to_rgba(ResourceFile::decode_ICON(rf.get_resource(type, id))); is_image = true;
        } else if (type == resource_type("SICN")) {
            auto icons = ResourceFile::decode_SICN(rf.get_resource(type, id));
            if (!icons.empty()) { img = to_rgba(icons[0]); is_image = true; }
        } else if (type == resource_type("PICT")) {
            img = rf.decode_PICT(id, type, false).image; is_image = true;
        } else if (type == resource_type("icns")) {
            auto dec = ResourceFile::decode_icns(rf.get_resource(type, id));
            for (auto& [t, image] : dec.type_to_composite_image) {
                if (image.get_width() > img.get_width()) { img = std::move(image); is_image = true; }
            }
            if (!is_image) {
                for (auto& [t, image] : dec.type_to_image) {
                    if (image.get_width() > img.get_width()) { img = std::move(image); is_image = true; }
                }
            }
        } else if (type == resource_type("CURS")) {
            img = to_rgba(ResourceFile::decode_CURS(rf.get_resource(type, id)).bitmap); is_image = true;
        } else if (type == resource_type("crsr")) {
            img = ResourceFile::decode_crsr(rf.get_resource(type, id)).image; is_image = true;
        } else if (type == resource_type("ppat")) {
            img = to_rgba(ResourceFile::decode_ppat(rf.get_resource(type, id)).pattern); is_image = true;
        } else if (type == resource_type("PAT ")) {
            img = to_rgba(ResourceFile::decode_PAT(rf.get_resource(type, id))); is_image = true;
        }

        if (is_image && img.get_width() > 0 && img.get_height() > 0) {
            std::string png = img.serialize(phosg::ImageFormat::PNG);
            std::string path = base + ".png";
            FILE* f = fopen(path.c_str(), "wb");
            if (f) { fwrite(png.data(), 1, png.size(), f); fclose(f); }
            return;
        }

        // Text types → .txt
        std::string text;
        if (type == resource_type("STR ")) {
            text = ResourceFile::decode_STR(rf.get_resource(type, id)).str;
        } else if (type == resource_type("STR#")) {
            auto dec = ResourceFile::decode_STRN(rf.get_resource(type, id));
            for (size_t i = 0; i < dec.strs.size(); i++)
                text += "[" + std::to_string(i) + "] " + dec.strs[i] + "\n";
        } else if (type == resource_type("TEXT")) {
            text = ResourceFile::decode_TEXT(rf.get_resource(type, id));
        }
        if (!text.empty()) {
            std::string path = base + ".txt";
            FILE* f = fopen(path.c_str(), "wb");
            if (f) { fwrite(text.data(), 1, text.size(), f); fclose(f); }
            return;
        }

        // Fallback: raw data as .bin
        auto res = rf.get_resource(type, id);
        std::string path = base + ".bin";
        FILE* f = fopen(path.c_str(), "wb");
        if (f) { fwrite(res->data.data(), 1, res->data.size(), f); fclose(f); }
    } catch (const std::exception& e) {
        fprintf(stderr, "Export failed for %.4s %d: %s\n", name, id, e.what());
        // On decode failure, save raw data
        try {
            auto res = rf.get_resource(type, id);
            std::string path = base + ".bin";
            FILE* f = fopen(path.c_str(), "wb");
            if (f) { fwrite(res->data.data(), 1, res->data.size(), f); fclose(f); }
        } catch (...) {}
    }
}

void ResEditWindow::save_all_of_type(const char* folder_path) {
    if (!rf || selected_type < 0 || selected_type >= (int)types.size()) return;

    uint32_t type = types[selected_type].type;
    auto ids = rf->all_resources_of_type(type);
    for (int16_t id : ids) {
        save_resource_to_file(*rf, type, id, folder_path);
    }
}

void ResEditWindow::dump_all_resources(const std::vector<uint8_t>& rsrc_data,
                                       const std::string& folder_path) {
    try {
        std::string data_str(reinterpret_cast<const char*>(rsrc_data.data()), rsrc_data.size());
        auto rf = ResourceDASM::parse_resource_fork(data_str);
        auto all_types = rf.all_resource_types();
        for (uint32_t type : all_types) {
            auto ids = rf.all_resources_of_type(type);
            for (int16_t id : ids) {
                save_resource_to_file(rf, type, id, folder_path);
            }
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "dump_all_resources failed: %s\n", e.what());
    }
}

void ResEditWindow::update_preview() {
    if (!rf || selected_type < 0 || selected_resource < 0) {
        clear_preview();
        return;
    }
    if (selected_type >= (int)types.size() || selected_resource >= (int)resources.size()) {
        clear_preview();
        return;
    }

    uint32_t type = types[selected_type].type;
    int16_t id = resources[selected_resource].id;

    // Don't re-decode if same resource
    if (type == last_preview_res_type && id == last_preview_res_id) return;
    last_preview_res_type = type;
    last_preview_res_id = id;

    error_text.clear();

    // Try image preview first
    if (try_preview_image(type, id)) return;

    // Try sound preview
    if (try_preview_sound(type, id)) return;

    // Try text/structured preview
    if (try_preview_text(type, id)) return;

    // Fall back to hex dump (with decode error shown if applicable)
    try {
        auto res = rf->get_resource(type, id);
        std::string saved_error = std::move(error_text);
        clear_preview();
        error_text = std::move(saved_error);
        make_hex_dump(res->data);
        preview_type = PreviewType::HEX;
    } catch (...) {
        clear_preview();
    }
}

bool ResEditWindow::render() {
    if (!open) return false;

    ImGui::SetNextWindowSize(ImVec2(850, 550), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return open;
    }

    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetContentRegionAvail().y;
    float type_panel_w = 150.0f;
    float right_panel_w = avail_w - type_panel_w - ImGui::GetStyle().ItemSpacing.x;

    // Left panel: type list
    ImGui::BeginChild("TypeList", ImVec2(type_panel_w, avail_h), ImGuiChildFlags_Borders);
    for (int i = 0; i < (int)types.size(); i++) {
        char label[32];
        snprintf(label, sizeof(label), "%s (%zu)", types[i].name, types[i].count);
        if (ImGui::Selectable(label, selected_type == i)) {
            if (selected_type != i) {
                selected_type = i;
                populate_resource_list();
                // Auto-select first resource and show preview
                if (!resources.empty()) {
                    selected_resource = 0;
                    update_preview();
                } else {
                    selected_resource = -1;
                    clear_preview();
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right panel: resource list + preview
    ImGui::BeginChild("RightPanel", ImVec2(right_panel_w, avail_h));

    float res_list_h = avail_h * 0.4f;
    float preview_h_remaining = avail_h - res_list_h - ImGui::GetStyle().ItemSpacing.y;

    // Resource list (top right)
    if (selected_type >= 0 && selected_type < (int)types.size() && !resources.empty()) {
        if (ImGui::Button("Save All...")) {
            SDL_ShowOpenFolderDialog(
                [](void* userdata, const char* const* filelist, int) {
                    auto* self = static_cast<ResEditWindow*>(userdata);
                    if (filelist && filelist[0]) {
                        self->save_all_of_type(filelist[0]);
                    }
                },
                this, nullptr, nullptr, false);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Export all %s resources to folder", types[selected_type].name);
    }
    ImGui::BeginChild("ResourceList", ImVec2(0, res_list_h), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("ResTable", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)resources.size(); i++) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            char id_str[16];
            snprintf(id_str, sizeof(id_str), "%d##res%d", resources[i].id, i);
            if (ImGui::Selectable(id_str, selected_resource == i,
                    ImGuiSelectableFlags_SpanAllColumns)) {
                selected_resource = i;
                update_preview();
            }
            // Also update when keyboard-navigated (arrow keys)
            if (ImGui::IsItemFocused() && selected_resource != i) {
                selected_resource = i;
                update_preview();
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(resources[i].name.c_str());

            ImGui::TableSetColumnIndex(2);
            if (resources[i].size >= 1024) {
                ImGui::Text("%.1f KB", resources[i].size / 1024.0);
            } else {
                ImGui::Text("%zu B", resources[i].size);
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    // Preview (bottom right)
    ImGui::BeginChild("Preview", ImVec2(0, preview_h_remaining), ImGuiChildFlags_Borders);

    // Copy button + Ctrl+C shortcut
    bool has_copyable = (preview_type == PreviewType::IMAGE ||
                         preview_type == PreviewType::TEXT ||
                         preview_type == PreviewType::HEX);
    if (has_copyable) {
        if (ImGui::Button("Copy")) {
            copy_to_clipboard();
        }
        ImGui::SameLine();
        if (preview_type == PreviewType::IMAGE) {
            ImGui::Text("Preview (%dx%d)", preview_w, preview_h);
        } else if (preview_type == PreviewType::TEXT) {
            ImGui::TextDisabled("Text");
        } else {
            ImGui::TextDisabled("Hex");
        }
    }

    // Sound playback controls
    if (preview_type == PreviewType::SOUND) {
        if (ImGui::Button("Play")) {
            play_sound();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save WAV...")) {
            SDL_ShowSaveFileDialog(
                [](void* userdata, const char* const* filelist, int) {
                    auto* self = static_cast<ResEditWindow*>(userdata);
                    if (filelist && filelist[0] && !self->sound_wav.empty()) {
                        FILE* f = fopen(filelist[0], "wb");
                        if (f) {
                            fwrite(self->sound_wav.data(), 1, self->sound_wav.size(), f);
                            fclose(f);
                        }
                    }
                },
                this, nullptr, nullptr, 0, nullptr);
        }
        ImGui::SameLine();
        ImGui::Text("%u Hz, %u-bit, %s",
            sound_sample_rate, sound_bits,
            sound_channels == 1 ? "mono" : "stereo");
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu bytes)", sound_wav.size());
    }

    // Ctrl+C keyboard shortcut (when this window is focused)
    if (has_copyable && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
        copy_to_clipboard();
    }

    if (preview_type == PreviewType::IMAGE && preview_tex) {
        float max_w = ImGui::GetContentRegionAvail().x;
        float max_h = ImGui::GetContentRegionAvail().y;
        float scale = std::min(max_w / preview_w, max_h / preview_h);
        if (scale > 4.0f) scale = 4.0f;
        ImGui::Image((ImTextureID)(intptr_t)preview_tex,
                     ImVec2(preview_w * scale, preview_h * scale));
    } else if (preview_type == PreviewType::TEXT) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::InputTextMultiline("##text_preview", preview_text.data(), preview_text.size() + 1,
            avail, ImGuiInputTextFlags_ReadOnly);
    } else if (preview_type == PreviewType::HEX) {
        if (!error_text.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
            ImGui::TextWrapped("%s", error_text.c_str());
            ImGui::PopStyleColor();
        }
        if (g_mono_font) ImGui::PushFont(g_mono_font);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::InputTextMultiline("##hex_preview", hex_dump.data(), hex_dump.size() + 1,
            avail, ImGuiInputTextFlags_ReadOnly);
        if (g_mono_font) ImGui::PopFont();
    } else if (preview_type == PreviewType::SOUND) {
        // Sound info already shown in the controls above
        ImGui::TextDisabled("Press Play to listen");
    } else if (selected_resource >= 0) {
        ImGui::TextDisabled("(no preview available)");
    } else {
        ImGui::TextDisabled("Select a resource to preview");
    }

    ImGui::EndChild();
    ImGui::EndChild(); // RightPanel

    ImGui::End();
    return open;
}
