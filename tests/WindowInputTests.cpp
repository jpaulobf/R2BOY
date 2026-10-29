#include "Window/Window.h"
#include "Window/ConfigManager.h"
#include "Window/TileViewer.h"
#include <chrono>
#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <cmath>
#include <iostream>
#include <fstream>
#include <stdexcept>

namespace
{
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void recentFilesUseRomLoadingFlow()
    {
        const auto configPath = std::filesystem::temp_directory_path() /
            ("r2boy-recent-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini");
        {
            R2BOY::Window::Window window;
            check(window.open(), "recent files test window must open");
            R2BOY::Common::Framebuffer framebuffer{};
            bool running = true;
            check(window.renderFrame(running, false, framebuffer), "initial frame must render");
            R2BOY::Window::ConfigManager config(configPath);
            R2BOY::Window::FileManager files(config);
            int loads = 0;
            files.setRomOpenHandler([&](const auto &path, const auto &entry)
            {
                check(path.is_absolute(), "recent loads must use an absolute path");
                check(!entry, "plain ROM must not supply a ZIP entry");
                if (path.filename() == "missing.gb")
                    throw std::runtime_error("Missing ROM");
                ++loads;
            });
            const auto frame = [&]()
            {
                ImGui::NewFrame();
                ImGui::Begin("Recent test");
                files.draw();
                ImGui::End();
                ImGui::Render();
            };
            const auto first = std::filesystem::absolute("first.gb").lexically_normal();
            files.requestOpen(first);
            frame();
            frame();
            check(loads == 1 && config.getRecentRoms().front() == first.u8string(),
                  "successful recent load must update history");
            check(config.getLastRomPath() == first.parent_path().u8string(),
                  "successful recent load must remember its directory");
            files.requestOpen(std::filesystem::absolute("second.gbc"));
            frame();
            frame();
            files.requestOpen(first);
            frame();
            frame();
            check(loads == 3 && config.getRecentRoms().size() == 2 && config.getRecentRoms().front() == first.u8string(),
                  "reopening a recent must reorder without duplicating");
            files.requestOpen(std::filesystem::absolute("missing.gb"));
            frame();
            frame();
            check(loads == 3 && config.getRecentRoms().size() == 2,
                  "failed recent load must preserve history");
            check(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup), "failed recent load must display selector with its error");
        }
        std::filesystem::remove(configPath);
    }

    void unsupportedNativeFileNames()
    {
#ifdef _WIN32
        const auto directory = std::filesystem::temp_directory_path() /
            ("r2boy-filename-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        const auto invalid = std::filesystem::path(std::wstring(1, wchar_t(0xD800)));
        const auto invalidRom = directory / std::filesystem::path(invalid.native() + L".gb");
        const auto invalidExtension = directory / std::filesystem::path(L"file." + invalid.native());
        const auto invalidDirectory = directory / invalid;
        const auto valid = directory / "valid.gb";
        std::filesystem::create_directory(invalidDirectory);
        for (const auto &path : {invalidRom, invalidExtension, valid})
        {
            std::ofstream out(path);
            out << "test";
            check(bool(out), "native filename fixture must be created");
        }
        {
            R2BOY::Window::Window window;
            check(window.open(), "filename test window must open");
            R2BOY::Common::Framebuffer framebuffer{};
            bool running = true;
            check(window.renderFrame(running, false, framebuffer), "filename test frame must render");
            R2BOY::Window::ConfigManager config(directory / "config.ini");
            config.setLastRomPath(directory.u8string());
            R2BOY::Window::FileManager files(config);
            unsigned loads = 0;
            files.setRomOpenHandler([&](const auto &path, const auto &)
            {
                check(path == valid, "valid filename must retain its native path");
                ++loads;
            });
            const auto frame = [&]()
            {
                ImGui::NewFrame();
                ImGui::Begin("Filename test");
                files.draw();
                ImGui::End();
                ImGui::Render();
            };
            files.requestOpen();
            frame();
            frame();
            check(loads == 0, "listing unsupported names must not load a ROM");
            files.requestOpen(valid);
            frame();
            frame();
            check(loads == 1 && config.getRecentRoms().front() == valid.u8string(),
                  "valid ROMs must remain accessible alongside unsupported native names");
        }
        check(std::filesystem::equivalent(directory.parent_path(), std::filesystem::temp_directory_path()),
              "filename fixture cleanup must stay in the temporary directory");
        std::filesystem::remove_all(directory);
#endif
    }

    void tileViewerLifecycle()
    {
        R2BOY::Common::TileSnapshot sample;
        sample.banks[1][0] = 0xA0;
        sample.banks[1][1] = 0x60;
        sample.banks[1][16 * 383 + 14] = 0x01;
        sample.palettes[1][3] = {0xFF000000, 0xFF111111, 0xFF222222, 0xFF333333};
        const auto pixels = R2BOY::Window::TileViewer::decode(sample, 1, 1, 3);
        check(pixels[0] == 0xFF111111 && pixels[1] == 0xFF222222 && pixels[2] == 0xFF333333 &&
              pixels[3] == 0xFF000000 && pixels.back() == 0xFF111111,
              "tiles must decode both bit planes, bank, palette and final tile in grid order");
        R2BOY::Window::Window window;
        check(window.open(), "tile viewer test window must open");
        R2BOY::Common::Framebuffer framebuffer{};
        bool running = true;
        unsigned snapshots = 0;
        window.setTileSnapshotProvider([&]()
        {
            ++snapshots;
            R2BOY::Common::TileSnapshot data;
            data.loaded = true;
            data.banks[0][0] = 0x80;
            data.palettes[0][0] = {0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000};
            return data;
        });
        const auto frame = [&]()
        {
            window.pollEvents(running);
            check(window.renderFrame(running, true, framebuffer), "tile viewer frame must render");
        };
        const auto findViewer = []() -> SDL_Window *
        {
            int count = 0;
            SDL_Window **windows = SDL_GetWindows(&count);
            SDL_Window *found = nullptr;
            for (int i = 0; i < count; ++i)
                if (std::string(SDL_GetWindowTitle(windows[i])) == "Tile Viewer")
                    found = windows[i];
            SDL_free(windows);
            return found;
        };
        const auto toggle = [&]()
        {
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
            check(main != nullptr, "main window must exist");
            ImGui::ActivateItemByID(ImHashStr("Debug", 0, main->GetID("##MenuBar")));
            frame();
            frame();
            auto *menu = ImGui::FindWindowByName("Debug###Menu_00");
            check(menu && menu->Active, "Debug menu must open");
            ImGui::ActivateItemByID(menu->GetID("Tile Viewer"));
            frame();
            frame();
        };
        frame();
        frame();
        check(!findViewer() && snapshots == 0, "viewer starts closed without requesting snapshots");
        toggle();
        check(findViewer() && snapshots > 0, "menu must open an updating native viewer");
        int count = 0;
        SDL_Window **windows = SDL_GetWindows(&count);
        SDL_Window *mainWindow = nullptr;
        for (int i = 0; i < count; ++i)
            if (windows[i] != findViewer())
                mainWindow = windows[i];
        SDL_free(windows);
        check(mainWindow != nullptr, "main SDL window must exist");
        const auto checkSnap = [&]()
        {
            int mainX = 0, mainY = 0, mainWidth = 0, viewerX = 0, viewerY = 0;
            int mainTop = 0, mainRight = 0, viewerTop = 0, viewerLeft = 0;
            check(SDL_GetWindowPosition(mainWindow, &mainX, &mainY) && SDL_GetWindowSize(mainWindow, &mainWidth, nullptr) &&
                  SDL_GetWindowPosition(findViewer(), &viewerX, &viewerY), "window positions must be readable");
            SDL_GetWindowBordersSize(mainWindow, &mainTop, nullptr, nullptr, &mainRight);
            SDL_GetWindowBordersSize(findViewer(), &viewerTop, &viewerLeft, nullptr, nullptr);
            check(viewerX - viewerLeft == mainX + mainWidth + mainRight + 1 &&
                  viewerY - viewerTop == mainY - mainTop,
                  "viewer must align outer top edges with one pixel after main window");
        };
        checkSnap();
        int mainX = 0, mainY = 0;
        check(SDL_GetWindowPosition(mainWindow, &mainX, &mainY), "main position must be readable");
        check(SDL_SetWindowPosition(mainWindow, mainX + 37, mainY + 29) && SDL_SyncWindow(mainWindow),
              "main window must move");
        frame();
        frame();
        checkSnap();
        check(SDL_SetWindowSize(mainWindow, 800, 730) && SDL_SyncWindow(mainWindow), "main window must resize");
        frame();
        frame();
        checkSnap();
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.windowID = SDL_GetWindowID(findViewer());
        key.key.key = SDLK_F5;
        check(SDL_PushEvent(&key), "viewer key event must enter queue");
        frame();
        check(window.takeRomAction() == R2BOY::Window::Window::RomAction::None,
              "viewer keyboard input must not trigger emulator shortcuts");
        toggle();
        check(!findViewer(), "selecting the checked menu item must close viewer");
        const auto closedCount = snapshots;
        frame();
        check(snapshots == closedCount, "closed viewer must stop requesting snapshots");
        toggle();
        auto *viewer = findViewer();
        check(viewer != nullptr, "viewer must reopen after menu close");
        SDL_Event close{};
        close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        close.window.windowID = SDL_GetWindowID(viewer);
        check(SDL_PushEvent(&close), "viewer close event must enter queue");
        frame();
        check(running && !findViewer(), "viewer close must not exit the emulator");
        toggle();
        check(findViewer(), "closing with X must uncheck menu so next click reopens viewer");
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
        check(!window.isMenuOpen(), "idle game window must not request menu pause");

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
        check(window.isMenuOpen(), "ROM selector must request temporary emulation pause");

        // Fecha o modal e verifica que o foco devolvido à janela libera o teclado.
        ImGui::NewFrame();
        ImGui::Begin("R2BOY v0.2.6", nullptr, ImGuiWindowFlags_NoNavInputs);
        check(ImGui::BeginPopupModal("Open ROM"), "ROM selector must be open");
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        ImGui::End();
        ImGui::Render();
        frame();
        frame();
        check(!io.WantCaptureKeyboard, "closing the selector returns keyboard to the game");
        check(!window.isMenuOpen(), "closing the ROM selector must release its pause request");
    }

    void menusRequestTemporaryPause()
    {
        R2BOY::Window::Window window;
        check(window.open(), "menu pause test window must open");
        R2BOY::Common::Framebuffer framebuffer{};
        bool running = true;
        const auto frame = [&]()
        {
            window.pollEvents(running);
            check(window.renderFrame(running, true, framebuffer), "menu pause frame must render");
        };
        frame();
        frame();
        check(!window.isMenuOpen(), "visible menu bar alone must not pause emulation");
        const auto escape = [&]()
        {
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
            frame();
        };
        for (const auto *label : {"File", "Display", "Sound", "Input", "Debug", "Hacks"})
        {
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
            ImGui::ActivateItemByID(ImHashStr(label, 0, main->GetID("##MenuBar")));
            frame();
            frame();
            check(window.isMenuOpen(), "every open menu must request emulation pause");
            check(!window.acceptsGameInput(), "open menu must suppress game input");
            if (std::string(label) == "Display")
            {
                auto *menu = ImGui::FindWindowByName("Display###Menu_00");
                check(menu && menu->Active, "Display must open for submenu pause test");
                ImGui::ActivateItemByID(menu->GetID("Palette Preset"));
                frame();
                frame();
                auto *submenu = ImGui::FindWindowByName("Palette Preset###Menu_01");
                check(submenu && submenu->Active && window.isMenuOpen(), "submenu must keep emulation paused");
                escape();
                check(window.isMenuOpen(), "closing a submenu must retain the parent menu pause");
                ImGui::ActivateItemByID(menu->GetID("Bilinear Filter"));
                frame();
                frame();
                check(!window.isMenuOpen(), "selecting a menu item must release menu pause when it closes");
            }
            else
            {
                escape();
                check(!window.isMenuOpen(), "Escape must release menu pause when the last menu closes");
            }
        }
    }

    void rewindMenuControlsHistory()
    {
        R2BOY::Window::Window window;
        check(window.open(), "rewind menu window must open");
        R2BOY::Common::Framebuffer framebuffer{};
        bool running = true;
        const auto frame = [&]()
        {
            window.pollEvents(running);
            check(window.renderFrame(running, false, framebuffer), "rewind menu renders without a ROM");
        };
        frame();
        frame();
        check(!window.isRewindEnabled() && window.rewindFrameInterval() == 5,
              "rewind defaults to disabled with Normal granularity");
        const auto openHacks = [&]()
        {
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
            check(main != nullptr, "main window exists");
            ImGui::ActivateItemByID(ImHashStr("Hacks", 0, main->GetID("##MenuBar")));
            frame();
            frame();
            auto *menu = ImGui::FindWindowByName("Hacks###Menu_00");
            check(menu && menu->Active, "Hacks menu opens");
            return menu;
        };
        const auto closeMenus = [&]()
        {
            ImGui::ClosePopupsOverWindow(ImGui::FindWindowByName("R2BOY v0.2.6"), false);
            frame();
        };
        auto *menu = openHacks();
        ImGui::ActivateItemByID(menu->GetID("Rewind Level"));
        frame();
        auto *levels = ImGui::FindWindowByName("Rewind Level###Menu_01");
        check(!levels || !levels->Active, "Rewind Level is disabled until rewind is enabled");
        closeMenus();
        menu = openHacks();
        ImGui::ActivateItemByID(menu->GetID("Enable Rewind"));
        frame();
        check(window.isRewindEnabled(), "Enable Rewind toggles on");
        closeMenus();
        const char *labels[] = {"Light", "Normal", "Precise"};
        const unsigned intervals[] = {10, 5, 1};
        for (unsigned i = 0; i < 3; ++i)
        {
            menu = openHacks();
            ImGui::ActivateItemByID(menu->GetID("Rewind Level"));
            frame();
            frame();
            levels = ImGui::FindWindowByName("Rewind Level###Menu_01");
            check(levels && levels->Active, "Rewind Level opens when enabled");
            ImGui::ActivateItemByID(levels->GetID(labels[i]));
            frame();
            check(window.rewindFrameInterval() == intervals[i], "level selects its frame interval");
            closeMenus();
        }
        menu = openHacks();
        ImGui::ActivateItemByID(menu->GetID("Enable Rewind"));
        frame();
        check(!window.isRewindEnabled() && window.rewindFrameInterval() == 1,
              "disabling rewind preserves the selected level");
        closeMenus();
    }

    void displayMenuControlsRenderLayers()
    {
        R2BOY::Window::Window window;
        check(window.open(), "render layers window must open");
        R2BOY::Common::Framebuffer framebuffer{};
        bool running = true, tiles = true, sprites = true, windowLayer = true;
        unsigned changes = 0;
        window.setRenderLayersCallback([&](bool renderTiles, bool renderSprites, bool renderWindow)
        {
            tiles = renderTiles;
            sprites = renderSprites;
            windowLayer = renderWindow;
            ++changes;
        });
        const auto frame = [&]()
        {
            window.pollEvents(running);
            check(window.renderFrame(running, false, framebuffer), "render layers menu works without ROM");
        };
        frame();
        frame();
        check(window.isRenderTilesEnabled() && window.isRenderSpritesEnabled() && window.isRenderWindowEnabled(),
              "all three layers must start enabled");
        const auto select = [&](const char *label)
        {
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
            ImGui::ActivateItemByID(ImHashStr("Display", 0, main->GetID("##MenuBar")));
            frame();
            frame();
            auto *menu = ImGui::FindWindowByName("Display###Menu_00");
            check(menu && menu->Active, "Display menu must open");
            ImGui::ActivateItemByID(menu->GetID(label));
            frame();
            ImGui::ClosePopupsOverWindow(main, false);
            frame();
            check(window.isRenderTilesEnabled() == tiles && window.isRenderSpritesEnabled() == sprites &&
                  window.isRenderWindowEnabled() == windowLayer,
                  "layer checkmarks must match callback values");
        };
        select("Render Tiles");
        check(!tiles && sprites && changes == 1, "Render Tiles must toggle independently");
        select("Render Sprites");
        check(!tiles && !sprites && changes == 2, "both layers can be disabled");
        select("Render Tiles");
        select("Render Sprites");
        check(tiles && sprites && changes == 4, "both layers can be reenabled");
        select("Render Window");
        check(tiles && sprites && !windowLayer && changes == 5, "window must toggle independently");
        select("Render Window");
        check(tiles && sprites && windowLayer && changes == 6, "window can be reenabled");
    }

    void soundMenuControlsOutput()
    {
        R2BOY::Window::Window window;
        check(window.open(), "sound menu test window must open");
        R2BOY::Common::Framebuffer framebuffer{};
        bool running = true;
        bool master = true;
        std::uint8_t channels = 15;
        unsigned changes = 0;
        window.setSoundCallback([&](bool enabled, std::uint8_t mask)
                                { master = enabled; channels = mask; ++changes; });
        const auto frame = [&]()
        {
            window.pollEvents(running);
            check(window.renderFrame(running, false, framebuffer), "sound menu works without a ROM");
        };
        frame();
        frame();
        check(window.isMasterSoundEnabled() && window.soundChannels() == 15, "sound starts fully enabled");
        const auto select = [&](const char *label)
        {
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
            ImGui::ActivateItemByID(ImHashStr("Sound", 0, main->GetID("##MenuBar")));
            frame();
            frame();
            auto *menu = ImGui::FindWindowByName("Sound###Menu_00");
            check(menu && menu->Active, "Sound menu must open");
            ImGui::ActivateItemByID(menu->GetID(label));
            frame();
            ImGui::ClosePopupsOverWindow(main, false);
            frame();
            check(window.isMasterSoundEnabled() == master && window.soundChannels() == channels,
                  "sound checkmarks match the notified settings");
        };
        select("Master Sound");
        check(!master && channels == 15, "master mute preserves individual selections");
        unsigned index = 0;
        for (const auto *label : {"Pulse 1", "Pulse 2", "Wave", "Noise"})
        {
            select(label);
            check(!master && channels == (15u & ~((1u << ++index) - 1)), "channels can be toggled independently while master is off");
        }
        select("Master Sound");
        check(master && channels == 0, "enabling master preserves muted channels");
        for (const auto *label : {"Pulse 1", "Pulse 2", "Wave", "Noise"})
            select(label);
        check(master && channels == 15 && changes == 10, "all sound options toggle back on");
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
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
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
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
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

    void windowSizeShortcuts()
    {
        R2BOY::Window::Window window;
        check(window.open(), "resize shortcut window must open");
        auto *native = SDL_GetWindowFromID(static_cast<SDL_WindowID>(
            reinterpret_cast<intptr_t>(ImGui::GetMainViewport()->PlatformHandle)));
        check(native != nullptr, "resize shortcut SDL window must be available");
        R2BOY::Common::Framebuffer framebuffer{};
        bool running = true;
        const auto frame = [&]()
        {
            window.pollEvents(running);
            check(window.renderFrame(running, false, framebuffer), "resize shortcut frame must render");
        };
        frame();
        frame();
        const auto select = [&](const char *label)
        {
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
            ImGui::ActivateItemByID(ImHashStr("Display", 0, main->GetID("##MenuBar")));
            frame();
            frame();
            auto *menu = ImGui::FindWindowByName("Display###Menu_00");
            check(menu && menu->Active, "Display menu must open for shortcut test");
            ImGui::ActivateItemByID(menu->GetID(label));
            frame();
            frame();
            frame();
            check((SDL_GetWindowFlags(native) & SDL_WINDOW_FULLSCREEN) != 0,
                  "shortcut test must enter fullscreen");
        };
        const auto checkScale = [&](int scale)
        {
            int width = 0, height = 0;
            check(SDL_GetWindowSize(native, &width, &height), "shortcut window size must be available");
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
            check(width == 160 * scale && height == 144 * scale + static_cast<int>(main->MenuBarHeight),
                  "Ctrl+1 through Ctrl+5 must size the game area, excluding the menu");
            check((SDL_GetWindowFlags(native) & SDL_WINDOW_FULLSCREEN) == 0 && main->MenuBarHeight > 0,
                  "resize shortcut must return to windowed mode with the menu");
        };
        const auto resizeShortcut = [&](SDL_Keycode key, SDL_Keymod modifiers, bool repeat = false)
        {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.windowID = SDL_GetWindowID(native);
            event.key.key = key;
            event.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
            event.key.mod = modifiers;
            event.key.down = true;
            event.key.repeat = repeat;
            check(SDL_PushEvent(&event), "resize shortcut must enter queue");
            frame();
            event.type = SDL_EVENT_KEY_UP;
            event.key.down = false;
            event.key.repeat = false;
            event.key.mod = SDL_KMOD_NONE;
            check(SDL_PushEvent(&event), "resize shortcut release must enter queue");
            frame();
            frame();
        };
        for (int scale = 1; scale <= 5; ++scale)
        {
            resizeShortcut(SDLK_1 + scale - 1, scale % 2 ? SDL_KMOD_LCTRL : SDL_KMOD_RCTRL);
            checkScale(scale);
        }
        resizeShortcut(SDLK_1, SDL_KMOD_NONE);
        checkScale(5);
        resizeShortcut(SDLK_2, SDL_KMOD_CTRL | SDL_KMOD_ALT);
        checkScale(5);
        resizeShortcut(SDLK_3, SDL_KMOD_CTRL, true);
        checkScale(5);
        resizeShortcut(SDLK_6, SDL_KMOD_CTRL);
        checkScale(5);
        select("Borderless Fullscreen");
        resizeShortcut(SDLK_2, SDL_KMOD_CTRL);
        checkScale(2);
        select("Borderless Fullscreen Stretched");
        resizeShortcut(SDLK_1, SDL_KMOD_CTRL);
        checkScale(1);
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
        bool dmgMode = true;
        const auto frame = [&]()
        {
            window.pollEvents(running);
            check(window.renderFrame(running, true, framebuffer, dmgMode), "display frame must render");
        };
        frame();
        frame();
        const auto checkScale = [&](int scale)
        {
            int width = 0;
            int height = 0;
            check(SDL_GetWindowSize(native, &width, &height), "window size must be available");
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
            check(main->MenuBarHeight > 0, "windowed mode must show the menu again");
            check(width == 160 * scale && height == 144 * scale + static_cast<int>(main->MenuBarHeight),
                  "scale must size the game area, excluding the menu");
            check((SDL_GetWindowFlags(native) & SDL_WINDOW_FULLSCREEN) == 0,
                  "selecting a scale must return to windowed mode");
        };
        const auto select = [&](const char *label)
        {
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
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
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
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
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
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
        const auto selectHack = [&](const char *label)
        {
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
            ImGui::ClosePopupsOverWindow(main, false);
            frame();
            ImGui::ActivateItemByID(ImHashStr("Hacks", 0, main->GetID("##MenuBar")));
            frame();
            frame();
            auto *menu = ImGui::FindWindowByName("Hacks###Menu_00");
            check(menu && menu->Active, "Hacks menu must open");
            ImGui::ActivateItemByID(menu->GetID(label));
            frame();
            ImGui::ClosePopupsOverWindow(main, false);
            frame();
        };
        selectHack("Enable Fast Forward");
        check(!window.isFastForwardEnabled() && !fastForwardEnabled && fastForwardChanges == 1,
              "Hacks must disable fast-forward and notify the Engine");
        selectHack("Enable Fast Forward");
        check(window.isFastForwardEnabled() && fastForwardEnabled && fastForwardChanges == 2,
              "Hacks must reenable fast-forward and notify the Engine");
        bool unlimitedSprites = window.isUnlimitedSpritesEnabled();
        int unlimitedChanges = 0;
        check(unlimitedSprites, "unlimited sprites must be enabled by default");
        window.setUnlimitedSpritesCallback([&](bool enabled)
                                           {
            unlimitedSprites = enabled;
            ++unlimitedChanges; });
        selectHack("Enable Unlimited Sprites");
        check(!window.isUnlimitedSpritesEnabled() && !unlimitedSprites && unlimitedChanges == 1,
              "Hacks must disable unlimited sprites and notify the Engine once");
        selectHack("Enable Unlimited Sprites");
        check(window.isUnlimitedSpritesEnabled() && unlimitedSprites && unlimitedChanges == 2,
              "Hacks must reenable unlimited sprites and notify the Engine once");
        int overclockChanges = 0;
        bool overclockEnabled = window.isCPUOverclockEnabled();
        check(!overclockEnabled, "CPU overclock must be disabled by default");
        window.setCPUOverclockCallback([&](bool enabled)
                                       {
            overclockEnabled = enabled;
            ++overclockChanges; });
        const std::string warning = " | CPU OVERCLOCK ENABLED! GAMES CAN CRASH!";
        const auto checkTitle = [&](bool enabled)
        {
            const std::string title = SDL_GetWindowTitle(native);
            const auto position = title.find(warning);
            check(enabled ? position != std::string::npos && position + warning.size() == title.size()
                          : position == std::string::npos,
                  "title must immediately append or remove the complete overclock warning");
            if (enabled)
                check(title.find("R2BOY v0.2.6 | FPS: ") == 0,
                      "overclock warning must appear after the FPS value");
        };
        checkTitle(false);
        selectHack("Enable CPU Overclock");
        check(window.isCPUOverclockEnabled() && overclockEnabled && overclockChanges == 1,
              "Hacks must enable CPU overclock and notify the Engine once");
        checkTitle(true);
        check(window.cpuOverclockPercent() == 200, "overclock slider must default to 2x");
        int multiplierChanges = 0;
        int selectedPercent = 200;
        window.setCPUOverclockPercentCallback([&](int percent)
                                              {
            selectedPercent = percent;
            ++multiplierChanges; });
        const auto clickMultiplier = [&](float fraction)
        {
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
            ImGui::ClosePopupsOverWindow(main, false);
            frame();
            ImGui::ActivateItemByID(ImHashStr("Hacks", 0, main->GetID("##MenuBar")));
            frame();
            frame();
            auto *menu = ImGui::FindWindowByName("Hacks###Menu_00");
            check(menu && menu->Active, "Hacks menu must open for multiplier selection");
            auto &io = ImGui::GetIO();
            const auto &style = ImGui::GetStyle();
            const float rowHeight = ImGui::GetFontSize() + style.ItemSpacing.y;
            io.AddMousePosEvent(menu->DC.CursorStartPos.x + 1.0f + 148.0f * fraction,
                                menu->DC.CursorStartPos.y + 2 * rowHeight + ImGui::GetFrameHeight() / 2);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            ImGui::ClosePopupsOverWindow(main, false);
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            frame();
        };
        clickMultiplier(0.5f);
        check(window.cpuOverclockPercent() == 150 && selectedPercent == 150 && multiplierChanges == 1,
              "slider must select 1.5x and notify the Engine once");
        clickMultiplier(0.0f);
        check(window.cpuOverclockPercent() == 100 && selectedPercent == 100,
              "slider lower endpoint must select native CPU speed");
        clickMultiplier(1.0f);
        check(window.cpuOverclockPercent() == 200 && selectedPercent == 200,
              "slider upper endpoint must select the existing 2x overclock");
        clickMultiplier(0.5f);
        // Verifica que a próxima amostra de FPS preserva o aviso completo.
        SDL_Delay(1010);
        frame();
        checkTitle(true);
        check(window.renderFrame(running, false, framebuffer), "unloaded frame must render");
        checkTitle(true);
        selectHack("Enable CPU Overclock");
        check(!window.isCPUOverclockEnabled() && !overclockEnabled && overclockChanges == 2,
              "Hacks must disable CPU overclock and notify the Engine once");
        checkTitle(false);
        const int changesBeforeDisabled = multiplierChanges;
        clickMultiplier(1.0f);
        check(window.cpuOverclockPercent() == 150 && multiplierChanges == changesBeforeDisabled,
              "disabled slider must retain its selection and ignore clicks");
        // Cada faixa contém um tom já resolvido por BGP/OBP; o quadro não avança.
        const std::array<std::uint32_t, 4> grayscale{0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000};
        for (int y = 0; y < 144; ++y)
            for (int x = 0; x < 160; ++x)
                framebuffer[y * 160 + x] = grayscale[x / 40];
        const auto originalFrame = framebuffer;
        const auto checkPalette = [&](const std::array<std::uint32_t, 4> &expected, unsigned brightness = 100)
        {
            auto *main = ImGui::FindWindowByName("R2BOY v0.2.6");
            ImGui::ClosePopupsOverWindow(main, false);
            frame();
            frame();
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
                SDL_RenderReadPixels(SDL_GetRenderer(native), nullptr), SDL_DestroySurface);
            check(pixels != nullptr, "palette pixels must be readable");
            const int top = static_cast<int>(main->MenuBarHeight);
            for (int shade = 0; shade < 4; ++shade)
                for (int row = 0; row < 2; ++row)
                {
                    Uint8 r, g, b, alpha;
                    check(SDL_ReadSurfacePixel(pixels.get(), (shade * 40 + 20) * 4, top + 10 + row * 4,
                                               &r, &g, &b, &alpha),
                          "palette color must be readable");
                    const unsigned intensity = row == 0 ? 100 : brightness;
                    const auto channel = [&](unsigned shift)
                    { return ((((expected[shade] >> shift) & 255) * intensity) + 50) / 100; };
                    check(r == channel(16) && g == channel(8) && b == channel(0) && alpha == 255,
                          "preset must map all four tones and compose with scanlines in the correct order");
                }
            check(framebuffer == originalFrame, "palette selection must preserve the paused Core framebuffer");
        };
        const auto selectPalette = [&](const char *label)
        {
            select("Palette Preset");
            auto *submenu = ImGui::FindWindowByName("Palette Preset###Menu_01");
            check(submenu && submenu->Active, "Palette Preset submenu must open");
            ImGui::ActivateItemByID(submenu->GetID(label));
            frame();
        };
        selectPalette("Default");
        checkPalette(grayscale);
        selectPalette("Classic Green");
        checkPalette({0xFF9BBC0F, 0xFF8BAC0F, 0xFF306230, 0xFF0F380F});
        selectPalette("Pocket");
        checkPalette({0xFFE0E8D8, 0xFFA8B8A0, 0xFF607058, 0xFF202820});
        selectPalette("Amber");
        checkPalette({0xFFFFF1C1, 0xFFE8B45B, 0xFF99602F, 0xFF382414});
        selectPalette("Neon");
        checkPalette({0xFFF4D6FF, 0xFFFF45D8, 0xFF852BFF, 0xFF10004D});
        selectPalette("Blue Neon");
        checkPalette({0xFFE0F8FF, 0xFF42DFFF, 0xFF245BFF, 0xFF020B33});
        selectPalette("Green Neon");
        checkPalette({0xFFDCFFE9, 0xFF4DFF88, 0xFF16A34A, 0xFF032A18});
        selectPalette("Ocean");
        const std::array<std::uint32_t, 4> ocean{0xFFE0F4FF, 0xFF8CB8D8, 0xFF406888, 0xFF142838};
        checkPalette(ocean);
        dmgMode = false;
        checkPalette(grayscale); // Até pixels cinza de um jogo CGB devem permanecer intactos.
        dmgMode = true;
        checkPalette(ocean); // Preferência preservada ao voltar a um jogo DMG.
        select("Scanlines");
        checkPalette(ocean, 95);
        selectPalette("Default");
        checkPalette(grayscale, 95);
        select("Scanlines");
        framebuffer.fill(0xFFFFFFFFu);
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

int main(int argc, char **argv)
{
    try
    {
        if (argc == 2 && std::string(argv[1]) == "--rewind")
        {
            rewindMenuControlsHistory();
            std::cout << "Rewind menu tests passed.\n";
            return 0;
        }
        recentFilesUseRomLoadingFlow();
        unsupportedNativeFileNames();
        tileViewerLifecycle();
        capturesKeyboardOnlyForInterface();
        menusRequestTemporaryPause();
        soundMenuControlsOutput();
        displayMenuControlsRenderLayers();
        fileMenuRequestsRomActions();
        windowSizeShortcuts();
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
