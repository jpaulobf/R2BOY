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

        enum class DisplayMode
        {
            Windowed,
            FullscreenStretched,
            Fullscreen
        };

        struct DisplaySettings
        {
            int scale = 4;
            bool scanlines = false;
            int scanlinesLevel = 5;
            DisplayMode mode = DisplayMode::Windowed;
            bool pending = true;
        };

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
                           const Window::InvertBAYBCallback &invertBAYBCallback, DisplaySettings &display,
                           Window::RomAction &romAction, int &stateSlot,
                           bool vsyncEnabled, const std::function<void()> &toggleVSync,
                           bool &fastForwardEnabled, const Window::FastForwardCallback &fastForwardCallback,
                           bool &cpuOverclockEnabled, const Window::CPUOverclockCallback &cpuOverclockCallback)
        {
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            const bool showMenu = display.mode == DisplayMode::Windowed;
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoNavInputs;
            if (showMenu)
                flags |= ImGuiWindowFlags_MenuBar;
            // O foco da área do jogo não deve ativar NavActive/WantCaptureKeyboard.
            // Menus e o seletor modal continuam recebendo a navegação do ImGui.
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 255));
            const bool visible = ImGui::Begin("R2BOY", nullptr, flags);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            if (visible)
            {
                if (showMenu && ImGui::BeginMenuBar())
                {
                    if (ImGui::BeginMenu("File"))
                    {
                        if (ImGui::MenuItem("Open ROM...", "Ctrl+O"))
                            fileManager.requestOpen();
                        if (ImGui::MenuItem("Reset", "F12", false, gameLoaded))
                            romAction = Window::RomAction::Reset;
                        if (ImGui::MenuItem("Unload", nullptr, false, gameLoaded))
                            romAction = Window::RomAction::Unload;

                        ImGui::Separator();
                        if (ImGui::MenuItem("Save State", "F5", false, gameLoaded))
                            romAction = Window::RomAction::SaveState;
                        if (ImGui::MenuItem("Load State", "F6", false, gameLoaded))
                            romAction = Window::RomAction::LoadState;
                        if (ImGui::BeginMenu("Save State Slot", gameLoaded))
                        {
                            for (int slot = 1; slot <= 3; ++slot)
                            {
                                char label[16];
                                SDL_snprintf(label, sizeof(label), "Slot %d", slot);
                                if (ImGui::MenuItem(label, nullptr, stateSlot == slot))
                                    stateSlot = slot;
                            }
                            ImGui::EndMenu();
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Exit"))
                            running = false;
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Display"))
                    {
                        if (ImGui::MenuItem("VSync", nullptr, vsyncEnabled))
                            toggleVSync();
                        ImGui::Separator();
                        ImGui::MenuItem("Bilinear Filter", nullptr, &bilinearFilter);
                        ImGui::MenuItem("Scanlines", nullptr, &display.scanlines);
                        if (ImGui::BeginMenu("Scanlines Level", display.scanlines))
                        {
                            for (int level = 5; level <= 25; level += 5)
                            {
                                char label[8];
                                SDL_snprintf(label, sizeof(label), "%d%%", level);
                                if (ImGui::MenuItem(label, nullptr, display.scanlinesLevel == level))
                                    display.scanlinesLevel = level;
                            }
                            ImGui::EndMenu();
                        }
                        ImGui::Separator();
                        for (int scale = 1; scale <= 5; ++scale)
                        {
                            char label[8];
                            SDL_snprintf(label, sizeof(label), "%dX", scale);
                            if (ImGui::MenuItem(label, nullptr, display.scale == scale && display.mode == DisplayMode::Windowed))
                            {
                                display.scale = scale;
                                display.mode = DisplayMode::Windowed;
                                display.pending = true;
                            }
                        }
                        ImGui::Separator();
                        const auto fullscreenItem = [&display](const char *label, DisplayMode mode)
                        {
                            if (ImGui::MenuItem(label, nullptr, display.mode == mode))
                            {
                                display.mode = display.mode == mode ? DisplayMode::Windowed : mode;
                                display.pending = true;
                            }
                        };
                        fullscreenItem("Borderless Fullscreen Stretched", DisplayMode::FullscreenStretched);
                        fullscreenItem("Borderless Fullscreen", DisplayMode::Fullscreen);
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Input"))
                    {
                        if (ImGui::MenuItem("Invert BA/YB Buttons", nullptr, &invertBAYB) && invertBAYBCallback)
                            invertBAYBCallback(invertBAYB);
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Hacks"))
                    {
                        if (ImGui::MenuItem("Enable Fast Forward", nullptr, &fastForwardEnabled) && fastForwardCallback)
                            fastForwardCallback(fastForwardEnabled);
                        if (ImGui::MenuItem("Enable CPU Overclock", nullptr, &cpuOverclockEnabled) && cpuOverclockCallback)
                            cpuOverclockCallback(cpuOverclockEnabled);
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
                    const ImVec2 size = display.mode == DisplayMode::FullscreenStretched
                                            ? available
                                            : ImVec2(screenWidth * scale, screenHeight * scale);
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
        RomAction romAction = RomAction::None;
        int stateSlot = 1;
        bool bilinearFilter = false;
        bool invertBAYB = false;
        InvertBAYBCallback invertBAYBCallback;
        bool vsyncEnabled = false;
        VSyncCallback vsyncCallback;
        bool vsyncSuppressed = false;
        bool fastForwardEnabled = true;
        FastForwardCallback fastForwardCallback;
        bool cpuOverclockEnabled = false;
        CPUOverclockCallback cpuOverclockCallback;
        double currentFPS = 0;
        Uint64 fpsSampleStart = 0;
        unsigned presentedFrames = 0;
        bool gameWasLoaded = false;
        int displayWidth = 0;
        int displayHeight = 0;
        DisplaySettings display;
        Common::Framebuffer filteredFrame{};
        DisplayMode appliedMode = DisplayMode::Windowed;

        void updateTitle()
        {
            if (!gameWasLoaded && !cpuOverclockEnabled)
            {
                SDL_SetWindowTitle(window.get(), "R2BOY");
                return;
            }
            char title[128];
            SDL_snprintf(title, sizeof(title), "R2BOY | FPS: %.2f%s", currentFPS,
                         cpuOverclockEnabled ? " | CPU OVERCLOCK ENABLED! GAMES CAN CRASH!" : "");
            SDL_SetWindowTitle(window.get(), title);
        }

        bool updateDisplay(int menuHeight)
        {
            SDL_Window *nativeWindow = window.get();
            if (display.pending)
            {
                const bool fullscreen = display.mode != DisplayMode::Windowed;
                const bool wasFullscreen = appliedMode != DisplayMode::Windowed;
                if (fullscreen != wasFullscreen)
                {
                    // Modo nulo usa o desktop atual: nunca solicita fullscreen exclusivo.
                    if ((fullscreen && !SDL_SetWindowFullscreenMode(nativeWindow, nullptr)) ||
                        !SDL_SetWindowFullscreen(nativeWindow, fullscreen) || !SDL_SyncWindow(nativeWindow))
                        return false;
                }
                if (!fullscreen)
                {
                    if ((SDL_GetWindowFlags(nativeWindow) & SDL_WINDOW_MAXIMIZED) != 0 &&
                        (!SDL_RestoreWindow(nativeWindow) || !SDL_SyncWindow(nativeWindow)))
                        return false;
                    if (!SDL_SetWindowSize(nativeWindow, screenWidth * display.scale,
                                           screenHeight * display.scale + menuHeight) ||
                        !SDL_SyncWindow(nativeWindow))
                        return false;
                    const int centered = SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetDisplayForWindow(nativeWindow));
                    if (!SDL_SetWindowPosition(nativeWindow, centered, centered))
                        std::cerr << "Unable to center window: " << SDL_GetError() << '\n';
                    // Não confunde a restauração de nX com um redimensionamento manual.
                    displayWidth = 0;
                    displayHeight = 0;
                }
                appliedMode = display.mode;
                display.pending = false;
            }
            if (appliedMode != DisplayMode::Windowed)
                return true;

            // Em janela, mantém a proporção abaixo do menu ao redimensionar à mão.
            int width = 0;
            int height = 0;
            if (!SDL_GetWindowSize(nativeWindow, &width, &height))
                return false;
            int targetWidth = width;
            int targetHeight = height;
            if (displayWidth != 0 && width == displayWidth && height != displayHeight)
                targetWidth = std::max(1, static_cast<int>(std::lround(
                                              std::max(1, height - menuHeight) * static_cast<double>(screenWidth) / screenHeight)));
            else
                targetHeight = menuHeight + static_cast<int>(std::lround(
                                                width * static_cast<double>(screenHeight) / screenWidth));
            if ((targetWidth != width || targetHeight != height) &&
                !SDL_SetWindowSize(nativeWindow, targetWidth, targetHeight))
                return false;
            displayWidth = targetWidth;
            displayHeight = targetHeight;
            return true;
        }
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
            "R2BOY", screenWidth * state->display.scale, screenHeight * state->display.scale,
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
        state->vsyncEnabled = SDL_SetRenderVSync(state->renderer.get(), 1);
        if (!state->vsyncEnabled)
            std::cerr << "Unable to enable VSync: " << SDL_GetError() << '\n';
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
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (inputHandler)
                inputHandler(event, acceptsGameInput());
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && impl_->gameWasLoaded &&
                (event.key.key == SDLK_F5 || event.key.key == SDLK_F6))
                impl_->romAction = event.key.key == SDLK_F5 ? RomAction::SaveState : RomAction::LoadState;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F12 &&
                !event.key.repeat && impl_->gameWasLoaded)
                impl_->romAction = RomAction::Reset;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE &&
                !event.key.repeat && impl_->display.mode != DisplayMode::Windowed)
            {
                impl_->display.mode = DisplayMode::Windowed;
                impl_->display.pending = true;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_O &&
                (event.key.mod & SDL_KMOD_CTRL) != 0 && !event.key.repeat)
                impl_->fileManager.requestOpen();
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(impl_->window.get())))
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

    void Window::setVSyncCallback(VSyncCallback callback)
    {
        impl_->vsyncCallback = std::move(callback);
    }

    void Window::toggleVSync()
    {
        const bool enabled = !impl_->vsyncEnabled;
        if (!SDL_SetRenderVSync(impl_->renderer.get(), enabled && !impl_->vsyncSuppressed ? 1 : 0))
        {
            std::cerr << "Unable to change VSync: " << SDL_GetError() << '\n';
            return;
        }
        impl_->vsyncEnabled = enabled;
        if (impl_->vsyncCallback)
            impl_->vsyncCallback(enabled);
    }

    bool Window::isVSyncEnabled() const
    {
        return impl_->vsyncEnabled;
    }

    bool Window::setVSyncSuppressed(bool suppressed)
    {
        if (impl_->vsyncSuppressed == suppressed)
            return true;
        if (!SDL_SetRenderVSync(impl_->renderer.get(), impl_->vsyncEnabled && !suppressed ? 1 : 0))
        {
            std::cerr << "Unable to suppress/restore VSync: " << SDL_GetError() << '\n';
            return false;
        }
        impl_->vsyncSuppressed = suppressed;
        return true;
    }

    void Window::setFastForwardCallback(FastForwardCallback callback)
    {
        impl_->fastForwardCallback = std::move(callback);
    }

    bool Window::isFastForwardEnabled() const
    {
        return impl_->fastForwardEnabled;
    }

    void Window::setCPUOverclockCallback(CPUOverclockCallback callback)
    {
        impl_->cpuOverclockCallback = std::move(callback);
    }

    bool Window::isCPUOverclockEnabled() const
    {
        return impl_->cpuOverclockEnabled;
    }

    Window::RomAction Window::takeRomAction()
    {
        return std::exchange(impl_->romAction, RomAction::None);
    }

    int Window::stateSlot() const
    {
        return impl_->stateSlot;
    }

    bool Window::renderFrame(bool &running, bool gameLoaded, const Common::Framebuffer &framebuffer)
    {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSize(impl_->window.get(), &width, &height))
            return false;
        const float windowScale = height / ImGui::GetIO().DisplaySize.y;
        const int menuHeight = static_cast<int>(std::lround(ImGui::GetFrameHeight() * windowScale));
        if (!impl_->updateDisplay(menuHeight))
        {
            std::cerr << "Failed to update display mode: " << SDL_GetError() << '\n';
            return false;
        }
        bool titleChanged = impl_->gameWasLoaded != gameLoaded;
        const bool wasOverclockEnabled = impl_->cpuOverclockEnabled;
        drawInterface(running, gameLoaded, impl_->fileManager, impl_->texture.get(), impl_->bilinearFilter, impl_->invertBAYB, impl_->invertBAYBCallback, impl_->display, impl_->romAction, impl_->stateSlot, impl_->vsyncEnabled, [this]()
                      { toggleVSync(); }, impl_->fastForwardEnabled, impl_->fastForwardCallback, impl_->cpuOverclockEnabled, impl_->cpuOverclockCallback);
        titleChanged |= wasOverclockEnabled != impl_->cpuOverclockEnabled;
        const Common::Framebuffer *textureFrame = &framebuffer;
        if (impl_->display.scanlines)
        {
            // Escurece linhas alternadas na textura, preservando o framebuffer do Core.
            impl_->filteredFrame = framebuffer;
            const unsigned brightness = 100 - impl_->display.scanlinesLevel;
            for (int y = 1; y < screenHeight; y += 2)
            {
                for (int x = 0; x < screenWidth; ++x)
                {
                    auto &pixel = impl_->filteredFrame[y * screenWidth + x];
                    const unsigned red = (((pixel >> 16) & 0xFF) * brightness + 50) / 100;
                    const unsigned green = (((pixel >> 8) & 0xFF) * brightness + 50) / 100;
                    const unsigned blue = ((pixel & 0xFF) * brightness + 50) / 100;
                    pixel = (pixel & 0xFF000000u) | (red << 16) | (green << 8) | blue;
                }
            }
            textureFrame = &impl_->filteredFrame;
        }
        if (!SDL_UpdateTexture(impl_->texture.get(), nullptr, textureFrame->data(), screenWidth * sizeof(std::uint32_t)))
        {
            std::cerr << "Failed to update display texture: " << SDL_GetError() << '\n';
            return false;
        }
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
            impl_->currentFPS = 0;
        }
        impl_->gameWasLoaded = gameLoaded;
        if (!gameLoaded)
        {
            if (titleChanged)
                impl_->updateTitle();
            return true;
        }
        ++impl_->presentedFrames;
        const auto elapsed = now - impl_->fpsSampleStart;
        if (elapsed >= 1000000000ULL)
        {
            impl_->currentFPS = impl_->presentedFrames * 1000000000.0 / elapsed;
            titleChanged = true;
            impl_->presentedFrames = 0;
            impl_->fpsSampleStart = now;
        }
        if (titleChanged)
            impl_->updateTitle();
        return true;
    }
}
