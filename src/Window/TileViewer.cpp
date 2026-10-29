#include "TileViewer.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <algorithm>
#include <array>
#include <iostream>

namespace R2BOY::Window
{
    namespace
    {
        constexpr int columns = 16, rows = 24, tileSize = 8;
        constexpr int imageWidth = columns * tileSize, imageHeight = rows * tileSize;

        struct ContextScope
        {
            ImGuiContext *previous = ImGui::GetCurrentContext();
            explicit ContextScope(ImGuiContext *context) { ImGui::SetCurrentContext(context); }
            ~ContextScope() { ImGui::SetCurrentContext(previous); }
        };
    }

    struct TileViewer::Impl
    {
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        SDL_Texture *texture = nullptr;
        ImGuiContext *context = nullptr;
        bool platformReady = false, rendererReady = false;
        int bank = 0, object = 0, palette = 0;
        Pixels pixels{};

        ~Impl()
        {
            if (context)
            {
                ContextScope scope(context);
                if (rendererReady)
                    ImGui_ImplSDLRenderer3_Shutdown();
                if (platformReady)
                    ImGui_ImplSDL3_Shutdown();
                ImGui::DestroyContext(context);
            }
            if (texture)
                SDL_DestroyTexture(texture);
            if (renderer)
                SDL_DestroyRenderer(renderer);
            if (window)
                SDL_DestroyWindow(window);
        }
    };

    TileViewer::TileViewer() = default;
    TileViewer::~TileViewer() = default;

    TileViewer::Pixels TileViewer::decode(const Common::TileSnapshot &snapshot, unsigned bank, unsigned object, unsigned palette)
    {
        Pixels pixels{};
        if (bank >= snapshot.banks.size() || object >= snapshot.palettes.size() ||
            palette >= snapshot.palettes[object].size())
            return pixels;
        const auto &data = snapshot.banks[bank];
        const auto &colors = snapshot.palettes[object][palette];
        for (int tile = 0; tile < static_cast<int>(Common::TileSnapshot::TileCount); ++tile)
            for (int y = 0; y < tileSize; ++y)
            {
                const auto low = data[tile * 16 + y * 2], high = data[tile * 16 + y * 2 + 1];
                for (int x = 0; x < tileSize; ++x)
                {
                    const unsigned bit = 7 - x;
                    const unsigned color = ((low >> bit) & 1) | (((high >> bit) & 1) << 1);
                    pixels[(tile / columns * tileSize + y) * imageWidth + tile % columns * tileSize + x] = colors[color];
                }
            }
        return pixels;
    }

    bool TileViewer::open()
    {
        if (impl_)
            return true;
        auto viewer = std::make_unique<Impl>();
        viewer->window = SDL_CreateWindow("Tile Viewer", 480, 760, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!viewer->window || !(viewer->renderer = SDL_CreateRenderer(viewer->window, nullptr)) ||
            !(viewer->texture = SDL_CreateTexture(viewer->renderer, SDL_PIXELFORMAT_ARGB8888,
                                                  SDL_TEXTUREACCESS_STREAMING, imageWidth, imageHeight)) ||
            !SDL_SetTextureScaleMode(viewer->texture, SDL_SCALEMODE_NEAREST))
        {
            std::cerr << "Tile Viewer: Unable to create window or renderer: " << SDL_GetError() << '\n';
            return false;
        }
        ImGuiContext *previous = ImGui::GetCurrentContext();
        viewer->context = ImGui::CreateContext();
        ImGui::SetCurrentContext(viewer->context);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        viewer->platformReady = ImGui_ImplSDL3_InitForSDLRenderer(viewer->window, viewer->renderer);
        if (viewer->platformReady)
            viewer->rendererReady = ImGui_ImplSDLRenderer3_Init(viewer->renderer);
        ImGui::SetCurrentContext(previous);
        if (!viewer->platformReady || !viewer->rendererReady)
        {
            std::cerr << "Tile Viewer: Unable to initialize interface.\n";
            return false;
        }
        impl_ = std::move(viewer);
        return true;
    }

    void TileViewer::close() { impl_.reset(); }
    bool TileViewer::isOpen() const { return static_cast<bool>(impl_); }

    void TileViewer::snapTo(SDL_Window *mainWindow)
    {
        if (!impl_ || !mainWindow)
            return;
        int x = 0, y = 0, width = 0;
        if (!SDL_GetWindowPosition(mainWindow, &x, &y) || !SDL_GetWindowSize(mainWindow, &width, nullptr))
            return;
        // SDL usa a origem da área cliente; as bordas ficam fora desse retângulo.
        int mainTop = 0, mainRight = 0, viewerTop = 0, viewerLeft = 0;
        SDL_GetWindowBordersSize(mainWindow, &mainTop, nullptr, nullptr, &mainRight);
        SDL_GetWindowBordersSize(impl_->window, &viewerTop, &viewerLeft, nullptr, nullptr);
        if (!SDL_SetWindowPosition(impl_->window, x + width + mainRight + viewerLeft + 1,
                                   y - mainTop + viewerTop))
            std::cerr << "Tile Viewer: Unable to position window: " << SDL_GetError() << '\n';
    }

    bool TileViewer::processEvent(const SDL_Event &event)
    {
        if (!impl_)
            return false;
        const auto id = SDL_GetWindowID(impl_->window);
        Uint32 eventWindow = 0;
        if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST)
            eventWindow = event.window.windowID;
        else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
            eventWindow = event.key.windowID;
        else if (event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING)
            eventWindow = event.text.windowID;
        else if (event.type == SDL_EVENT_MOUSE_MOTION)
            eventWindow = event.motion.windowID;
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
            eventWindow = event.button.windowID;
        else if (event.type == SDL_EVENT_MOUSE_WHEEL)
            eventWindow = event.wheel.windowID;
        if (eventWindow != id)
            return false;
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            close();
        else
        {
            ContextScope scope(impl_->context);
            ImGui_ImplSDL3_ProcessEvent(&event);
        }
        return true;
    }

    bool TileViewer::render(const SnapshotProvider &provider)
    {
        if (!impl_ || (SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_MINIMIZED))
            return true;
        ContextScope scope(impl_->context);
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const auto *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("Tiles", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        const auto snapshot = provider ? provider() : Common::TileSnapshot{};
        if (!snapshot.loaded)
            ImGui::TextUnformatted("Open a ROM to inspect its tiles.");
        else
        {
            if (!snapshot.color)
                impl_->bank = 0;
            ImGui::BeginDisabled(!snapshot.color);
            ImGui::SetNextItemWidth(100);
            ImGui::Combo("VRAM Bank", &impl_->bank, "Bank 0\0Bank 1\0");
            ImGui::EndDisabled();
            ImGui::SetNextItemWidth(150);
            ImGui::Combo("Palette Type", &impl_->object, "Background\0Sprites\0");
            const int paletteCount = snapshot.color ? 8 : impl_->object ? 2 : 1;
            impl_->palette = std::clamp(impl_->palette, 0, paletteCount - 1);
            ImGui::BeginDisabled(paletteCount == 1);
            ImGui::SetNextItemWidth(100);
            ImGui::SliderInt("Palette", &impl_->palette, 0, paletteCount - 1, "%d", ImGuiSliderFlags_AlwaysClamp);
            impl_->palette = std::clamp(impl_->palette, 0, paletteCount - 1);
            ImGui::EndDisabled();
            impl_->pixels = decode(snapshot, impl_->bank, impl_->object, impl_->palette);
            if (!SDL_UpdateTexture(impl_->texture, nullptr, impl_->pixels.data(), imageWidth * sizeof(std::uint32_t)))
            {
                std::cerr << "Tile Viewer: Unable to update texture: " << SDL_GetError() << '\n';
                ImGui::End();
                ImGui::EndFrame();
                return false;
            }
            ImGui::TextUnformatted("Tiles $8000-$97FF");
            const float scale = std::max(1.0f, std::min(3.0f, (ImGui::GetContentRegionAvail().x - 8.0f) / imageWidth));
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest);
            ImGui::Image(ImTextureRef(reinterpret_cast<ImTextureID>(impl_->texture)), ImVec2(imageWidth * scale, imageHeight * scale));
            ImGui::GetWindowDrawList()->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear);
            if (ImGui::IsItemHovered())
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                const int x = static_cast<int>((mouse.x - origin.x) / (tileSize * scale));
                const int y = static_cast<int>((mouse.y - origin.y) / (tileSize * scale));
                const int tile = y * columns + x;
                if (x >= 0 && x < columns && y >= 0 && y < rows)
                    ImGui::SetTooltip("Tile %d ($%03X)\nAddress: $%04X\nBank: %d", tile, tile, 0x8000 + tile * 16, impl_->bank);
            }
        }
        ImGui::End();
        ImGui::Render();
        const auto &io = ImGui::GetIO();
        if (!SDL_SetRenderScale(impl_->renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y) ||
            !SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255) || !SDL_RenderClear(impl_->renderer))
            return false;
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), impl_->renderer);
        return SDL_RenderPresent(impl_->renderer);
    }
}
