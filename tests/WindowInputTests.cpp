#include "Window/Window.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void capturesKeyboardOnlyForInterface()
    {
        R2BOY::Window::Window window;
        check(window.open(), "headless window must open");
        R2BOY::Common::Framebuffer framebuffer{};
        bool running = true;
        const auto frame = [&]()
        {
            check(window.renderFrame(running, false, framebuffer),
                  "headless frame must render");
        };
        auto &io = ImGui::GetIO();
        io.AddFocusEvent(true);
        frame();
        frame();
        frame();
        check(!io.WantCaptureKeyboard,
              "main game window must not capture keyboard merely because it is focused");

        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = SDLK_O;
        event.key.scancode = SDL_SCANCODE_O;
        event.key.mod = SDL_KMOD_CTRL;
        check(SDL_PushEvent(&event), "Ctrl+O event must enter queue");
        window.pollEvents(running);
        frame();
        frame();
        check(io.WantCaptureKeyboard, "ROM selector must still capture keyboard");

        // Fecha o modal e verifica que o foco devolvido à janela libera o teclado.
        ImGui::NewFrame();
        ImGui::Begin("R2BOY", nullptr, ImGuiWindowFlags_NoNavInputs);
        check(ImGui::BeginPopupModal("Open ROM"), "ROM selector must be open");
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        ImGui::End();
        ImGui::Render();
        frame();
        frame();
        check(!io.WantCaptureKeyboard, "closing the selector returns keyboard to the game");
    }

    void fileMenuRequestsRomActions()
    {
        using RomAction = R2BOY::Window::Window::RomAction;
        R2BOY::Window::Window window;
        check(window.open(), "file menu test window must open");
        R2BOY::Common::Framebuffer framebuffer{};
        bool running = true;
        bool gameLoaded = false;
        const auto frame = [&]()
        {
            window.pollEvents(running);
            check(window.renderFrame(running, gameLoaded, framebuffer), "file menu frame must render");
        };
        frame();
        frame();
        const auto select = [&](const char *label)
        {
            auto *main = ImGui::FindWindowByName("R2BOY");
            ImGui::ActivateItemByID(ImHashStr("File", 0, main->GetID("##MenuBar")));
            frame();
            frame();
            auto *menu = ImGui::FindWindowByName("File###Menu_00");
            check(menu && menu->Active, "File menu must open");
            ImGui::ActivateItemByID(menu->GetID(label));
            frame();
            ImGui::ClosePopupsOverWindow(main, false);
            frame();
        };
        select("Unload");
        check(window.takeRomAction() == RomAction::None, "Unload must be disabled without a game");
        select("Reset");
        check(window.takeRomAction() == RomAction::None, "Reset must be disabled without a game");
        gameLoaded = true;
        select("Reset");
        check(window.takeRomAction() == RomAction::Reset, "Reset must request a game reset");
        check(window.takeRomAction() == RomAction::None, "menu actions must be consumed only once");
        select("Unload");
        check(window.takeRomAction() == RomAction::Unload, "Unload must request cartridge removal");
        check(running, "ROM actions must not close the application");
        gameLoaded = false;
        select("Exit");
        check(!running, "Exit must still close the application without a game");
    }

    void displayMenuControlsWindow()
    {
        R2BOY::Window::Window window;
        check(window.open(), "display test window must open");
        auto *native = SDL_GetWindowFromID(static_cast<SDL_WindowID>(
            reinterpret_cast<intptr_t>(ImGui::GetMainViewport()->PlatformHandle)));
        check(native != nullptr, "SDL window must be available");
        R2BOY::Common::Framebuffer framebuffer{};
        bool running = true;
        const auto frame = [&]()
        {
            window.pollEvents(running);
            check(window.renderFrame(running, true, framebuffer), "display frame must render");
        };
        frame();
        frame();
        const auto checkScale = [&](int scale)
        {
            int width = 0;
            int height = 0;
            check(SDL_GetWindowSize(native, &width, &height), "window size must be available");
            auto *main = ImGui::FindWindowByName("R2BOY");
            check(main->MenuBarHeight > 0, "windowed mode must show the menu again");
            check(width == 160 * scale && height == 144 * scale + static_cast<int>(main->MenuBarHeight),
                  "scale must size the game area, excluding the menu");
            check((SDL_GetWindowFlags(native) & SDL_WINDOW_FULLSCREEN) == 0,
                  "selecting a scale must return to windowed mode");
        };
        const auto select = [&](const char *label)
        {
            auto *main = ImGui::FindWindowByName("R2BOY");
            ImGui::ActivateItemByID(ImHashStr("Display", 0, main->GetID("##MenuBar")));
            frame();
            frame();
            auto *menu = ImGui::FindWindowByName("Display###Menu_00");
            check(menu && menu->Active, "Display menu must open");
            ImGui::ActivateItemByID(menu->GetID(label));
            frame();
            frame();
            frame();
        };
        const auto escape = [&]()
        {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.windowID = SDL_GetWindowID(native);
            event.key.key = SDLK_ESCAPE;
            event.key.scancode = SDL_SCANCODE_ESCAPE;
            event.key.down = true;
            check(SDL_PushEvent(&event), "Escape key must enter queue");
            frame();
            event.type = SDL_EVENT_KEY_UP;
            event.key.down = false;
            check(SDL_PushEvent(&event), "Escape release must enter queue");
            frame();
            frame();
            check(running, "Escape must never close the emulator");
        };
        const auto checkFullscreen = [&](bool stretched)
        {
            check((SDL_GetWindowFlags(native) & SDL_WINDOW_FULLSCREEN) != 0,
                  "fullscreen menu must enable fullscreen");
            check(SDL_GetWindowFullscreenMode(native) == nullptr,
                  "fullscreen must use the desktop without an exclusive display mode");
            auto *main = ImGui::FindWindowByName("R2BOY");
            check(main->MenuBarHeight == 0, "fullscreen must not draw or reserve space for the menu");
            const auto &vertices = main->DrawList->VtxBuffer;
            check(vertices.Size >= 4, "game image must be drawn");
            const ImVec2 topLeft = vertices[vertices.Size - 4].pos;
            const ImVec2 bottomRight = vertices[vertices.Size - 2].pos;
            const float width = bottomRight.x - topLeft.x;
            const float height = bottomRight.y - topLeft.y;
            if (stretched)
                check(std::abs(width - main->Size.x) < 1 &&
                          std::abs(height - main->Size.y) < 1 &&
                          std::abs(topLeft.x - main->Pos.x) < 1 &&
                          std::abs(topLeft.y - main->Pos.y) < 1,
                      "stretched image must fill the entire fullscreen window");
            else
                check(std::abs(width * 144 - height * 160) < 1,
                      "proportional fullscreen must preserve the Game Boy aspect ratio");
        };

        checkScale(3);
        escape();
        checkScale(3);
        select("Borderless Fullscreen");
        checkFullscreen(false);
        escape();
        checkScale(3);
        for (int scale = 1; scale <= 4; ++scale)
        {
            const char *labels[] = {"1X", "2X", "3X", "4X"};
            select(labels[scale - 1]);
            checkScale(scale);
        }
        check(SDL_SetWindowSize(native, 720, 600), "manual resize must succeed");
        frame();
        frame();
        select("Borderless Fullscreen Stretched");
        checkFullscreen(true);
        escape();
        checkScale(4);
        select("Borderless Fullscreen");
        checkFullscreen(false);
        escape();
        checkScale(4);
        select("2X");
        select("Borderless Fullscreen Stretched");
        checkFullscreen(true);
        escape();
        checkScale(2);
        select("Borderless Fullscreen");
        escape();
        select("1X");
        checkScale(1);
    }
}

int main()
{
    try
    {
        capturesKeyboardOnlyForInterface();
        fileMenuRequestsRomActions();
        displayMenuControlsWindow();
        std::cout << "Window input and display tests passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
