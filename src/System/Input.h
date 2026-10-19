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
        Input();
        ~Input();
        Input(const Input &) = delete;
        Input &operator=(const Input &) = delete;

        // Inicializar depois de abrir a Window; shutdown deve preceder SDL_Quit.
        bool initialize();
        void shutdown();
        void handleEvent(const SDL_Event &event, bool enabled);
        std::uint8_t update(std::uint64_t emulatedFrame, bool enabled = true);
        void clear();
        void configureABBAButtons(bool invert);
        void mapKey(SDL_Keycode key, Core::IO::GameBoyButtons button, bool turbo = false);
        void mapGamepad(SDL_GamepadButton physical, Core::IO::GameBoyButtons button, bool turbo = false);

    private:
        struct Binding
        {
            Core::IO::GameBoyButtons button;
            bool turbo;
        };
        void openGamepad(SDL_JoystickID id);
        std::map<SDL_Keycode, Binding> keyMap_;
        std::map<SDL_GamepadButton, Binding> gamepadMap_;
        std::set<SDL_Keycode> pressedKeys_;
        using Gamepad = std::unique_ptr<SDL_Gamepad, decltype(&SDL_CloseGamepad)>;
        std::map<SDL_JoystickID, Gamepad> gamepads_;
        bool initialized_ = false;
    };
}
