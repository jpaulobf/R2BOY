#include "Engine.h"

#include "Window/Window.h"
#include "Common/RomFile.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <exception>
#include "SaveFile.h"
#include "StateFile.h"
#include "FramePacer.h"
#include <chrono>
#include <algorithm>
#include <memory>

namespace R2BOY::System
{
    using Common::RomFile;

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
        StateFile states(std::filesystem::u8path(basePath) / "saves");
        bool cpuFaultReported = false;
        bool cpuStopReported = false;
        double cycleCredit = 0;
        auto lastExecution = FramePacer::Clock::now();
        FramePacer framePacer(lastExecution);
        vsyncEnabled_ = window.isVSyncEnabled();
        window.setVSyncCallback([&](bool enabled)
                                {
            vsyncEnabled_ = enabled;
            cycleCredit = 0;
            lastExecution = FramePacer::Clock::now();
            framePacer.reset(lastExecution); });
        fastForwardEnabled_ = window.isFastForwardEnabled();
        window.setFastForwardCallback([this](bool enabled)
                                      { fastForwardEnabled_ = enabled; });
        board_.setCPUOverclock(window.isCPUOverclockEnabled());
        window.setCPUOverclockCallback([this](bool enabled)
                                       { board_.setCPUOverclock(enabled); });
        const auto setUncappedSpeed = [&](bool enabled)
        {
            if (uncappedSpeed_ == enabled || !window.setVSyncSuppressed(enabled))
                return;
            uncappedSpeed_ = enabled;
            cycleCredit = 0;
            lastExecution = FramePacer::Clock::now();
            framePacer.reset(lastExecution);
        };
        const auto syncFastForward = [&]()
        {
            setUncappedSpeed(fastForwardEnabled_ && input_.isFastForwardPressed() &&
                             board_.cartridge().loaded() && !paused_ && !window.isMinimized());
        };
        const auto flushSave = [&]()
        {
            if (saves.save(board_.cartridge()))
                std::cout << "SaveFile: Saved RAM -> " << saves.pathFor(board_.cartridge()).u8string() << std::endl;
        };
        const auto resetRuntime = [&]()
        {
            input_.clear();
            paused_ = false;
            setUncappedSpeed(false);
            cpuFaultReported = false;
            cpuStopReported = false;
            cycleCredit = 0;
            lastExecution = FramePacer::Clock::now();
            framePacer.reset(lastExecution);
        };

        // A interface escolhe o arquivo; a Engine faz a ponte entre leitura e cartucho.
        // Erros propagam para o seletor, que apresenta a mensagem ao usuário.
        window.setRomOpenHandler([this, &saves, &flushSave, &resetRuntime](const std::filesystem::path &path, const std::optional<RomFile::Entry> &entry)
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
                std::cout << "CPU: Reset without boot ROM -> PC=0100, SP=FFFE" << std::endl;
                std::cout << "Serial: ROM output follows in this console.\n" << std::endl;
                // O carregamento não conta como tempo de execução da nova ROM.
                resetRuntime();
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
            if (input_.takePauseToggleRequest() && board_.cartridge().loaded())
            {
                paused_ = !paused_;
                input_.clear();
                cycleCredit = 0;
                lastExecution = FramePacer::Clock::now();
                framePacer.reset(lastExecution);
                std::cout << (paused_ ? "Emulation paused.\n" : "Emulation resumed.\n");
            }
            const bool inputEnabled = window.acceptsGameInput();
            auto inputFrame = board_.baseClockCycles() / 70224;
            const auto buttons = input_.update(inputFrame, inputEnabled);
            // Entrada continua sendo processada, mas não altera JOYP/IF durante a pausa.
            if (!paused_)
                board_.setButtons(buttons);
            syncFastForward();

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

            // Com VSync ou fast-forward, avança um quadro por apresentação.
            // Fast-forward suspende a espera SDL e o limitador abaixo.
            // Sem VSync, mantém o avanço por tempo decorrido e o cap de 60 FPS.
            // O orçamento usa o clock base, independente de double speed e overclock.
            if (!paused_ && board_.cartridge().loaded() && !board_.cpu().locked())
            {
                cycleCredit += (uncappedSpeed_ || vsyncEnabled_) ? 70224.0 : std::min(elapsed, 0.05) * 4194304.0;
                while (cycleCredit > 0 && !board_.cpu().locked())
                {
                    // Turbo segue o relógio emulado, inclusive com o LCD desligado.
                    const auto frame = board_.baseClockCycles() / 70224;
                    if (frame != inputFrame)
                    {
                        inputFrame = frame;
                        board_.setButtons(input_.update(frame, inputEnabled));
                    }
                    const auto before = board_.baseClockCycles();
                    const auto cpuCycles = board_.step();
                    const auto baseCycles = board_.baseClockCycles() - before;
                    // STOP comum congela o hardware, mas deve consumir a espera do loop.
                    cycleCredit -= baseCycles != 0 ? baseCycles : cpuCycles / board_.cpuSpeedMultiplier();
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

            if (!window.renderFrame(running, board_.cartridge().loaded(), board_.framebuffer()))
            {
                renderFailed = true;
                break;
            }

            const auto action = window.takeRomAction();
            if (action != Window::Window::RomAction::None && board_.cartridge().loaded())
            {
                try
                {
                    if (action == Window::Window::RomAction::SaveState)
                    {
                        states.save(board_, window.stateSlot());
                        std::cout << "Saved state to slot " << window.stateSlot() << std::endl;
                    }
                    else if (action == Window::Window::RomAction::LoadState)
                    {
                        states.load(board_, window.stateSlot());
                        resetRuntime();
                        std::cout << "Loaded state from slot " << window.stateSlot() << std::endl;
                    }
                    else
                    {
                        flushSave();
                        if (action == Window::Window::RomAction::Unload)
                            board_.unloadCartridge();
                        else
                            board_.reset();
                        resetRuntime();
                    }
                }
                catch (const std::exception &e)
                {
                    if (action == Window::Window::RomAction::SaveState || action == Window::Window::RomAction::LoadState)
                    {
                        std::cerr << "Save state: " << e.what() << std::endl;
                        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Save State", e.what(), nullptr);
                        lastExecution = FramePacer::Clock::now();
                        framePacer.reset(lastExecution);
                    }
                    else
                        std::cerr << "Engine: Failed to "
                                  << (action == Window::Window::RomAction::Unload ? "unload" : "reset")
                                  << " ROM: " << e.what() << std::endl;
                }
            }

            if (!running)
                break;
            // Menus e carregamentos podem mudar a permissão durante renderFrame().
            if (!window.acceptsGameInput())
                input_.clear();
            syncFastForward();
            if (!vsyncEnabled_ && !uncappedSpeed_)
            {
                const auto wait = framePacer.remaining(FramePacer::Clock::now());
                if (wait > FramePacer::Duration::zero())
                    SDL_DelayPrecise(static_cast<Uint64>(wait.count()));
            }
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