#include "Window.h"
#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <algorithm>
#include <iostream>
#include <memory>

namespace
{
constexpr int screenWidth = 160;
constexpr int screenHeight = 144;

struct SDLSession
{
    ~SDLSession() { SDL_Quit(); }
};

struct ImGuiSession
{
    bool platformReady = false;
    bool rendererReady = false;
    ~ImGuiSession()
    {
        if (rendererReady) ImGui_ImplSDLRenderer2_Shutdown();
        if (platformReady) ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
};

void drawInterface(bool &running)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (ImGui::Begin("R2BOY", nullptr, flags))
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit", "Esc")) running = false;
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        ImGui::TextUnformatted("R2BOY - Game Boy / Game Boy Color");
        ImGui::TextUnformatted("No cartridge loaded.");
        ImGui::Separator();
        // Placeholder for the future Game Boy framebuffer, drawn inside ImGui.
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float scale = std::min(available.x / screenWidth, available.y / screenHeight);
        if (scale > 0.0f)
        {
            const ImVec2 size(screenWidth * scale, screenHeight * scale);
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const ImVec2 topLeft(cursor.x + (available.x - size.x) * 0.5f,
                                 cursor.y + (available.y - size.y) * 0.5f);
            ImGui::GetWindowDrawList()->AddRectFilled(
                topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y), IM_COL32(155, 188, 15, 255));
            ImGui::Dummy(available);
        }
    }
    ImGui::End();
}
}

int Window::run()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << '\n';
        return 1;
    }
    const SDLSession session;
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("R2BOY", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         screenWidth * 4, screenHeight * 4 + 80,
                         SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI),
        SDL_DestroyWindow);
    if (!window)
    {
        std::cerr << "Failed to create window: " << SDL_GetError() << '\n';
        return 1;
    }
    const std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateRenderer(window.get(), -1, 0), SDL_DestroyRenderer);
    if (!renderer)
    {
        std::cerr << "Failed to create renderer: " << SDL_GetError() << '\n';
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiSession gui;
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    gui.platformReady = ImGui_ImplSDL2_InitForSDLRenderer(window.get(), renderer.get());
    if (!gui.platformReady)
    {
        std::cerr << "Failed to initialize ImGui SDL2 backend\n";
        return 1;
    }
    gui.rendererReady = ImGui_ImplSDLRenderer2_Init(renderer.get());
    if (!gui.rendererReady)
    {
        std::cerr << "Failed to initialize ImGui renderer backend\n";
        return 1;
    }

    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                 event.window.windowID == SDL_GetWindowID(window.get())) ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE && !io.WantCaptureKeyboard))
                running = false;
        }
        if (!running) break;
        if (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(16);
            continue;
        }
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        drawInterface(running);
        ImGui::Render();
        if (SDL_RenderSetScale(renderer.get(), io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y) != 0 ||
            SDL_SetRenderDrawColor(renderer.get(), 24, 24, 24, 255) != 0 || SDL_RenderClear(renderer.get()) != 0)
        {
            std::cerr << "Failed to prepare frame: " << SDL_GetError() << '\n';
            return 1;
        }
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer.get());
        SDL_Delay(16); // Placeholder until hardware timing is implemented.
    }
    return 0;
}
