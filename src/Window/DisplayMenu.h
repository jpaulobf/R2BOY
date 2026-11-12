#pragma once
#include "Window.h"
#include "FullscreenController.h"
#include "Common/Constants.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace R2BOY::Window
{
    struct PalettePreset
    {
        const char *name;
        std::array<std::uint32_t, 4> colors;
        const std::array<std::array<std::uint32_t, 4>, 4> *pixelColors = nullptr;
    };
    enum class DisplayMode
    {
        Windowed,
        FullscreenStretched,
        Fullscreen
    };
    enum class ShaderType
    {
        None,
        Gauss,
        HQ2,
        CRT,
        CRT3D,
        ScaleFX
    };
    struct DisplaySettings
    {
        FullscreenController resolution;
        int scale = 4;
        bool scanlines = false;
        int scanlinesLevel = 5;
        std::size_t palettePreset = 2;
        DisplayMode mode = DisplayMode::Windowed;
        bool pending = true;
        ShaderType shader = ShaderType::None;
        int imageWidth = Common::Constants::ScreenWidth * 4;
        int imageHeight = Common::Constants::ScreenHeight * 4;
    };
    struct DisplayMenuContext
    {
        SDL_Window *nativeWindow;
        DisplaySettings &display;
        bool &bilinearFilter;
        bool &renderTiles, &renderSprites, &renderWindow;
        const Window::RenderLayersCallback &renderLayersCallback;
        bool vsyncEnabled;
        const std::function<void()> &toggleVSync;
        const PalettePreset *palettes;
        std::size_t paletteCount;
    };
    void drawDisplayMenu(DisplayMenuContext context);
}