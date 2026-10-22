#pragma once

#include "Core/IO/GameBoyButtons.h"
#include <SDL3/SDL.h>
#include <map>
#include <set>
#include <memory>

namespace R2BOY::System
{
    // Entrada de um jogador; combina fontes antes de publicar o estado no Core.
    class Input
    {
    public:
        // Configura os mapeamentos padrão; ainda não inicializa o subsistema SDL.
        Input();

        // Encerra a sessão de entrada e libera os controles que permanecerem abertos.
        ~Input();

        // Os controles SDL têm posse exclusiva e não podem ser duplicados por cópia.
        Input(const Input &) = delete;

        // Preserva a posse dos controles e o estado da sessão de entrada.
        Input &operator=(const Input &) = delete;

        // Inicializar depois de abrir a Window; shutdown deve preceder SDL_Quit.
        bool initialize();

        // Fecha controles e o subsistema de gamepads; pode ser chamado mais de uma vez.
        void shutdown();

        // Processa conexão/remoção e teclado; enabled filtra novas teclas de jogo.
        // Perda de foco ou entrada desabilitada libera as teclas memorizadas.
        void handleEvent(const SDL_Event &event, bool enabled);

        // Combina teclado e controles em GameBoyButtons; desabilitado retorna zero.
        // O turbo pulsa em um de cada quatro frames emulados, independente da renderização.
        std::uint8_t update(std::uint64_t emulatedFrame, bool enabled = true);

        // Esquece teclas pressionadas; os botões físicos dos controles são lidos em update().
        void clear();

        // TAB permanece ativo até a soltura, perda de foco ou captura pela interface.
        bool isFastForwardPressed() const;

        // Consome a solicitação de alternar pausa feita pela tecla Pause.
        bool takePauseToggleRequest();

        // Reconfigura J/K/I/U e os botões frontais do controle, incluindo os atalhos turbo.
        void configureABBAButtons(bool invert);

        // Substitui a associação da tecla e remove seu eventual estado pressionado.
        void mapKey(SDL_Keycode key, Core::IO::GameBoyButtons button, bool turbo = false);

        // Substitui a associação de um botão físico, compartilhada por todos os controles.
        void mapGamepad(SDL_GamepadButton physical, Core::IO::GameBoyButtons button, bool turbo = false);

    private:
        struct Binding
        {
            Core::IO::GameBoyButtons button;
            bool turbo;
        };
        // Abre um controle ainda não registrado; falhas são relatadas sem encerrar a aplicação.
        void openGamepad(SDL_JoystickID id);
        std::map<SDL_Keycode, Binding> keyMap_;
        std::map<SDL_GamepadButton, Binding> gamepadMap_;
        std::set<SDL_Keycode> pressedKeys_;
        using Gamepad = std::unique_ptr<SDL_Gamepad, decltype(&SDL_CloseGamepad)>;
        std::map<SDL_JoystickID, Gamepad> gamepads_;
        bool initialized_ = false;
        bool fastForwardPressed_ = false;
        bool pauseToggleRequested_ = false;
    };
}
