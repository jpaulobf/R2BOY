#pragma once
#include <SDL3/SDL.h>
#include <array>

namespace R2BOY::System
{
    // Uma tecla por função; zero indica uma função sem tecla atribuída.
    using KeyboardMapping = std::array<SDL_Keycode, 10>;
    // Retorna as associações de fábrica usadas na inicialização e na opção Default.
    inline KeyboardMapping defaultKeyboardMapping()
    {
        return {SDLK_W, SDLK_S, SDLK_A, SDLK_D, SDLK_J, SDLK_K, SDLK_U, SDLK_I, SDLK_RETURN, SDLK_BACKSPACE};
    }
    // Exclui teclas sem nome e as reservadas aos atalhos do emulador.
    inline bool assignableKey(SDL_Keycode key)
    {
        return key != 0 && key != SDLK_TAB && key != SDLK_CAPSLOCK && key != SDLK_PAUSE &&
               key != SDLK_F5 && key != SDLK_F6 && key != SDLK_F12 &&
               key != SDLK_LCTRL && key != SDLK_RCTRL && key != SDLK_LALT && key != SDLK_RALT &&
               key != SDLK_LGUI && key != SDLK_RGUI && SDL_GetKeyName(key)[0] != '\0';
    }
}