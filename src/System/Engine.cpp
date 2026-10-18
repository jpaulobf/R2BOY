#include "Engine.h"

#include "Window/Window.h"
#include <SDL.h>
#include <iostream>
#include <exception>
#include "SaveFile.h"
#include <chrono>
#include <memory>

namespace R2BOY::System
{
    int Engine::run()
    {
        Window::Window window;
        if (!window.open())
            return 1;

        std::unique_ptr<char, decltype(&SDL_free)> basePath(SDL_GetBasePath(), SDL_free);
        if (!basePath)
        {
            std::cerr << "Unable to locate save directory: " << SDL_GetError() << '\n';
            return 1;
        }

        SaveFile saves(std::filesystem::u8path(basePath.get()) / "saves");
        const auto flushSave = [&]()
        {
            if (saves.save(board_.cartridge()))
                std::cout << "SaveFile: Saved RAM -> " << saves.pathFor(board_.cartridge()).u8string() << std::endl;
        };

        // A interface escolhe o arquivo; a Engine faz a ponte entre leitura e cartucho.
        // Erros propagam para o seletor, que apresenta a mensagem ao usuário.
        window.setRomOpenHandler([this, &saves, &flushSave](const std::filesystem::path &path, const std::optional<RomFile::Entry> &entry)
                                 {
            const auto fullPath = std::filesystem::absolute(path).lexically_normal();
            auto source = fullPath.u8string();
            if (entry)
                source += " :: " + entry->name;

            std::cout << "Engine: Loading ROM -> " << source << std::endl;
            try
            {
                auto rom = entry ? RomFile::read(fullPath, entry->index) : RomFile::read(fullPath);
                const auto romHash = RomFile::hash(rom);
                Core::Cartridge::Cartridge next;
                next.load(std::move(rom), source);
                // Grava antes de restaurar: reabrir a mesma ROM também deve preservar a RAM mais recente.
                flushSave();
                const bool restored = saves.load(next);
                board_.loadCartridge(std::move(next));
                // Só registra sucesso depois da validação e substituição do cartucho.
                std::cout << "Cartridge '" << source << "' loaded successfully. ROM Hash (CRC32): "
                          << romHash << std::endl;
                std::cout << "Cartridge: RAM allocated -> " << board_.cartridge().ram().size()
                          << " bytes; battery -> " << (board_.cartridge().hasBattery() ? "yes" : "no") << std::endl;
                if (restored)
                    std::cout << "SaveFile: Loaded RAM -> " << saves.pathFor(board_.cartridge()).u8string() << std::endl;
            }
            catch (const std::exception &e)
            {
                std::cerr << "Engine: Failed to load ROM '" << source << "': " << e.what() << std::endl;
                throw; // O seletor também apresenta o erro ao usuário.
            } });

        bool running = true;
        bool renderFailed = false;
        auto nextSave = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (running)
        {
            window.pollEvents(running);
            if (!running)
                break;

            if (std::chrono::steady_clock::now() >= nextSave)
            {
                try
                {
                    flushSave();
                }
                catch (const std::exception &e)
                {
                    std::cerr << "SaveFile: " << e.what() << std::endl;
                }
                nextSave = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            }

            if (window.isMinimized())
            {
                SDL_Delay(16);
                continue;
            }

            // Avançar os ciclos da Board quando a execução do hardware estiver implementada.
            if (!window.renderFrame(running, board_.cartridge()))
            {
                renderFailed = true;
                break;
            }

            SDL_Delay(16); // Pausa provisória até implementar a temporização do hardware.
        }

        try
        {
            flushSave();
        }
        catch (const std::exception &e)
        {
            std::cerr << "SaveFile: " << e.what() << std::endl;
            return 1;
        }
        return renderFailed ? 1 : 0;
    }
}
