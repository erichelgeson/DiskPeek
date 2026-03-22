/*
 * Disk Peek - GUI for browsing Macintosh HFS disk images
 * Main entry point with SDL3/OpenGL/ImGui setup
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include "app.h"
#include "fonts.h"

#include <cstring>
#include <cstdio>

ImFont* g_mono_font = nullptr;

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "Error: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow(
        "Disk Peek",
        1024, 700,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );

    if (!window) {
        fprintf(stderr, "Error: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        fprintf(stderr, "Error: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Scale UI based on display DPI
    float scale = SDL_GetWindowDisplayScale(window);
    if (scale < 1.0f) scale = 1.0f;

    // Classic Mac OS 6/7 inspired theme
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    // Window
    c[ImGuiCol_WindowBg]            = ImVec4(0.93f, 0.93f, 0.93f, 1.00f);
    c[ImGuiCol_ChildBg]             = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    c[ImGuiCol_PopupBg]             = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);

    // Text
    c[ImGuiCol_Text]                = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    c[ImGuiCol_TextDisabled]        = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

    // Borders
    c[ImGuiCol_Border]              = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
    c[ImGuiCol_BorderShadow]        = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);

    // Frames
    c[ImGuiCol_FrameBg]             = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    c[ImGuiCol_FrameBgHovered]      = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    c[ImGuiCol_FrameBgActive]       = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);

    // Title bar
    c[ImGuiCol_TitleBg]             = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
    c[ImGuiCol_TitleBgActive]       = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]    = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    c[ImGuiCol_MenuBarBg]           = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);

    // Buttons
    c[ImGuiCol_Button]              = ImVec4(0.83f, 0.83f, 0.83f, 1.00f);
    c[ImGuiCol_ButtonHovered]       = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
    c[ImGuiCol_ButtonActive]        = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);

    // Headers
    c[ImGuiCol_Header]              = ImVec4(0.00f, 0.00f, 0.00f, 0.15f);
    c[ImGuiCol_HeaderHovered]       = ImVec4(0.00f, 0.00f, 0.00f, 0.25f);
    c[ImGuiCol_HeaderActive]        = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]         = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]       = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.55f, 0.55f, 0.55f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

    // Separator
    c[ImGuiCol_Separator]           = ImVec4(0.00f, 0.00f, 0.00f, 0.30f);
    c[ImGuiCol_SeparatorHovered]    = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
    c[ImGuiCol_SeparatorActive]     = ImVec4(0.00f, 0.00f, 0.00f, 0.70f);

    // Selection
    c[ImGuiCol_TextSelectedBg]      = ImVec4(0.00f, 0.00f, 0.50f, 0.35f);

    // Table
    c[ImGuiCol_TableHeaderBg]       = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
    c[ImGuiCol_TableBorderStrong]   = ImVec4(0.00f, 0.00f, 0.00f, 0.30f);
    c[ImGuiCol_TableBorderLight]    = ImVec4(0.00f, 0.00f, 0.00f, 0.15f);
    c[ImGuiCol_TableRowBg]          = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    c[ImGuiCol_TableRowBgAlt]       = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);

    // Misc
    c[ImGuiCol_CheckMark]           = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    c[ImGuiCol_SliderGrab]          = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    c[ImGuiCol_SliderGrabActive]    = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);

    // Style tweaks
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 0.0f;
    style.GrabRounding      = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.TabRounding       = 0.0f;
    style.FrameBorderSize   = 1.0f;
    style.WindowBorderSize  = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FramePadding      = ImVec2(6, 3);
    style.ItemSpacing       = ImVec2(6, 4);

    style.ScaleAllSizes(scale);

    // Load Chicago font with extended glyph ranges for MacRoman characters
    // Default ImGui range is U+0020-U+00FF; MacRoman needs additional glyphs
    // like ƒ (U+0192), curly quotes, ellipsis, em dash, etc.
    static const ImWchar mac_glyph_ranges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
        0x0131, 0x0131, // ı (dotless i)
        0x0152, 0x0153, // Œ œ
        0x0178, 0x0178, // Ÿ
        0x0192, 0x0192, // ƒ (folder symbol)
        0x02C6, 0x02DD, // modifier letters (ˆ ˜ ¯ ˘ ˙ ˚ ¸ ˝ ˛ ˇ)
        0x2013, 0x2026, // – — ' ' " " † ‡ • …
        0x2030, 0x2030, // ‰
        0x2039, 0x203A, // ‹ ›
        0x2044, 0x2044, // ⁄
        0x20AC, 0x20AC, // €
        0x2122, 0x2122, // ™
        0x2202, 0x2202, // ∂
        0x2206, 0x2206, // ∆
        0x220F, 0x220F, // ∏
        0x2211, 0x2211, // ∑
        0x221A, 0x221A, // √
        0x221E, 0x221E, // ∞
        0x222B, 0x222B, // ∫
        0x2248, 0x2248, // ≈
        0x2260, 0x2260, // ≠
        0x2264, 0x2265, // ≤ ≥
        0x25CA, 0x25CA, // ◊
        0xFB01, 0xFB02, // fi fl ligatures
        0,
    };

    ImFont* font = io.Fonts->AddFontFromFileTTF("lib/fonts/ChicagoFLF.ttf", 14.0f * scale, nullptr, mac_glyph_ranges);
    if (!font)
        io.Fonts->AddFontDefault();

    // Load built-in monospace font for hex dumps etc.
    ImFontConfig mono_cfg;
    mono_cfg.SizePixels = 13.0f * scale;
    snprintf(mono_cfg.Name, sizeof(mono_cfg.Name), "ProggyClean (mono)");
    g_mono_font = io.Fonts->AddFontDefault(&mono_cfg);

    io.FontGlobalScale = 1.0f;

    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    App app;
    app.init();

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window))
                done = true;
            if (event.type == SDL_EVENT_DROP_FILE) {
                const char* dropped = event.drop.data;
                if (dropped) {
                    const char* ext = strrchr(dropped, '.');
                    bool is_image = ext && strcasecmp(ext, ".hda") == 0;

                    if (is_image) {
                        app.open_image(dropped);
                    } else if (app.has_volume()) {
                        app.import_file(dropped);
                    } else {
                        app.open_image(dropped);
                    }
                }
                // SDL3: drop.data is managed by SDL, do not free
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        app.render();

        // Update OS window title to reflect volume state
        if (app.has_volume())
            SDL_SetWindowTitle(window, ("Disk Peek \xe2\x80\x94 " + app.volume_name()).c_str());
        else
            SDL_SetWindowTitle(window, "Disk Peek");

        if (app.should_quit())
            done = true;

        ImGui::Render();
        int display_w, display_h;
        SDL_GetWindowSizeInPixels(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.93f, 0.93f, 0.93f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    app.shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
