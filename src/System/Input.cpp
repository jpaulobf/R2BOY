#include "Input.h"

#include <iostream>

namespace R2BOY::System
{
    using namespace Core::IO;

    Input::Input()
    {
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
        invert_ = invert;
        setKeyboardMapping(keyboardMapping_);
        mapGamepad(SDL_GAMEPAD_BUTTON_SOUTH, invert ? BUTTON_A : BUTTON_B);
        mapGamepad(SDL_GAMEPAD_BUTTON_EAST, BUTTON_A, invert);
        mapGamepad(SDL_GAMEPAD_BUTTON_WEST, BUTTON_B, !invert);
        mapGamepad(SDL_GAMEPAD_BUTTON_NORTH, invert ? BUTTON_B : BUTTON_A, true);
    }

    void Input::setKeyboardMapping(const KeyboardMapping &mapping)
    {
        keyboardMapping_ = mapping;
        clear();
        keyMap_.clear();
        constexpr GameBoyButtons buttons[] = {BUTTON_UP, BUTTON_DOWN, BUTTON_LEFT, BUTTON_RIGHT,
                                              BUTTON_B, BUTTON_A, BUTTON_B, BUTTON_A, BUTTON_START, BUTTON_SELECT};
        // Traduz os índices do layout armazenado para as funções após a inversão.
        constexpr int inverted[] = {0, 1, 2, 3, 5, 7, 4, 6, 8, 9};
        for (int i = 0; i < 10; ++i)
        {
            const int target = invert_ ? inverted[i] : i;
            if (keyboardMapping_[i])
                mapKey(keyboardMapping_[i], buttons[target], target == 6 || target == 7);
        }
    }

    void Input::clear()
    {
        pressedKeys_.clear();
        fastForwardPressed_ = false;
        rewindPressed_ = false;
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
            if (event.key.key == SDLK_CAPSLOCK)
                rewindPressed_ = false;
        }
        else if (event.type == SDL_EVENT_KEY_DOWN && enabled && !event.key.repeat &&
                 !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)))
        {
            if (event.key.key == SDLK_TAB)
                fastForwardPressed_ = true;
            else if (event.key.key == SDLK_CAPSLOCK)
                rewindPressed_ = true;
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