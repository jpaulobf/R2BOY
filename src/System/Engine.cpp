#include "Engine.h"

#include "Window/Window.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <exception>
#include "SaveFile.h"
#include "FramePacer.h"
#include <chrono>
#include <algorithm>
#include <memory>

namespace R2BOY::System
{
    int Engine::run()
    {
        Window::Window window;
        if (!window.open())
            return 1;

        // Libera controles antes de a Window encerrar SDL, inclusive em falhas.
        const auto stopInput = [this](Input *)
        { input_.shutdown(); };
        std::unique_ptr<Input, decltype(stopInput)> inputSession(&input_, stopInput);
        if (!input_.initialize())
            std::cerr << "Input: Gamepads unavailable: " << SDL_GetError() << '\n';

        window.setInvertBAYBCallback([this](bool invert)
                                     { input_.configureABBAButtons(invert); });

        const char *basePath = SDL_GetBasePath();
        if (!basePath)
        {
            std::cerr << "Unable to locate save directory: " << SDL_GetError() << '\n';
            return 1;
        }

        SaveFile saves(std::filesystem::u8path(basePath) / "saves");
        bool cpuFaultReported = false;
        bool cpuStopReported = false;
        bool speedSwitchRequested = false;
        bool speedSwitchWarningReported = false;
        // Contorno provisório da interface: observa a escrita de preparação em KEY1.
        // Não altera leituras de KEY1 nem simula a velocidade dupla no Core.
        board_.cpu().setCycleObserver([&speedSwitchRequested](const Core::CPU::CPU::Cycle &cycle)
                                      {
            if (cycle.type == Core::CPU::CPU::CycleType::Write && cycle.address == 0xFF4D)
                speedSwitchRequested = (cycle.value & 1) != 0; });
        double cycleCredit = 0;
        auto lastExecution = FramePacer::Clock::now();
        FramePacer framePacer(lastExecution);
        const auto flushSave = [&]()
        {
            if (saves.save(board_.cartridge()))
                std::cout << "SaveFile: Saved RAM -> " << saves.pathFor(board_.cartridge()).u8string() << std::endl;
        };

        // A interface escolhe o arquivo; a Engine faz a ponte entre leitura e cartucho.
        // Erros propagam para o seletor, que apresenta a mensagem ao usuário.
        window.setRomOpenHandler([this, &saves, &flushSave, &cpuFaultReported, &cpuStopReported, &speedSwitchRequested, &speedSwitchWarningReported, &cycleCredit, &lastExecution, &framePacer](const std::filesystem::path &path, const std::optional<RomFile::Entry> &entry)
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
                input_.clear();
                cpuFaultReported = false;
                cpuStopReported = false;
                speedSwitchRequested = false;
                speedSwitchWarningReported = false;
                cycleCredit = 0;

                // Só registra sucesso depois da validação e substituição do cartucho.
                std::cout << "Cartridge '" << source << "' loaded successfully. ROM Hash (CRC32): "
                          << romHash << std::endl;
                std::cout << "Cartridge: RAM allocated -> " << board_.cartridge().ram().size()
                          << " bytes; battery -> " << (board_.cartridge().hasBattery() ? "yes" : "no") << std::endl;
                if (restored)
                    std::cout << "SaveFile: Loaded RAM -> " << saves.pathFor(board_.cartridge()).u8string() << std::endl;
                std::cout << "CPU: Reset without boot ROM -> PC=0100, SP=FFFE" << std::endl;
                std::cout << "Serial: ROM output follows in this console.\n" << std::endl;
                // O carregamento não conta como tempo de execução da nova ROM.
                lastExecution = FramePacer::Clock::now();
                framePacer.reset(lastExecution);
            }
            catch (const std::exception &e)
            {
                std::cerr << "Engine: Failed to load ROM '" << source << "': " << e.what() << std::endl;
                throw; // O seletor também apresenta o erro ao usuário.
            } });

        bool running = true;
        bool renderFailed = false;
        auto nextSave = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        lastExecution = FramePacer::Clock::now();
        framePacer.reset(lastExecution);

        while (running)
        {
            window.pollEvents(running, [this](const SDL_Event &event, bool enabled)
                              { input_.handleEvent(event, enabled); });
            if (!running)
                break;
            const bool inputEnabled = window.acceptsGameInput();
            auto inputFrame = board_.clockCycles() / 70224;
            board_.setButtons(input_.update(inputFrame, inputEnabled));

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
                // Reduz uso de CPU em segundo plano e descarta o tempo pausado.
                SDL_Delay(50);
                lastExecution = FramePacer::Clock::now();
                framePacer.reset(lastExecution);
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
                {
                    // Turbo segue o relógio emulado, inclusive com o LCD desligado.
                    const auto frame = board_.clockCycles() / 70224;
                    if (frame != inputFrame)
                    {
                        inputFrame = frame;
                        board_.setButtons(input_.update(frame, inputEnabled));
                    }
                    cycleCredit -= board_.step();
                    if (board_.cpu().stopped() && speedSwitchRequested &&
                        board_.executionMode() == Core::ExecutionMode::CGB)
                    {
                        // Consome apenas o pedido de troca; STOP comum continua parando.
                        board_.cpu().wakeFromStop();
                        speedSwitchRequested = false;
                        if (!speedSwitchWarningReported)
                        {
                            std::cout << board_.takeSerialOutput()
                                      << "\nCPU: Skipping CGB speed-switch STOP (temporary workaround). "
                                         "Continuing at normal speed; timing results are not reliable.\n"
                                      << std::flush;
                            speedSwitchWarningReported = true;
                        }
                    }
                }
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
            if (board_.cpu().stopped() && !cpuStopReported)
            {
                std::cout << "\nCPU: STOP entered; waiting for wake-up."
                          << std::endl;
                cpuStopReported = true;
            }
            if (!board_.cpu().stopped())
                cpuStopReported = false;

            if (!window.renderFrame(running, board_.cartridge(), board_.framebuffer()))
            {
                renderFailed = true;
                break;
            }

            if (!running)
                break;
            const auto wait = framePacer.remaining(FramePacer::Clock::now());
            if (wait > FramePacer::Duration::zero())
                SDL_DelayPrecise(static_cast<Uint64>(wait.count()));
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
