#include "Window/Window.h"
#include "Core/Board.h"
#include <imgui.h>
#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>

namespace
{
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void capturesKeyboardOnlyForInterface()
    {
        R2BOY::Window::Window window;
        check(window.open(), "headless window must open");
        R2BOY::Core::Board board;
        bool running = true;
        const auto frame = [&]()
        {
            check(window.renderFrame(running, board.cartridge(), board.framebuffer()),
                  "headless frame must render");
        };
        auto &io = ImGui::GetIO();
        io.AddFocusEvent(true);
        frame();
        frame();
        frame();
        check(!io.WantCaptureKeyboard,
              "main game window must not capture keyboard merely because it is focused");

        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = SDLK_O;
        event.key.scancode = SDL_SCANCODE_O;
        event.key.mod = SDL_KMOD_CTRL;
        check(SDL_PushEvent(&event), "Ctrl+O event must enter queue");
        window.pollEvents(running);
        frame();
        frame();
        check(io.WantCaptureKeyboard, "ROM selector must still capture keyboard");

        // Fecha o modal e verifica que o foco devolvido à janela libera o teclado.
        ImGui::NewFrame();
        ImGui::Begin("R2BOY", nullptr, ImGuiWindowFlags_NoNavInputs);
        check(ImGui::BeginPopupModal("Open ROM"), "ROM selector must be open");
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        ImGui::End();
        ImGui::Render();
        frame();
        frame();
        check(!io.WantCaptureKeyboard, "closing the selector returns keyboard to the game");
    }
}

int main()
{
    try
    {
        capturesKeyboardOnlyForInterface();
        std::cout << "Window keyboard capture tests passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
