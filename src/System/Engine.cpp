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
        { input.shutdown(); };
        std::unique_ptr<Input, decltype(stopInput)> inputSession(&input, stopInput);
        if (!input.initialize())
            std::cerr << "Input: Gamepads unavailable: " << SDL_GetError() << '\n';

        // Fecha o áudio antes de a Window encerrar SDL; sem dispositivo o emulador fica mudo.
        const auto stopAudio = [this](Audio *)
        { audio.close(); };
        std::unique_ptr<Audio, decltype(stopAudio)> audioSession(&audio_, stopAudio);
        if (audio.open())
            board.setAudioSampleRate(audio.sampleRate());

        // Configura a callback para inverter os botões A/B e B/A conforme a interface do usuário.
        window.setInvertBAYBCallback([this](bool invert)
                                     { input.configureABBAButtons(invert); });

        const char *basePath = SDL_GetBasePath();
        if (!basePath)
        {
            std::cerr << "Unable to locate save directory: " << SDL_GetError() << '\n';
            return 1;
        }

        // RAM de bateria e snapshots usam a pasta de saves ao lado do executável.
        SaveFile saves(std::filesystem::u8path(basePath) / "saves");
        StateFile states(std::filesystem::u8path(basePath) / "saves");
        bool cpuFaultReported = false;
        bool cpuStopReported = false;
        bool menuPaused = false;
        // A interface nunca altera a pausa manual de Pause/Break.
        const auto emulationPaused = [&]()
        { return paused || menuPaused; };
        // Saldo de ciclos a executar; pode ficar negativo ao concluir uma instrução.
        double cycleCredit = 0;
        auto lastExecution = FramePacer::Clock::now();
        FramePacer framePacer(lastExecution);
        vsyncEnabled = window.isVSyncEnabled();
        window.setVSyncCallback([&](bool enabled)
                                {
            vsyncEnabled = enabled;
            cycleCredit = 0;
            lastExecution = FramePacer::Clock::now();
            framePacer.reset(lastExecution); });
        fastForwardEnabled = window.isFastForwardEnabled();
        window.setFastForwardCallback([this](bool enabled)
                                      { fastForwardEnabled = enabled; });
        board.setCPUOverclock(window.isCPUOverclockEnabled());
        window.setCPUOverclockCallback([this](bool enabled)
                                       { board.setCPUOverclock(enabled); });
        board.setUnlimitedSprites(window.isUnlimitedSpritesEnabled());
        window.setUnlimitedSpritesCallback([this](bool enabled)
                                           { board.setUnlimitedSprites(enabled); });
        // Ao mudar de ritmo, reinicia os relógios para não acumular tempo do modo anterior.
        const auto setUncappedSpeed = [&](bool enabled)
        {
            if (uncappedSpeed == enabled || !window.setVSyncSuppressed(enabled))
                return;
            uncappedSpeed = enabled;
            cycleCredit = 0;
            lastExecution = FramePacer::Clock::now();
            framePacer.reset(lastExecution);
        };
        const auto syncFastForward = [&]()
        {
            setUncappedSpeed(fastForwardEnabled && input.isFastForwardPressed() &&
                             board.cartridge().loaded() && !emulationPaused() && !window.isMinimized());
        };
        const auto flushSave = [&]()
        {
            if (saves.save(board.cartridge()))
                std::cout << "SaveFile: Saved RAM -> " << saves.pathFor(board.cartridge()).u8string() << std::endl;
        };
        // ROM nova, reset e restauração de estado começam sem entrada ou tempo pendente.
        const auto resetRuntime = [&]()
        {
            input.clear();
            paused = false;
            setUncappedSpeed(false);
            cpuFaultReported = false;
            cpuStopReported = false;
            cycleCredit = 0;
            lastExecution = FramePacer::Clock::now();
            framePacer.reset(lastExecution);
        };

        // A interface escolhe o arquivo; a Engine faz a ponte entre leitura e cartucho.
        // Erros propagam para o seletor, que apresenta a mensagem ao usuário.
        window.setRomOpenHandler([this, &window, &saves, &flushSave, &resetRuntime](const std::filesystem::path &path, const std::optional<RomFile::Entry> &entry)
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
                board.loadCartridge(std::move(next));
                window.setRomFileName(entry ? std::filesystem::u8path(entry->name).filename().u8string()
                                            : fullPath.filename().u8string());

                // Só registra sucesso depois da validação e substituição do cartucho.
                std::cout << "Cartridge '" << source << "' loaded successfully. ROM Hash (CRC32): "
                          << romHash << std::endl;
                std::cout << "Cartridge: RAM allocated -> " << board.cartridge().ram().size()
                          << " bytes; battery -> " << (board.cartridge().hasBattery() ? "yes" : "no") << std::endl;
                
                if (restored)
                    std::cout << "SaveFile: Loaded RAM -> " << saves.pathFor(board.cartridge()).u8string() << std::endl;
                
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
            // Processa eventos antes da emulação para aplicar a entrada nesta iteração.
            window.pollEvents(running, [this](const SDL_Event &event, bool enabled)
                              { input.handleEvent(event, enabled); });
            if (!running)
                break;
            if (input.takePauseToggleRequest() && board.cartridge().loaded())
            {
                paused = !paused;
                input.clear();
                cycleCredit = 0;
                lastExecution = FramePacer::Clock::now();
                framePacer.reset(lastExecution);
                std::cout << (paused ? "Emulation paused.\n" : "Emulation resumed.\n");
            }
            const bool inputEnabled = window.acceptsGameInput();
            auto inputFrame = board.baseClockCycles() / 70224;
            const auto buttons = input.update(inputFrame, inputEnabled);
            // Entrada continua sendo processada, mas não altera JOYP/IF durante a pausa.
            if (!emulationPaused())
                board.setButtons(buttons);
            syncFastForward();

            // Persiste a RAM periodicamente, inclusive durante pausas e com a janela minimizada.
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
            if (!emulationPaused() && board.cartridge().loaded() && !board.cpu().locked())
            {
                // Limita a recuperação a 50 ms para uma demora do host não gerar uma rajada longa.
                cycleCredit += (uncappedSpeed || vsyncEnabled) ? 70224.0 : std::min(elapsed, 0.05) * 4194304.0;
                while (cycleCredit > 0 && !board.cpu().locked())
                {
                    // Turbo segue o relógio emulado, inclusive com o LCD desligado.
                    const auto frame = board.baseClockCycles() / 70224;
                    if (frame != inputFrame)
                    {
                        inputFrame = frame;
                        board.setButtons(input.update(frame, inputEnabled));
                    }
                    const auto before = board.baseClockCycles();
                    const auto cpuCycles = board.step();
                    const auto baseCycles = board.baseClockCycles() - before;
                    // STOP comum congela o hardware, mas deve consumir a espera do loop.
                    cycleCredit -= baseCycles != 0 ? baseCycles : cpuCycles / board.cpuSpeedMultiplier();
                }
            }
            if (board.cpu().locked() && !cpuFaultReported)
            {
                std::cerr << "CPU: Locked by invalid opcode near PC=" << std::hex
                          << board.cpu().registers().pc << std::dec << std::endl;
                cpuFaultReported = true;
            }
            // Fast-forward e pausa não devem acumular áudio fora do tempo real.
            if (uncappedSpeed || emulationPaused())
            {
                board.takeAudioSamples();
                audio.clear();
            }
            else
                audio.queue(board_.takeAudioSamples());
          
            const auto serialOutput = board_.takeSerialOutput();
            const auto serialOutput = board.takeSerialOutput();
            if (!serialOutput.empty())
                std::cout << serialOutput << std::flush;
            if (board.cpu().stopped() && !cpuStopReported)
            {
                std::cout << "\nCPU: STOP entered; waiting for wake-up."
                          << std::endl;
                cpuStopReported = true;
            }
            if (!board.cpu().stopped())
                cpuStopReported = false;

            // A interface continua sendo desenhada mesmo sem ROM ou com a emulação pausada.
            if (!window.renderFrame(running, board.cartridge().loaded(), board.framebuffer(),
                                    board.executionMode() == Core::ExecutionMode::DMG))
            {
                renderFailed = true;
                break;
            }

            // ImGui abre/fecha os menus durante renderFrame(). Descarta o tempo
            // da interface nos dois sentidos, sem tocar na pausa manual.
            const bool menuOpen = window.isMenuOpen();
            if (menuPaused != menuOpen)
            {
                menuPaused = menuOpen;
                input.clear();
                cycleCredit = 0;
                lastExecution = FramePacer::Clock::now();
                framePacer.reset(lastExecution);
            }

            // Executa as ações escolhidas nos menus depois de terminar o desenho da interface.
            const auto action = window.takeRomAction();
            if (action != Window::Window::RomAction::None && board.cartridge().loaded())
            {
                try
                {
                    if (action == Window::Window::RomAction::SaveState)
                    {
                        states.save(board, window.stateSlot());
                        std::cout << "Saved state to slot " << window.stateSlot() << std::endl;
                    }
                    else if (action == Window::Window::RomAction::LoadState)
                    {
                        states.load(board, window.stateSlot());
                        resetRuntime();
                        std::cout << "Loaded state from slot " << window.stateSlot() << std::endl;
                    }
                    else
                    {
                        flushSave();
                        if (action == Window::Window::RomAction::Unload)
                        {
                            board.unloadCartridge();
                            window.setRomFileName({});
                        }
                        else
                            board.reset();
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

            // Verifica se o motor ainda está em execução antes de continuar.
            if (!running)
                break;

            // Menus e carregamentos podem mudar a permissão durante renderFrame().
            if (!window.acceptsGameInput())
                input.clear();

            // Sincroniza o estado de avanço rápido antes de renderizar o próximo frame.
            syncFastForward();

            // Sem a espera do VSync, o FramePacer controla o intervalo entre apresentações.
            if (!vsyncEnabled && !uncappedSpeed)
            {
                const auto wait = framePacer.remaining(FramePacer::Clock::now());
                if (wait > FramePacer::Duration::zero())
                    SDL_DelayPrecise(static_cast<Uint64>(wait.count()));
            }
        }

        // Faz uma última gravação da RAM, também ao sair por falha de renderização.
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