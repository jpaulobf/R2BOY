#include "Window.h"
#include "Common/Constants.h"
#include "Core/Cartridge/Cartridge.h"
#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>

namespace R2BOY::Window
{
    namespace
    {
        constexpr int screenWidth = Common::Constants::ScreenWidth;
        constexpr int screenHeight = Common::Constants::ScreenHeight;

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
                if (rendererReady)
                    ImGui_ImplSDLRenderer2_Shutdown();
                if (platformReady)
                    ImGui_ImplSDL2_Shutdown();
                ImGui::DestroyContext();
            }
        };

        void drawInterface(bool &running, const Core::Cartridge::Cartridge &cartridge, FileManager &fileManager)
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
                        if (ImGui::MenuItem("Load ROM...", "Ctrl+O"))
                            fileManager.requestOpen();
                        if (ImGui::MenuItem("Exit", "Esc"))
                            running = false;
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenuBar();
                }
                //ImGui::TextUnformatted("R2BOY - Game Boy / Game Boy Color");
                // if (cartridge.loaded())
                // {
                //     const auto &header = cartridge.header();
                //     ImGui::Text("%s | %s", header.title.empty() ? "Untitled ROM" : header.title.c_str(), header.compatibility());
                //     if (ImGui::CollapsingHeader("Cartridge details"))
                //     {
                //         ImGui::TextWrapped("Source: %s", cartridge.source().c_str());
                //         ImGui::Text("Type: %s (0x%02X)", header.typeName(), header.cartridgeType);
                //         ImGui::Text("ROM: %zu KiB (header: %zu KiB) | RAM: %zu bytes", cartridge.rom().size() / 1024, header.romSize / 1024, header.ramSize);
                //         ImGui::Text("Version: %u | SGB: %s | Region: 0x%02X", header.version,
                //                     header.sgbFlag == 0x03 ? "Yes" : "No", header.destinationCode);
                //         ImGui::Text("Manufacturer: %s | Licensee: %s (old: 0x%02X)", header.manufacturer.c_str(), header.newLicensee.c_str(), header.oldLicensee);
                //         ImGui::Text("Logo: %s | Header checksum: %s | Global checksum: %s",
                //                     header.logoValid ? "OK" : "Warning", header.headerChecksumValid ? "OK" : "Warning",
                //                     header.globalChecksumValid ? "OK" : "Warning");
                //         if (!header.romSize || header.romSize != cartridge.rom().size())
                //             ImGui::TextUnformatted("Warning: ROM size differs from the header.");
                //         if (header.ramSizeCode > 5)
                //             ImGui::TextUnformatted("Warning: unknown RAM size code.");
                //     }
                //     ImGui::TextUnformatted("ROM loaded. Hardware emulation is not implemented yet.");
                // }
                // else
                //     ImGui::TextUnformatted("No cartridge loaded. Use File > Open to select a ROM.");
                fileManager.draw();
                
                // Área provisória para o framebuffer do Game Boy, desenhada dentro do ImGui.
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

    struct Window::Impl
    {
        // Os membros são destruídos em ordem inversa: a sessão SDL permanece ativa
        // até a liberação do ImGui, do renderer e da janela nativa.
        SDLSession session;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{nullptr, SDL_DestroyWindow};
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{nullptr, SDL_DestroyRenderer};
        ImGuiSession gui;
        FileManager fileManager;
    };

    Window::Window() = default;
    Window::~Window() = default;

    bool Window::open()
    {
        if (impl_)
            return true;
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        {
            std::cerr << "Failed to initialize SDL: " << SDL_GetError() << '\n';
            SDL_Quit();
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto state = std::make_unique<Impl>();
        state->window.reset(SDL_CreateWindow(
            "R2BOY", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            screenWidth * 4, screenHeight * 4 + 80,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI));
        if (!state->window)
        {
            std::cerr << "Failed to create window: " << SDL_GetError() << '\n';
            return false;
        }
        state->renderer.reset(SDL_CreateRenderer(state->window.get(), -1, 0));
        if (!state->renderer)
        {
            std::cerr << "Failed to create renderer: " << SDL_GetError() << '\n';
            return false;
        }

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();
        state->gui.platformReady = ImGui_ImplSDL2_InitForSDLRenderer(state->window.get(), state->renderer.get());
        if (!state->gui.platformReady)
        {
            std::cerr << "Failed to initialize ImGui SDL2 backend\n";
            return false;
        }
        state->gui.rendererReady = ImGui_ImplSDLRenderer2_Init(state->renderer.get());
        if (!state->gui.rendererReady)
        {
            std::cerr << "Failed to initialize ImGui renderer backend\n";
            return false;
        }

        impl_ = std::move(state);
        return true;
    }

    void Window::pollEvents(bool &running)
    {
        ImGuiIO &io = ImGui::GetIO();
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_o &&
                (event.key.keysym.mod & KMOD_CTRL) != 0 && !event.key.repeat)
                impl_->fileManager.requestOpen();
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                 event.window.windowID == SDL_GetWindowID(impl_->window.get())) ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE && !io.WantCaptureKeyboard))
                running = false;
        }
    }

    bool Window::isMinimized() const
    {
        return (SDL_GetWindowFlags(impl_->window.get()) & SDL_WINDOW_MINIMIZED) != 0;
    }

    void Window::setRomOpenHandler(RomOpenHandler handler)
    {
        impl_->fileManager.setRomOpenHandler(std::move(handler));
    }

    bool Window::renderFrame(bool &running, const Core::Cartridge::Cartridge &cartridge)
    {
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        drawInterface(running, cartridge, impl_->fileManager);
        ImGui::Render();
        const ImGuiIO &io = ImGui::GetIO();
        SDL_Renderer *renderer = impl_->renderer.get();
        if (SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y) != 0 ||
            SDL_SetRenderDrawColor(renderer, 24, 24, 24, 255) != 0 || SDL_RenderClear(renderer) != 0)
        {
            std::cerr << "Failed to prepare frame: " << SDL_GetError() << '\n';
            return false;
        }
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer);
        return true;
    }
}
