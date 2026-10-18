#include "Engine.h"

#include "Window/Window.h"
#include <SDL.h>
#include <iostream>
#include <exception>
#include "SaveFile.h"
#include <chrono>
#include <memory>
#include <algorithm>

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
        bool cpuFaultReported = false;
        double cycleCredit = 0;
        const auto flushSave = [&]()
        {
            if (saves.save(board_.cartridge()))
                std::cout << "SaveFile: Saved RAM -> " << saves.pathFor(board_.cartridge()).u8string() << std::endl;
        };

        // A interface escolhe o arquivo; a Engine faz a ponte entre leitura e cartucho.
        // Erros propagam para o seletor, que apresenta a mensagem ao usuário.
        window.setRomOpenHandler([this, &saves, &flushSave, &cpuFaultReported, &cycleCredit](const std::filesystem::path &path, const std::optional<RomFile::Entry> &entry)
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
                cpuFaultReported = false;
                cycleCredit = 0;

                // Só registra sucesso depois da validação e substituição do cartucho.
                std::cout << "Cartridge '" << source << "' loaded successfully. ROM Hash (CRC32): "
                          << romHash << std::endl;
                std::cout << "Cartridge: RAM allocated -> " << board_.cartridge().ram().size()
                          << " bytes; battery -> " << (board_.cartridge().hasBattery() ? "yes" : "no") << std::endl;
                if (restored)
                    std::cout << "SaveFile: Loaded RAM -> " << saves.pathFor(board_.cartridge()).u8string() << std::endl;
                std::cout << "CPU: Reset without boot ROM -> PC=0100, SP=FFFE" << std::endl;
            }
            catch (const std::exception &e)
            {
                std::cerr << "Engine: Failed to load ROM '" << source << "': " << e.what() << std::endl;
                throw; // O seletor também apresenta o erro ao usuário.
            } });

        bool running = true;
        bool renderFailed = false;
        auto nextSave = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        auto lastExecution = std::chrono::steady_clock::now();

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
                lastExecution = std::chrono::steady_clock::now();
                SDL_Delay(16);
                continue;
            }

            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - lastExecution).count();
            lastExecution = now;
            
            // Limita a recuperação de tempo após pausas e mantém a fração/estouro
            // de ciclos entre frames. Double speed CGB ainda não está implementado.
            if (board_.cartridge().loaded() && !board_.cpu().locked())
            {
                cycleCredit += std::min(elapsed, 0.05) * 4194304.0;
                while (cycleCredit > 0 && !board_.cpu().locked())
                    cycleCredit -= board_.step();
            }
            if (board_.cpu().locked() && !cpuFaultReported)
            {
                std::cerr << "CPU: Locked by invalid opcode near PC=" << std::hex
                          << board_.cpu().registers().pc << std::dec << std::endl;
                cpuFaultReported = true;
            }
            const auto serialOutput = board_.takeSerialOutput();
            if (!serialOutput.empty())
                std::cout << serialOutput << std::flush;
            if (!window.renderFrame(running, board_.cartridge()))
            {
                renderFailed = true;
                break;
            }

            SDL_Delay(16); // Apresentação provisória; o avanço da CPU usa o tempo decorrido.
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
