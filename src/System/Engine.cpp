#include "Engine.h"

#include "Window/Window.h"
#include <SDL.h>
#include <iostream>
#include <exception>

namespace R2BOY::System
{
    int Engine::run()
    {
        Window::Window window;
        if (!window.open())
            return 1;

        // A interface escolhe o arquivo; a Engine faz a ponte entre leitura e cartucho.
        // Erros propagam para o seletor, que apresenta a mensagem ao usuário.
        window.setRomOpenHandler([this](const std::filesystem::path &path, const std::optional<RomFile::Entry> &entry)
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
                board_.cartridge().load(std::move(rom), source);
                // Só registra sucesso depois da validação e substituição do cartucho.
                std::cout << "Cartridge '" << source << "' loaded successfully. ROM Hash (CRC32): "
                          << romHash << std::endl;
            }
            catch (const std::exception &e)
            {
                std::cerr << "Engine: Failed to load ROM '" << source << "': " << e.what() << std::endl;
                throw; // O seletor também apresenta o erro ao usuário.
            }
        });

        bool running = true;

        while (running)
        {
            window.pollEvents(running);
            if (!running)
                break;

            if (window.isMinimized())
            {
                SDL_Delay(16);
                continue;
            }

            // Avançar os ciclos da Board quando a execução do hardware estiver implementada.
            if (!window.renderFrame(running, board_.cartridge()))
                return 1;

            SDL_Delay(16); // Pausa provisória até implementar a temporização do hardware.
        }

        return 0;
    }
}
