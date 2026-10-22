#include "Input.h"

#include <iostream>

namespace R2BOY::System
{
    using namespace Core::IO;

    Input::Input()
    {
        mapKey(SDLK_W, BUTTON_UP);
        mapKey(SDLK_S, BUTTON_DOWN);
        mapKey(SDLK_A, BUTTON_LEFT);
        mapKey(SDLK_D, BUTTON_RIGHT);
        mapKey(SDLK_BACKSPACE, BUTTON_SELECT);
        mapKey(SDLK_RETURN, BUTTON_START);
        mapGamepad(SDL_GAMEPAD_BUTTON_DPAD_UP, BUTTON_UP);
        mapGamepad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, BUTTON_DOWN);
        mapGamepad(SDL_GAMEPAD_BUTTON_DPAD_LEFT, BUTTON_LEFT);
        mapGamepad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, BUTTON_RIGHT);
        mapGamepad(SDL_GAMEPAD_BUTTON_BACK, BUTTON_SELECT);
        mapGamepad(SDL_GAMEPAD_BUTTON_START, BUTTON_START);
        configureABBAButtons(false);
    }

    Input::~Input() { shutdown(); }

    bool Input::initialize()
    {
        if (initialized_)
            return true;
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
            return false;
        initialized_ = true;
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        for (int i = 0; ids && i < count; ++i)
            openGamepad(ids[i]);
        SDL_free(ids);
        return true;
    }

    void Input::shutdown()
    {
        gamepads_.clear();
        clear();
        if (initialized_)
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        initialized_ = false;
    }

    void Input::openGamepad(SDL_JoystickID id)
    {
        if (!initialized_ || gamepads_.count(id))
            return;
        Gamepad pad(SDL_OpenGamepad(id), SDL_CloseGamepad);
        if (pad)
            gamepads_.emplace(id, std::move(pad));
        else
            std::cerr << "Input: Failed to open gamepad: " << SDL_GetError() << '\n';
    }

    void Input::mapKey(SDL_Keycode key, GameBoyButtons button, bool turbo)
    {
        pressedKeys_.erase(key);
        keyMap_.insert_or_assign(key, Binding{button, turbo});
    }

    void Input::mapGamepad(SDL_GamepadButton physical, GameBoyButtons button, bool turbo)
    {
        gamepadMap_.insert_or_assign(physical, Binding{button, turbo});
    }

    void Input::configureABBAButtons(bool invert)
    {
        clear();
        mapKey(SDLK_J, invert ? BUTTON_A : BUTTON_B);
        mapKey(SDLK_K, BUTTON_A, invert);
        mapKey(SDLK_I, invert ? BUTTON_B : BUTTON_A, true);
        mapKey(SDLK_U, BUTTON_B, !invert);
        // Posições SDL: South=A, East=B, West=X, North=Y no layout Xbox.
        mapGamepad(SDL_GAMEPAD_BUTTON_SOUTH, invert ? BUTTON_A : BUTTON_B);
        mapGamepad(SDL_GAMEPAD_BUTTON_EAST, BUTTON_A, invert);
        mapGamepad(SDL_GAMEPAD_BUTTON_WEST, BUTTON_B, !invert);
        mapGamepad(SDL_GAMEPAD_BUTTON_NORTH, invert ? BUTTON_B : BUTTON_A, true);
    }

    void Input::clear()
    {
        pressedKeys_.clear();
        fastForwardPressed_ = false;
        pauseToggleRequested_ = false;
    }

    bool Input::isFastForwardPressed() const { return fastForwardPressed_; }

    bool Input::takePauseToggleRequest()
    {
        const bool requested = pauseToggleRequested_;
        pauseToggleRequested_ = false;
        return requested;
    }

    void Input::handleEvent(const SDL_Event &event, bool enabled)
    {
        if (event.type == SDL_EVENT_GAMEPAD_ADDED)
            openGamepad(event.gdevice.which);
        else if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
            gamepads_.erase(event.gdevice.which);
        else if (event.type == SDL_EVENT_KEY_UP)
        {
            pressedKeys_.erase(event.key.key);
            if (event.key.key == SDLK_TAB)
                fastForwardPressed_ = false;
        }
        else if (event.type == SDL_EVENT_KEY_DOWN && enabled && !event.key.repeat &&
                 !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)))
        {
            if (event.key.key == SDLK_TAB)
                fastForwardPressed_ = true;
            else if (event.key.key == SDLK_PAUSE)
                pauseToggleRequested_ = !pauseToggleRequested_;
            else if (keyMap_.count(event.key.key))
                pressedKeys_.insert(event.key.key);
        }
        if (!enabled || event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
            clear();
    }

    std::uint8_t Input::update(std::uint64_t emulatedFrame, bool enabled)
    {
        if (!enabled)
        {
            clear();
            return 0;
        }
        std::uint8_t buttons = 0;
        // O pulso compartilha a fase entre todas as fontes; sobreposições são combinadas
        // por OR, portanto uma associação sem turbo mantém o botão pressionado.
        const bool pulse = emulatedFrame % 4 == 3;
        const auto apply = [&](const Binding &binding)
        {
            if (!binding.turbo || pulse)
                buttons |= binding.button;
        };
        for (const auto key : pressedKeys_)
            apply(keyMap_.at(key));
        for (const auto &[id, pad] : gamepads_)
        {
            (void)id;
            for (const auto &[physical, binding] : gamepadMap_)
                if (SDL_GetGamepadButton(pad.get(), physical))
                    apply(binding);
            constexpr int deadZone = 16000;
            const auto x = SDL_GetGamepadAxis(pad.get(), SDL_GAMEPAD_AXIS_LEFTX);
            const auto y = SDL_GetGamepadAxis(pad.get(), SDL_GAMEPAD_AXIS_LEFTY);
            if (x < -deadZone)
                buttons |= BUTTON_LEFT;
            if (x > deadZone)
                buttons |= BUTTON_RIGHT;
            if (y < -deadZone)
                buttons |= BUTTON_UP;
            if (y > deadZone)
                buttons |= BUTTON_DOWN;
        }
        return buttons;
    }
}
