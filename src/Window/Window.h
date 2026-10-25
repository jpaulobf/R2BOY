#pragma once

#include <memory>
#include <cstdint>
#include <functional>
#include <string>
#include "FileManager.h"
#include "Common/Framebuffer.h"

union SDL_Event;

namespace R2BOY::Window
{
    /**
     * Gerencia a janela SDL, os eventos e a interface Dear ImGui.
     * Apresenta o framebuffer e o seletor de ROMs, delegando o carregamento ao callback
     * configurado pela Engine. Os recursos nativos ficam ocultos em Impl e são
     * liberados automaticamente na destruição. A instância não pode ser copiada.
     */
    class Window
    {
    public:
        // Constrói a fachada sem criar recursos SDL; open() inicia a sessão.
        Window();

        // Libera ImGui, textura, renderer e janela antes de encerrar SDL.
        ~Window();

        // A sessão SDL/ImGui e seus recursos têm posse exclusiva.
        Window(const Window &) = delete;

        // Impede compartilhar ou substituir os recursos nativos por cópia.
        Window &operator=(const Window &) = delete;

        // Inicializa SDL/ImGui; retorna false em falha e true se já estiver aberta.
        // Os demais métodos públicos exigem que open() tenha retornado true.
        bool open();

        // Encaminha eventos ao ImGui e pode definir running como false ao solicitar saída.
        using InputHandler = std::function<void(const SDL_Event &, bool)>;

        // Drena a fila SDL; o callback recebe cada evento e a permissão atual de entrada.
        void pollEvents(bool &running, const InputHandler &inputHandler = {});

        // Exige foco da janela e ausência de captura de teclado pelo ImGui.
        bool acceptsGameInput() const;

        // Menu, submenu ou seletor modal aberto solicita pausa temporária da emulação.
        bool isMenuOpen() const;

        // Indica se a janela está minimizada, permitindo suspender a renderização.
        bool isMinimized() const;

        // Callback síncrono: recebe o caminho e, para ZIP, o membro selecionado.
        // Exceções std::exception são capturadas pelo seletor e exibidas ao usuário.
        using RomOpenHandler = FileManager::RomOpenHandler;

        // Substitui o callback; configure antes de oferecer a abertura de ROMs.
        void setRomOpenHandler(RomOpenHandler handler);

        // Atualiza imediatamente o nome do arquivo exibido após o FPS; vazio ao descarregar.
        void setRomFileName(std::string name);

        // Ação solicitada pelo menu File; deve ser consumida pela Engine.
        enum class RomAction
        {
            None,
            Unload,
            Reset,
            SaveState,
            LoadState
        };

        // Consome a ação do menu File para a Engine aplicá-la após renderFrame().
        RomAction takeRomAction();

        // Slot selecionado no submenu File (1 a 3).
        int stateSlot() const;

        // Notifica a Engine quando o usuário altera o layout no menu Input.
        using InvertBAYBCallback = std::function<void(bool)>;

        // Substitui o callback do menu; não aplica o layout imediatamente.
        void setInvertBAYBCallback(InvertBAYBCallback callback);

        // Callback para notificar mudanças no estado do VSync.
        using VSyncCallback = std::function<void(bool)>;

        // Notifica a Engine somente após aplicar a mudança no renderer SDL.
        void setVSyncCallback(VSyncCallback callback);

        // Alterna o VSync; em falha SDL, preserva o estado anterior.
        void toggleVSync();

        // Retorna se o VSync está habilitado.
        bool isVSyncEnabled() const;

        // Preferências de saída da sessão, independentes dos registradores da APU.
        using SoundCallback = std::function<void(bool master, std::uint8_t channels)>;

        // Substitui o callback do menu; não aplica as preferências de som imediatamente.
        void setSoundCallback(SoundCallback callback);

        // Retorna se o som mestre está habilitado.
        bool isMasterSoundEnabled() const;

        // Retorna os canais de som habilitados.
        std::uint8_t soundChannels() const;

        // Volume da saída de áudio da sessão (0 a 100), independente de Master Sound.
        using VolumeCallback = std::function<void(int)>;
        void setVolumeCallback(VolumeCallback callback);
        int soundVolume() const;

        using ChannelVolumeCallback = std::function<void(unsigned channel, int percent)>;
        void setChannelVolumeCallback(ChannelVolumeCallback callback);
        int soundChannelVolume(unsigned channel) const;

        // Suspende a sincronização durante fast-forward, preservando a opção Display.
        // Retorna false em falha SDL, mantendo o estado anterior.
        bool setVSyncSuppressed(bool suppressed);

        // Retorna se o VSync está temporariamente suprimido durante fast-forward.
        using FastForwardCallback = std::function<void(bool)>;

        // Substitui o callback do menu; não aplica o fast-forward imediatamente.
        void setFastForwardCallback(FastForwardCallback callback);

        // Retorna se o fast-forward está habilitado.
        bool isFastForwardEnabled() const;

        // Callback para notificar mudanças no estado do overclock da CPU.
        using CPUOverclockCallback = std::function<void(bool)>;

        // Substitui o callback do menu; não aplica o overclock imediatamente.
        void setCPUOverclockCallback(CPUOverclockCallback callback);

        // Retorna se o overclock da CPU está habilitado.
        bool isCPUOverclockEnabled() const;

        // Callback para notificar mudanças no estado do hack de sprites ilimitadas.
        using UnlimitedSpritesCallback = std::function<void(bool)>;

        // Substitui o callback do menu; não aplica o hack imediatamente.
        void setUnlimitedSpritesCallback(UnlimitedSpritesCallback callback);

        // Retorna se o hack de sprites ilimitadas está habilitado.
        bool isUnlimitedSpritesEnabled() const;

        // Permite imprimir os bytes da porta serial no console; desativado por padrão.
        bool isSerialOutputEnabled() const;

        // Desenha e apresenta um frame; pode solicitar saída via running e retorna false em falha SDL.
        // gameLoaded controla a exibição do jogo e a medição de FPS, sem consultar o Core.
        // O framebuffer é usado por referência apenas durante esta chamada.
        // dmgMode aplica Palette Preset apenas a jogos monocromáticos, inclusive no CGB.
        bool renderFrame(bool &running, bool gameLoaded, const Common::Framebuffer &framebuffer, bool dmgMode = true);

    private:
        // Mantém tipos SDL/ImGui e sua ordem de destruição fora da interface pública.
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
