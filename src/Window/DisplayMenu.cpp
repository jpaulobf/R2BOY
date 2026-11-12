#include "DisplayMenu.h"
#include "FullscreenMenu.h"
#include <imgui.h>
#include <utility>

namespace R2BOY::Window
{
    void drawDisplayMenu(DisplayMenuContext context)
    {
        if (ImGui::BeginMenu("Display"))
        {
            if (ImGui::MenuItem("VSync", nullptr, context.vsyncEnabled))
                context.toggleVSync();
            ImGui::Separator();
            for (int scale = 1; scale <= 5; ++scale)
            {
                char label[8];
                char shortcut[16];
                SDL_snprintf(label, sizeof(label), "%dX", scale);
                SDL_snprintf(shortcut, sizeof(shortcut), "Ctrl+%d", scale);
                if (ImGui::MenuItem(label, shortcut, context.display.scale == scale && context.display.mode == DisplayMode::Windowed))
                {
                    context.display.scale = scale;
                    context.display.mode = DisplayMode::Windowed;
                    context.display.pending = true;
                }
            }
            ImGui::Separator();
            R2Window::drawFullscreenResolutionMenu(context.display.resolution, context.nativeWindow, "Fullscreen Resolution");
            const auto fullscreenItem = [&context](const char *label, DisplayMode mode)
            {
                if (ImGui::MenuItem(label, mode == DisplayMode::Fullscreen ? "Ctrl+F ou Alt+Enter" : nullptr, context.display.mode == mode))
                {
                    context.display.mode = context.display.mode == mode ? DisplayMode::Windowed : mode;
                    context.display.pending = true;
                }
            };
            fullscreenItem("Fullscreen Stretched", DisplayMode::FullscreenStretched);
            fullscreenItem("Fullscreen", DisplayMode::Fullscreen);
            ImGui::Separator();
            ImGui::MenuItem("Bilinear Filter", nullptr, &context.bilinearFilter);
            ImGui::MenuItem("Scanlines", nullptr, &context.display.scanlines);
            if (ImGui::BeginMenu("Scanlines Level", context.display.scanlines))
            {
                for (int level = 5; level <= 25; level += 5)
                {
                    char label[8];
                    SDL_snprintf(label, sizeof(label), "%d%%", level);
                    if (ImGui::MenuItem(label, nullptr, context.display.scanlinesLevel == level))
                        context.display.scanlinesLevel = level;
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Render Tiles", nullptr, &context.renderTiles) && context.renderLayersCallback)
                context.renderLayersCallback(context.renderTiles, context.renderSprites, context.renderWindow);
            if (ImGui::MenuItem("Render Sprites", nullptr, &context.renderSprites) && context.renderLayersCallback)
                context.renderLayersCallback(context.renderTiles, context.renderSprites, context.renderWindow);
            if (ImGui::MenuItem("Render Window", nullptr, &context.renderWindow) && context.renderLayersCallback)
                context.renderLayersCallback(context.renderTiles, context.renderSprites, context.renderWindow);
            ImGui::Separator();
            if (ImGui::BeginMenu("Palette Preset"))
            {
                for (std::size_t preset = 0; preset < context.paletteCount; ++preset)
                    if (ImGui::MenuItem(context.palettes[preset].name, nullptr, context.display.palettePreset == preset))
                        context.display.palettePreset = preset;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Shaders"))
            {
                const std::pair<ShaderType, const char *> presets[] = {
                    {ShaderType::None, "None"}, {ShaderType::Gauss, "Gauss Scanlines"}, {ShaderType::HQ2, "HQ2"}, {ShaderType::CRT, "CRT"}, {ShaderType::CRT3D, "CRT 3D"}, {ShaderType::ScaleFX, "ScaleFX"}};
                for (const auto &[type, label] : presets)
                    if (ImGui::MenuItem(label, nullptr, context.display.shader == type))
                        context.display.shader = type;
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
    }
}