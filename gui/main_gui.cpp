/*
 * HFS Browser - GUI for browsing Macintosh HFS disk images
 * Main entry point with SDL2/OpenGL/ImGui setup
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include "app.h"

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
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

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );

    SDL_Window* window = SDL_CreateWindow(
        "HFS Browser",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 700,
        window_flags
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
    float dpi = 0;
    float scale = 1.0f;
    if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window), &dpi, nullptr, nullptr) == 0 && dpi > 0) {
        scale = dpi / 96.0f;
        if (scale < 1.0f) scale = 1.0f;
    }

    // Classic Mac OS 6/7 inspired theme
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    // Window
    c[ImGuiCol_WindowBg]            = ImVec4(0.93f, 0.93f, 0.93f, 1.00f); // light gray desktop
    c[ImGuiCol_ChildBg]             = ImVec4(1.00f, 1.00f, 1.00f, 1.00f); // white content area
    c[ImGuiCol_PopupBg]             = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);

    // Text
    c[ImGuiCol_Text]                = ImVec4(0.00f, 0.00f, 0.00f, 1.00f); // black text
    c[ImGuiCol_TextDisabled]        = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

    // Borders — dark outer, white inner bevel
    c[ImGuiCol_Border]              = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
    c[ImGuiCol_BorderShadow]        = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);

    // Frames (input fields, checkboxes)
    c[ImGuiCol_FrameBg]             = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    c[ImGuiCol_FrameBgHovered]      = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    c[ImGuiCol_FrameBgActive]       = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);

    // Title bar
    c[ImGuiCol_TitleBg]             = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
    c[ImGuiCol_TitleBgActive]       = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]    = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    c[ImGuiCol_MenuBarBg]           = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);

    // Buttons — beveled 3D look
    c[ImGuiCol_Button]              = ImVec4(0.83f, 0.83f, 0.83f, 1.00f);
    c[ImGuiCol_ButtonHovered]       = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
    c[ImGuiCol_ButtonActive]        = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);

    // Headers (table headers, collapsing headers)
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

    // Selection (classic Mac highlight = dark blue/black inversion)
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

    // Style tweaks for Mac OS look
    style.WindowRounding    = 0.0f;  // sharp corners like classic Mac
    style.FrameRounding     = 0.0f;
    style.GrabRounding      = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.TabRounding       = 0.0f;
    style.FrameBorderSize   = 1.0f;  // visible borders on controls
    style.WindowBorderSize  = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FramePadding      = ImVec2(6, 3);
    style.ItemSpacing       = ImVec2(6, 4);

    style.ScaleAllSizes(scale);
    io.Fonts->AddFontDefault();
    io.FontGlobalScale = scale;

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    App app;
    app.init();

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
                done = true;
            if (event.type == SDL_DROPFILE) {
                if (app.has_volume()) {
                    // Volume is open — import the dropped file
                    app.import_file(event.drop.file);
                } else {
                    app.open_image(event.drop.file);
                }
                SDL_free(event.drop.file);
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        app.render();

        if (app.should_quit())
            done = true;

        ImGui::Render();
        int display_w, display_h;
        SDL_GetWindowSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.93f, 0.93f, 0.93f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    app.shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
