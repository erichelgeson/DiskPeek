/*
 * HFS Browser - GUI for browsing Macintosh HFS disk images
 * Main entry point with SDL3/OpenGL/ImGui setup
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include "app.h"

#include <cstring>

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
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
        "HFS Browser",
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
    io.Fonts->AddFontDefault();
    io.FontGlobalScale = scale;

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
