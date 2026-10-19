#include "Window.h"
#include "Common/Constants.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <algorithm>
#include <cmath>
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
            // Cada backend só é encerrado se sua inicialização tiver sido concluída.
            ~ImGuiSession()
            {
                if (rendererReady)
                    ImGui_ImplSDLRenderer3_Shutdown();
                if (platformReady)
                    ImGui_ImplSDL3_Shutdown();
                ImGui::DestroyContext();
            }
        };

        void drawInterface(bool &running, bool gameLoaded, FileManager &fileManager,
                           SDL_Texture *texture, bool &bilinearFilter, bool &invertBAYB,
                           const Window::InvertBAYBCallback &invertBAYBCallback)
        {
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
                                               ImGuiWindowFlags_NoNavInputs;
            // O foco da área do jogo não deve ativar NavActive/WantCaptureKeyboard.
            // Menus e o seletor modal continuam recebendo a navegação do ImGui.
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            const bool visible = ImGui::Begin("R2BOY", nullptr, flags);
            ImGui::PopStyleVar(2);
            if (visible)
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
                    if (ImGui::BeginMenu("Display"))
                    {
                        ImGui::MenuItem("Bilinear Filter", nullptr, &bilinearFilter);
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Input"))
                    {
                        if (ImGui::MenuItem("Invert BA/YB Buttons", nullptr, &invertBAYB) && invertBAYBCallback)
                            invertBAYBCallback(invertBAYB);
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenuBar();
                }
                fileManager.draw();

                // Frame completo, centralizado e com proporção nativa.
                const ImVec2 available = ImGui::GetContentRegionAvail();
                const float scale = std::min(available.x / screenWidth, available.y / screenHeight);
                if (scale > 0.0f)
                {
                    const ImVec2 size(screenWidth * scale, screenHeight * scale);
                    const ImVec2 cursor = ImGui::GetCursorScreenPos();
                    const ImVec2 topLeft(cursor.x + (available.x - size.x) * 0.5f,
                                         cursor.y + (available.y - size.y) * 0.5f);
                    if (gameLoaded)
                    {
                        ImDrawList *drawList = ImGui::GetWindowDrawList();
                        const ImGuiPlatformIO &platform = ImGui::GetPlatformIO();
                        // O backend sobrescreve o scale mode da textura; a escolha
                        // deve acompanhar o comando de desenho do framebuffer.
                        drawList->AddCallback(bilinearFilter ? platform.DrawCallback_SetSamplerLinear
                                                             : platform.DrawCallback_SetSamplerNearest);
                        drawList->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(texture)),
                                           topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y));
                        drawList->AddCallback(platform.DrawCallback_SetSamplerLinear);
                    }
                    else
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y), IM_COL32(0, 0, 0, 255));
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
        std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture{nullptr, SDL_DestroyTexture};
        ImGuiSession gui;
        FileManager fileManager;
        bool bilinearFilter = false;
        bool invertBAYB = false;
        InvertBAYBCallback invertBAYBCallback;
        Uint64 fpsSampleStart = 0;
        unsigned presentedFrames = 0;
        bool gameWasLoaded = false;
        int displayWidth = 0;
        int displayHeight = 0;
    };

    Window::Window() = default;
    Window::~Window() = default;

    bool Window::open()
    {
        if (impl_)
            return true;
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            std::cerr << "Failed to initialize SDL: " << SDL_GetError() << '\n';
            SDL_Quit();
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto state = std::make_unique<Impl>();
        state->window.reset(SDL_CreateWindow(
            "R2BOY", screenWidth * 4, screenHeight * 4,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY));
        if (!state->window)
        {
            std::cerr << "Failed to create window: " << SDL_GetError() << '\n';
            return false;
        }
        if (!SDL_SetWindowPosition(state->window.get(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED))
        {
            // Alguns sistemas de janelas não permitem posicionamento pela aplicação.
            std::cerr << "Unable to center window: " << SDL_GetError() << '\n';
        }
        state->renderer.reset(SDL_CreateRenderer(state->window.get(), nullptr));
        if (!state->renderer)
        {
            std::cerr << "Failed to create renderer: " << SDL_GetError() << '\n';
            return false;
        }
        state->texture.reset(SDL_CreateTexture(state->renderer.get(), SDL_PIXELFORMAT_ARGB8888,
                                               SDL_TEXTUREACCESS_STREAMING, screenWidth, screenHeight));
        if (!state->texture || !SDL_SetTextureScaleMode(state->texture.get(), SDL_SCALEMODE_NEAREST))
        {
            std::cerr << "Failed to create display texture: " << SDL_GetError() << '\n';
            return false;
        }

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();
        state->gui.platformReady = ImGui_ImplSDL3_InitForSDLRenderer(state->window.get(), state->renderer.get());
        if (!state->gui.platformReady)
        {
            std::cerr << "Failed to initialize ImGui SDL3 backend\n";
            return false;
        }
        state->gui.rendererReady = ImGui_ImplSDLRenderer3_Init(state->renderer.get());
        if (!state->gui.rendererReady)
        {
            std::cerr << "Failed to initialize ImGui renderer backend\n";
            return false;
        }

        // Publica a sessão apenas quando todos os recursos estiverem prontos;
        // os retornos anteriores liberam automaticamente o estado parcial.
        impl_ = std::move(state);
        impl_->fpsSampleStart = SDL_GetTicksNS();
        return true;
    }

    bool Window::acceptsGameInput() const
    {
        return (SDL_GetWindowFlags(impl_->window.get()) & SDL_WINDOW_INPUT_FOCUS) != 0 &&
               !ImGui::GetIO().WantCaptureKeyboard;
    }

    void Window::pollEvents(bool &running, const InputHandler &inputHandler)
    {
        ImGuiIO &io = ImGui::GetIO();
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (inputHandler)
                inputHandler(event, acceptsGameInput());
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_O &&
                (event.key.mod & SDL_KMOD_CTRL) != 0 && !event.key.repeat)
                impl_->fileManager.requestOpen();
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(impl_->window.get())) ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && !io.WantCaptureKeyboard))
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

    void Window::setInvertBAYBCallback(InvertBAYBCallback callback)
    {
        impl_->invertBAYBCallback = std::move(callback);
    }

    bool Window::renderFrame(bool &running, bool gameLoaded, const Common::Framebuffer &framebuffer)
    {
        if (!SDL_UpdateTexture(impl_->texture.get(), nullptr, framebuffer.data(), screenWidth * sizeof(std::uint32_t)))
        {
            std::cerr << "Failed to update display texture: " << SDL_GetError() << '\n';
            return false;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        // A proporção aplica-se à tela abaixo do menu. Usa a dimensão alterada
        // pelo usuário para ajustar a outra, em coordenadas da janela SDL.
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSize(impl_->window.get(), &width, &height))
            return false;
        const float windowScale = height / ImGui::GetIO().DisplaySize.y;
        const int menuHeight = static_cast<int>(std::lround(ImGui::GetFrameHeight() * windowScale));
        int targetWidth = width;
        int targetHeight = height;
        if (impl_->displayWidth != 0 && width == impl_->displayWidth && height != impl_->displayHeight)
            targetWidth = std::max(1, static_cast<int>(std::lround(
                                          std::max(1, height - menuHeight) * static_cast<double>(screenWidth) / screenHeight)));
        else
            targetHeight = menuHeight + static_cast<int>(std::lround(
                                            width * static_cast<double>(screenHeight) / screenWidth));
        if ((targetWidth != width || targetHeight != height) &&
            !SDL_SetWindowSize(impl_->window.get(), targetWidth, targetHeight))
        {
            std::cerr << "Failed to resize display: " << SDL_GetError() << '\n';
            return false;
        }
        impl_->displayWidth = targetWidth;
        impl_->displayHeight = targetHeight;
        drawInterface(running, gameLoaded, impl_->fileManager, impl_->texture.get(), impl_->bilinearFilter,
                      impl_->invertBAYB, impl_->invertBAYBCallback);
        ImGui::Render();
        const ImGuiIO &io = ImGui::GetIO();
        SDL_Renderer *renderer = impl_->renderer.get();
        // O ImGui trabalha em coordenadas lógicas; o renderer acompanha a densidade
        // de pixels do monitor para manter interface e imagem no tamanho correto.
        if (!SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y) ||
            !SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) || !SDL_RenderClear(renderer))
        {
            std::cerr << "Failed to prepare frame: " << SDL_GetError() << '\n';
            return false;
        }
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (!SDL_RenderPresent(renderer))
        {
            std::cerr << "Failed to present frame: " << SDL_GetError() << '\n';
            return false;
        }
        // Mede apresentações reais por segundo; atualiza o título uma vez por segundo.
        const auto now = SDL_GetTicksNS();
        if (!gameLoaded || !impl_->gameWasLoaded)
        {
            impl_->presentedFrames = 0;
            impl_->fpsSampleStart = now;
            SDL_SetWindowTitle(impl_->window.get(), "R2BOY");
        }
        impl_->gameWasLoaded = gameLoaded;
        if (!gameLoaded)
            return true;
        ++impl_->presentedFrames;
        const auto elapsed = now - impl_->fpsSampleStart;
        if (elapsed >= 1000000000ULL)
        {
            const double fps = impl_->presentedFrames * 1000000000.0 / elapsed;
            char title[80];
            SDL_snprintf(title, sizeof(title), "R2BOY | FPS: %.2f", fps);
            SDL_SetWindowTitle(impl_->window.get(), title);
            impl_->presentedFrames = 0;
            impl_->fpsSampleStart = now;
        }
        return true;
    }
}