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
        select("Save State");
        check(window.takeRomAction() == RomAction::None, "Save must be disabled without a game");
        select("Load State");
        check(window.takeRomAction() == RomAction::None, "Load must be disabled without a game");
        select("Save State Slot");
        auto *disabledSlots = ImGui::FindWindowByName("Save State Slot###Menu_01");
        check(!disabledSlots || !disabledSlots->Active, "Slot submenu must be disabled without a game");
        const auto key = [&](SDL_Keycode code, bool repeat = false)
        {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = code;
            event.key.repeat = repeat;
            check(SDL_PushEvent(&event), "shortcut must enter SDL queue");
            frame();
        };
        key(SDLK_F5);
        key(SDLK_F6);
        check(window.takeRomAction() == RomAction::None, "State shortcuts require a ROM");
        check(window.stateSlot() == 1, "Slot 1 must be selected initially");
        gameLoaded = true;
        select("Save State");
        check(window.takeRomAction() == RomAction::SaveState, "Save menu must request save");
        select("Load State");
        check(window.takeRomAction() == RomAction::LoadState, "Load menu must request load");
        key(SDLK_F5);
        check(window.takeRomAction() == RomAction::SaveState, "F5 must request save");
        key(SDLK_F6);
        check(window.takeRomAction() == RomAction::LoadState, "F6 must request load");
        key(SDLK_F5, true);
        key(SDLK_F6, true);
        check(window.takeRomAction() == RomAction::None, "Repeated shortcut keys must be ignored");
        // Seleciona cada slot pelo submenu real do ImGui.
        for (int slot : {2, 3, 1})
        {
            auto *main = ImGui::FindWindowByName("R2BOY");
            ImGui::ActivateItemByID(ImHashStr("File", 0, main->GetID("##MenuBar")));
            frame();
            frame();
            auto *menu = ImGui::FindWindowByName("File###Menu_00");
            check(menu && menu->Active, "File menu must open for slots");
            ImGui::ActivateItemByID(menu->GetID("Save State Slot"));
            frame();
            frame();
            auto *submenu = ImGui::FindWindowByName("Save State Slot###Menu_01");
            check(submenu && submenu->Active, "Slot submenu must open");
            const auto label = "Slot " + std::to_string(slot);
            ImGui::ActivateItemByID(submenu->GetID(label.c_str()));
            frame();
            check(window.stateSlot() == slot, "Slot submenu must update selected slot");
            check(window.takeRomAction() == RomAction::None, "Selecting a slot must not save or load");
            ImGui::ClosePopupsOverWindow(main, false);
            frame();
        }
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
        framebuffer.fill(0xFFFFFFFFu);
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

        const auto checkScanlines = [&](Uint8 brightness)
        {
            auto *main = ImGui::FindWindowByName("R2BOY");
            ImGui::ClosePopupsOverWindow(main, false);
            frame();
            frame();
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
                SDL_RenderReadPixels(SDL_GetRenderer(native), nullptr), SDL_DestroySurface);
            check(pixels != nullptr, "renderer pixels must be readable");
            const int top = static_cast<int>(main->MenuBarHeight);
            Uint8 r, g, b, alpha;
            check(SDL_ReadSurfacePixel(pixels.get(), 80, top + 10, &r, &g, &b, &alpha),
                  "unfiltered line must be readable");
            check(r == 255 && g == 255 && b == 255, "even rows must retain their original color");
            check(SDL_ReadSurfacePixel(pixels.get(), 80, top + 14, &r, &g, &b, &alpha),
                  "scanline must be readable");
            check(r == brightness && g == brightness && b == brightness,
                  "scanline brightness must match the selected level");
            check(framebuffer[160 * 3 + 20] == 0xFFFFFFFFu, "scanlines must preserve the Core framebuffer");
        };
        int vsyncChanges = 0;
        bool engineVSync = window.isVSyncEnabled();
        window.setVSyncCallback([&](bool enabled)
                               {
            ++vsyncChanges;
            engineVSync = enabled; });
        check(window.isVSyncEnabled(), "VSync must be enabled by default");
        check(vsyncChanges == 0, "registering the callback must not toggle VSync");
        int renderVSync = 0;
        check(SDL_GetRenderVSync(SDL_GetRenderer(native), &renderVSync), "renderer VSync must be readable");
        check(renderVSync == 1, "SDL synchronization must be enabled at startup");
        select("VSync");
        check(SDL_GetRenderVSync(SDL_GetRenderer(native), &renderVSync), "disabled VSync must be readable");
        check(!window.isVSyncEnabled() && !engineVSync && renderVSync == 0 && vsyncChanges == 1,
              "Display VSync must disable SDL synchronization and notify the Engine once");
        select("VSync");
        check(SDL_GetRenderVSync(SDL_GetRenderer(native), &renderVSync), "renderer VSync must be readable");
        check(window.isVSyncEnabled() && engineVSync && renderVSync == 1 && vsyncChanges == 2,
              "Display VSync must enable SDL synchronization and notify the Engine once");
        const auto checkRendererVSync = [&](int expected)
        {
            int actual = -1;
            check(SDL_GetRenderVSync(SDL_GetRenderer(native), &actual), "VSync must be readable");
            check(actual == expected, "renderer must respect suppression and the menu preference");
        };
        check(window.setVSyncSuppressed(true), "fast-forward must suspend VSync");
        check(window.isVSyncEnabled() && vsyncChanges == 2,
              "suppression must preserve the menu preference without notifying a preference change");
        checkRendererVSync(0);
        select("VSync");
        check(!window.isVSyncEnabled(), "VSync preference can be disabled during fast-forward");
        checkRendererVSync(0);
        select("VSync");
        check(window.isVSyncEnabled(), "VSync preference can be enabled during fast-forward");
        checkRendererVSync(0);
        check(window.setVSyncSuppressed(false), "releasing TAB must restore VSync");
        checkRendererVSync(1);
        select("VSync");
        check(window.setVSyncSuppressed(true) && window.setVSyncSuppressed(false),
              "fast-forward must also work with VSync disabled");
        checkRendererVSync(0);
        select("VSync");

        int fastForwardChanges = 0;
        bool fastForwardEnabled = window.isFastForwardEnabled();
        check(fastForwardEnabled, "fast-forward must be enabled by default");
        window.setFastForwardCallback([&](bool enabled)
                                      {
            fastForwardEnabled = enabled;
            ++fastForwardChanges; });
        const auto selectFastForward = [&]()
        {
            auto *main = ImGui::FindWindowByName("R2BOY");
            ImGui::ClosePopupsOverWindow(main, false);
            frame();
            ImGui::ActivateItemByID(ImHashStr("Hacks", 0, main->GetID("##MenuBar")));
            frame();
            frame();
            auto *menu = ImGui::FindWindowByName("Hacks###Menu_00");
            check(menu && menu->Active, "Hacks menu must open");
            ImGui::ActivateItemByID(menu->GetID("Enable Fast Forward"));
            frame();
            ImGui::ClosePopupsOverWindow(main, false);
            frame();
        };
        selectFastForward();
        check(!window.isFastForwardEnabled() && !fastForwardEnabled && fastForwardChanges == 1,
              "Hacks must disable fast-forward and notify the Engine");
        selectFastForward();
        check(window.isFastForwardEnabled() && fastForwardEnabled && fastForwardChanges == 2,
              "Hacks must reenable fast-forward and notify the Engine");
        checkScanlines(255);
        select("Scanlines");
        checkScanlines(242); // O efeito inicia com intensidade de 5%.
        const char *levels[] = {"5%", "10%", "15%", "20%", "25%"};
        const Uint8 brightness[] = {242, 230, 217, 204, 191};
        for (int level = 0; level < 5; ++level)
        {
            select("Scanlines Level");
            auto *submenu = ImGui::FindWindowByName("Scanlines Level###Menu_01");
            check(submenu && submenu->Active, "scanlines level submenu must open");
            ImGui::ActivateItemByID(submenu->GetID(levels[level]));
            frame();
            checkScanlines(brightness[level]);
        }
        select("Scanlines");
        checkScanlines(255);

        checkScale(4);
        escape();
        checkScale(4);
        select("Borderless Fullscreen");
        checkFullscreen(false);
        escape();
        checkScale(4);
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
