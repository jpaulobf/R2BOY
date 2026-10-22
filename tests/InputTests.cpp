#include "System/Input.h"
#include "Core/Board.h"
#include <iostream>
#include <stdexcept>

using namespace R2BOY;
using namespace R2BOY::Core::IO;

namespace
{
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void key(System::Input &input, SDL_Keycode code, bool down, bool enabled = true)
    {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.key = code;
        input.handleEvent(event, enabled);
    }

    void joypad()
    {
        Core::Board board;
        board.reset();
        check(board.read(0xFF00) == 0xFF, "JOYP starts with no group selected");
        for (unsigned bit = 0; bit < 8; ++bit)
        {
            board.setButtons(0);
            board.write(0xFF00, 0x30);
            board.write(0xFF0F, 0);
            board.setButtons(static_cast<std::uint8_t>(1 << bit));
            check(board.read(0xFF00) == 0xFF && !(board.read(0xFF0F) & 0x10),
                  "unselected buttons do not affect lines or request IRQ");
            board.write(0xFF00, bit < 4 ? 0x20 : 0x10);
            const unsigned expected = 0x0F & ~(1 << (bit % 4));
            check((board.read(0xFF00) & 0x0F) == expected, "every button maps to its hardware line");
            check(board.read(0xFF0F) & 0x10, "selecting a held button requests IRQ");
            board.write(0xFF0F, 0);
            board.setButtons(static_cast<std::uint8_t>(1 << bit));
            check(!(board.read(0xFF0F) & 0x10), "held buttons do not repeat IRQ");
            board.setButtons(0);
            check(!(board.read(0xFF0F) & 0x10), "release does not request IRQ");
        }
        board.setButtons(BUTTON_A | BUTTON_LEFT);
        board.write(0xFF00, 0);
        check(board.read(0xFF00) == 0xCC, "both selected groups combine active-low lines");
        board.write(0xFF00, 0xCF);
        check(board.read(0xFF00) == 0xCC, "only selection bits are writable");
        board.reset();
        check(board.read(0xFF00) == 0xFF, "reset releases buttons and selection");

        // STOP pode ser exercitado diretamente, sem depender de uma ROM externa.
        board.write(0xC000, 0x10);
        board.write(0xC001, 0);
        Core::CPU::CPU::Registers registers;
        registers.pc = 0xC000;
        board.cpu().reset(registers);
        board.cpu().step();
        check(board.cpu().stopped(), "STOP entered");
        board.setButtons(0);
        check(board.cpu().stopped(), "empty input does not wake STOP");
        board.setButtons(BUTTON_START);
        check(!board.cpu().stopped(), "new physical input wakes STOP independently of IE");
    }

    void keyboard()
    {
        System::Input input;
        key(input, SDLK_W, true);
        key(input, SDLK_J, true);
        key(input, SDLK_K, true);
        check(input.update(0) == (BUTTON_UP | BUTTON_B | BUTTON_A), "default keyboard mapping");
        key(input, SDLK_K, false);
        key(input, SDLK_I, true);
        check(!(input.update(0) & BUTTON_A) && (input.update(3) & BUTTON_A), "turbo follows emulated frames");
        key(input, SDLK_K, true);
        check(input.update(0) & BUTTON_A, "normal button wins over turbo off phase");
        key(input, SDLK_I, false);
        check(input.update(0) & BUTTON_A, "releasing turbo preserves normal source");
        input.mapKey(SDLK_L, BUTTON_A);
        key(input, SDLK_L, true);
        key(input, SDLK_K, false);
        check(input.update(0) & BUTTON_A, "releasing one binding preserves another");
        check(input.update(0, false) == 0 && input.update(0) == 0, "UI capture clears held keys");
        key(input, SDLK_W, true, false);
        check(input.update(0) == 0, "captured key presses ignored");
        key(input, SDLK_W, true);
        SDL_Event event{};
        event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        input.handleEvent(event, true);
        check(input.update(0) == 0, "focus loss releases keys");
        input.configureABBAButtons(true);
        key(input, SDLK_J, true);
        key(input, SDLK_U, true);
        check(input.update(0) == (BUTTON_A | BUTTON_B), "inverted normal layout");
        input.clear();
        key(input, SDLK_K, true);
        key(input, SDLK_I, true);
        check(input.update(0) == 0 && input.update(3) == (BUTTON_A | BUTTON_B), "inverted turbo layout");
    }

    void fastForward()
    {
        System::Input input;
        check(!input.isFastForwardPressed(), "fast-forward starts released");
        key(input, SDLK_TAB, true);
        check(input.isFastForwardPressed() && input.update(0) == 0,
              "TAB holds fast-forward without pressing joypad buttons");
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = SDLK_TAB;
        event.key.repeat = true;
        input.handleEvent(event, true);
        check(input.isFastForwardPressed(), "key repeat must preserve held fast-forward");
        key(input, SDLK_TAB, false, false);
        check(!input.isFastForwardPressed(), "TAB release must work during UI capture");
        key(input, SDLK_TAB, true, false);
        check(!input.isFastForwardPressed(), "captured TAB must not start fast-forward");
        key(input, SDLK_TAB, true);
        input.update(0, false);
        check(!input.isFastForwardPressed(), "UI capture must cancel held fast-forward");
        key(input, SDLK_TAB, true);
        event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        input.handleEvent(event, true);
        check(!input.isFastForwardPressed(), "focus loss must cancel held fast-forward");
        key(input, SDLK_TAB, true);
        input.clear();
        check(!input.isFastForwardPressed(), "runtime reset must cancel held fast-forward");
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.repeat = false;
        event.key.mod = SDL_KMOD_CTRL;
        input.handleEvent(event, true);
        check(!input.isFastForwardPressed(), "modified TAB must not start fast-forward");
    }

    void gamepad()
    {
        System::Input input;
        check(input.initialize(), SDL_GetError());
        SDL_VirtualJoystickDesc desc{};
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        desc.name = "R2BOY input test";
        const auto id = SDL_AttachVirtualJoystick(&desc);
        check(id != 0, SDL_GetError());
        SDL_Event event{};
        event.type = SDL_EVENT_GAMEPAD_ADDED;
        event.gdevice.which = id;
        input.handleEvent(event, true);
        SDL_Joystick *stick = SDL_OpenJoystick(id);
        check(stick != nullptr, SDL_GetError());
        check(SDL_SetJoystickVirtualButton(stick, SDL_GAMEPAD_BUTTON_EAST, true), SDL_GetError());
        SDL_UpdateGamepads();
        check(input.update(0) == BUTTON_A, "virtual gamepad maps East to A");
        key(input, SDLK_K, true);
        key(input, SDLK_K, false);
        check(input.update(0) == BUTTON_A, "keyboard release preserves gamepad input");
        check(SDL_SetJoystickVirtualButton(stick, SDL_GAMEPAD_BUTTON_EAST, false), SDL_GetError());
        check(SDL_SetJoystickVirtualButton(stick, SDL_GAMEPAD_BUTTON_NORTH, true), SDL_GetError());
        check(SDL_SetJoystickVirtualAxis(stick, SDL_GAMEPAD_AXIS_LEFTX, -24000), SDL_GetError());
        SDL_UpdateGamepads();
        check(input.update(0) == BUTTON_LEFT && input.update(3) == (BUTTON_LEFT | BUTTON_A),
              "gamepad turbo and analog dead zone");
        check(input.update(3, false) == 0, "UI capture also blocks gamepad");
        SDL_CloseJoystick(stick);
        check(SDL_DetachVirtualJoystick(id), SDL_GetError());
        event.type = SDL_EVENT_GAMEPAD_REMOVED;
        input.handleEvent(event, true);
        check(input.update(3) == 0, "disconnect releases gamepad input");
        input.shutdown();
        input.shutdown();
    }
}

int main()
{
    try
    {
        joypad();
        keyboard();
        fastForward();
        gamepad();
        std::cout << "Input mapping, turbo, virtual gamepad and JOYP tests passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
