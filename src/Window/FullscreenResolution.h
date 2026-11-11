#pragma once
#include <SDL3/SDL.h>
#include <imgui.h>
#include <array>
#include <cmath>

class FullscreenResolution
{
    struct Size
    {
        int w, h;
        const char *label;
    };
    inline static constexpr std::array<Size, 12> sizes{{{640, 480, "640 x 480"}, {800, 600, "800 x 600"}, {832, 624, "832 x 624"}, {1024, 768, "1024 x 768"}, {1128, 634, "1128 x 634"}, {1280, 720, "1280 x 720"}, {1280, 960, "1280 x 960"}, {1366, 768, "1366 x 768"}, {1440, 900, "1440 x 900"}, {1600, 900, "1600 x 900"}, {1680, 1050, "1680 x 1050"}, {1920, 1080, "1920 x 1080"}}};
    int selected = -1;
    std::array<bool, 12> unavailable{};

    bool findMode(SDL_Window *window, int index, SDL_DisplayMode &result) const
    {
        int count = 0;
        const auto display = SDL_GetDisplayForWindow(window);
        const auto *desktop = SDL_GetDesktopDisplayMode(display);
        if (!desktop || desktop->refresh_rate <= 0)
            return false;
        auto modes = SDL_GetFullscreenDisplayModes(display, &count);
        bool found = false;
        for (int i = 0; i < count; ++i)
            if (modes[i]->w == sizes[index].w && modes[i]->h == sizes[index].h &&
                std::abs(modes[i]->refresh_rate - desktop->refresh_rate) < 0.01f)
            {
                result = *modes[i];
                found = true;
                break;
            }
        SDL_free(modes);
        return found;
    }

public:
    void menu(SDL_Window *window)
    {
        if (!ImGui::BeginMenu("Fullscreen Resolution"))
            return;
        if (ImGui::MenuItem("Desktop Resolution", nullptr, selected < 0))
            selected = -1;
        for (int i = 0; i < static_cast<int>(sizes.size()); ++i)
            if (ImGui::MenuItem(sizes[i].label, nullptr, selected == i, !unavailable[i]))
            {
                SDL_DisplayMode mode{};
                if (findMode(window, i, mode))
                    selected = i;
                else
                {
                    unavailable[i] = true;
                    selected = -1;
                }
            }
        ImGui::EndMenu();
    }
    bool enter(SDL_Window *window)
    {
        SDL_DisplayMode mode{};
        if (selected >= 0 && (!findMode(window, selected, mode) || !SDL_SetWindowFullscreenMode(window, &mode)))
        {
            unavailable[selected] = true;
            selected = -1;
        }
        if (selected < 0 && !SDL_SetWindowFullscreenMode(window, nullptr))
            return false;
        if (!SDL_SetWindowFullscreen(window, true))
        {
            if (selected < 0)
                return false;
            unavailable[selected] = true;
            selected = -1;
            if (!SDL_SetWindowFullscreenMode(window, nullptr) || !SDL_SetWindowFullscreen(window, true))
                return false;
        }
        return SDL_SyncWindow(window);
    }
};