#pragma once

#include <memory>
#include <functional>
#include "FileManager.h"
#include "Core/PPU/PPU.h"

union SDL_Event;

namespace R2BOY::Core::Cartridge
{
    class Cartridge;
}

namespace R2BOY::Window
{
    /**
     * Gerencia a janela SDL, os eventos e a interface Dear ImGui.
     * Apresenta o cartucho e o seletor de ROMs, delegando o carregamento ao callback
     * configurado pela Engine. Os recursos nativos ficam ocultos em Impl e são
     * liberados automaticamente na destruição. A instância não pode ser copiada.
     */
    class Window
    {
    public:
        Window();
        ~Window();

        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;

        // Inicializa SDL/ImGui; retorna false em falha e true se já estiver aberta.
        // Os demais métodos públicos exigem que open() tenha retornado true.
        bool open();

        // Encaminha eventos ao ImGui e pode definir running como false ao solicitar saída.
        using InputHandler = std::function<void(const SDL_Event &, bool)>;
        void pollEvents(bool &running, const InputHandler &inputHandler = {});
        bool acceptsGameInput() const;

        // Indica se a janela está minimizada, permitindo suspender a renderização.
        bool isMinimized() const;

        // Callback síncrono: recebe o caminho e, para ZIP, o membro selecionado.
        // Exceções std::exception são capturadas pelo seletor e exibidas ao usuário.
        using RomOpenHandler = FileManager::RomOpenHandler;

        // Substitui o callback; configure antes de oferecer a abertura de ROMs.
        void setRomOpenHandler(RomOpenHandler handler);

        // Notifica a Engine quando o usuário altera o layout no menu Input.
        using InvertBAYBCallback = std::function<void(bool)>;
        void setInvertBAYBCallback(InvertBAYBCallback callback);

        // Desenha e apresenta um frame; pode solicitar saída via running e retorna false em falha SDL.
        bool renderFrame(bool &running, const Core::Cartridge::Cartridge &cartridge,
                         const Core::PPU::PPU::Framebuffer &framebuffer);

    private:
        // Mantém tipos SDL/ImGui e sua ordem de destruição fora da interface pública.
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
